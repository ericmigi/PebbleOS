/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "esp32s3.h"

#include "diskio.h"

#include <errno.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/pinctrl.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_gpio.h>
#include <hardware/esp32s3_system.h>

struct sd_host {
  struct pbl_device dev;
  uintptr_t base;
  struct pbl_gpio power;
  struct pbl_pinctrl pins;
};
static const struct sd_host *s_host;
static struct sd_card s_card;
static bool s_ready;
static uint32_t s_rca;
static uint32_t s_cached;
static bool s_cache_valid;
static _Alignas(4) BYTE s_sector[512];
static volatile uint32_t s_desc[4];

enum {
  CTRL = 0x00,
  PWREN = 0x04,
  CLKDIV = 0x08,
  CLKSRC = 0x0c,
  CLKENA = 0x10,
  TMOUT = 0x14,
  CTYPE = 0x18,
  BLKSIZ = 0x1c,
  BYTCNT = 0x20,
  CMDARG = 0x28,
  CMD = 0x2c,
  RESP = 0x30,
  RAW = 0x44,
  STATUS = 0x48,
  BMOD = 0x80,
  POLL = 0x84,
  DBADDR = 0x88,
  IDSTS = 0x8c,
  IDINTEN = 0x90,
  CLOCK = 0x800,
};
#define START            (PBL_BIT(31) | PBL_BIT(29) | PBL_BIT(16))
#define REPLY            PBL_BIT(6)
#define R1               (REPLY | PBL_BIT(8))
#define R2               (R1 | PBL_BIT(7))
#define DATA             PBL_BIT(9)
#define CMD_DONE         PBL_BIT(2)
#define DATA_DONE        PBL_BIT(3)
#define RESPONSE_TIMEOUT PBL_BIT(8)
#define ERRORS           (PBL_BIT(15) | PBL_GENMASK(13, 6) | PBL_BIT(1))
#define DMA_BITS         (PBL_BIT(25) | PBL_BIT(5))
#define R1_ERRORS        0xfdffe008U

static uint32_t rd(unsigned reg) {
  return pbl_readl(s_host->base + reg);
}
static void wr(unsigned reg, uint32_t val) {
  pbl_writel(val, s_host->base + reg);
}

static int prv_clock_update(void) {
  wr(CMDARG, 0);
  wr(CMD, START | PBL_BIT(21) | PBL_BIT(13));
  int ret = PBL_POLL_SLEEP_MS(!(rd(CMD) & PBL_BIT(31)), 1, 30);
  return ret ? ret : (rd(RAW) & PBL_BIT(12)) ? -EIO : 0;
}

static int prv_clock(uint8_t div) {
  wr(CLKENA, 0);
  int ret = prv_clock_update();
  if (ret != 0)
    return ret;
  wr(CLKSRC, 1U << 2); // Slot 1 uses divider 1.
  wr(CLKDIV, (uint32_t)div << 8);
  ret = prv_clock_update();
  if (ret != 0)
    return ret;
  wr(CLKENA, PBL_BIT(1));
  return prv_clock_update();
}

static int prv_cmd(uint8_t cmd, uint32_t arg, uint32_t flags) {
  s_card.last_cmd = cmd;
  wr(RAW, UINT32_MAX);
  wr(CMDARG, arg);
  wr(CMD, START | PBL_BIT(13) | flags | cmd);
  int ret = PBL_POLL_SLEEP_MS((rd(RAW) & (ERRORS | CMD_DONE)) != 0U, 1, 150);
  s_card.last_status = rd(RAW);
  if (ret != 0 || (s_card.last_status & RESPONSE_TIMEOUT))
    return -ETIMEDOUT;
  return (s_card.last_status & ERRORS) ? -EIO : 0;
}

static int prv_reset(void) {
  // Gate card power only on an explicit probe. No storage activity runs during boot.
  int ret;
  if (pbl_gpio_is_connected(&s_host->power)) {
    ret = pbl_gpio_configure(&s_host->power, PBL_GPIO_OUTPUT_INACTIVE);
    if (ret != 0)
      return ret;
    pbl_sleep_ms(80);
    ret = pbl_gpio_set(&s_host->power, true);
    if (ret != 0)
      return ret;
    pbl_sleep_ms(120);
  }
  ret = pbl_pinctrl_select_state(&s_host->pins, PBL_PINCTRL_STATE_DEFAULT);
  if (ret != 0)
    return ret;

  const uintptr_t clk = PBL_DT_REG_SYSTEM + ESP32S3_SYSTEM_PERIP_CLK_EN1_OFFSET;
  const uintptr_t rst = PBL_DT_REG_SYSTEM + ESP32S3_SYSTEM_PERIP_RST_EN1_OFFSET;
  pbl_irq_lock();
  pbl_modifyl(0, PBL_BIT(7), clk);
  pbl_modifyl(0, PBL_BIT(7), rst);
  pbl_modifyl(PBL_BIT(7), 0, rst);
  pbl_irq_unlock();
  // Probe card presence using command responses. Keep WP asserted for this read-only host.
  pbl_writel(GPIO_FUNC_IN_SEL | 0x3cU, PBL_DT_REG_GPIO + ESP32C3_GPIO_FUNC_IN_SEL_CFG_OFFSET(195));
  pbl_writel(GPIO_FUNC_IN_SEL | 0x38U, PBL_DT_REG_GPIO + ESP32C3_GPIO_FUNC_IN_SEL_CFG_OFFSET(197));
  pbl_writel(GPIO_FUNC_IN_SEL | 0x38U, PBL_DT_REG_GPIO + ESP32C3_GPIO_FUNC_IN_SEL_CFG_OFFSET(199));
  // 40 MHz crystal / 2. Core and input phase 0, output phase 90 degrees.
  wr(CLOCK, 1U | (1U << 13) | (1U << 17));
  wr(CTRL, 7);
  ret = PBL_POLL_SLEEP_MS(!(rd(CTRL) & 7), 1, 100);
  if (ret != 0)
    return ret;
  wr(BMOD, 1);
  ret = PBL_POLL_SLEEP_MS(!(rd(BMOD) & 1), 1, 100);
  if (ret != 0)
    return ret;
  wr(PWREN, PBL_BIT(1));
  wr(CTYPE, 0);
  wr(TMOUT, UINT32_MAX);
  wr(RAW, UINT32_MAX);
  return prv_clock(25);
}

static int prv_read_sector(uint32_t sector) {
  if ((uint64_t)sector >= s_card.sectors)
    return -ERANGE;
  s_cache_valid = false;
  s_desc[0] = PBL_BIT(31) | PBL_BIT(4) | PBL_BIT(3) | PBL_BIT(2);
  s_desc[1] = sizeof(s_sector);
  s_desc[2] = (uintptr_t)s_sector;
  s_desc[3] = 0;
  __asm__ volatile("memw" ::: "memory");
  wr(BLKSIZ, 512);
  wr(BYTCNT, 512);
  wr(DBADDR, (uintptr_t)s_desc);
  wr(IDSTS, UINT32_MAX);
  wr(IDINTEN, PBL_BIT(8) | PBL_BIT(1) | PBL_BIT(0));
  wr(CTRL, DMA_BITS);
  wr(BMOD, PBL_BIT(7) | PBL_BIT(1));
  wr(POLL, 1);
  int ret = prv_cmd(17, s_card.block_addressed ? sector : sector * 512U, R1 | DATA);
  if (ret == 0 && (rd(RESP) & R1_ERRORS))
    ret = -EIO;
  if (ret == 0) {
    ret = PBL_POLL_SLEEP_MS(
        (rd(RAW) & ERRORS) || ((rd(RAW) & DATA_DONE) && !(s_desc[0] & PBL_BIT(31))), 1, 250);
    s_card.last_status = rd(RAW);
    if ((s_card.last_status & ERRORS) || (s_desc[0] & PBL_BIT(30)))
      ret = -EIO;
  }
  wr(BMOD, 0);
  wr(CTRL, 0);
  __asm__ volatile("memw" ::: "memory");
  if (ret == 0) {
    s_cached = sector;
    s_cache_valid = true;
  }
  return ret;
}

static uint32_t prv_csd_bits(const uint32_t csd[4], unsigned start, unsigned len) {
  uint32_t value = csd[start / 32] >> (start % 32);
  if ((start % 32) + len > 32)
    value |= csd[start / 32 + 1] << (32 - (start % 32));
  return value & ((1U << len) - 1U);
}

static int prv_identify(void) {
  int ret = prv_reset();
  if (ret != 0)
    return ret;
  ret = prv_cmd(0, 0, PBL_BIT(15));
  if (ret != 0)
    return ret;
  ret = prv_cmd(8, 0x1aa, R1);
  bool modern = ret == 0;
  if (ret != 0 && ret != -ETIMEDOUT)
    return ret;
  if (modern && (rd(RESP) & 0xfffU) != 0x1aa)
    return -ENOTSUP;
  uint64_t start = pbl_uptime_ms();
  uint32_t ocr;
  do {
    ret = prv_cmd(55, 0, R1);
    if (ret != 0)
      return ret;
    if ((rd(RESP) & R1_ERRORS) || !(rd(RESP) & PBL_BIT(5)))
      return -ENOTSUP;
    ret = prv_cmd(41, 0x00ff8000U | (modern ? PBL_BIT(30) : 0), REPLY);
    if (ret != 0)
      return ret;
    ocr = rd(RESP);
    if (ocr & PBL_BIT(31))
      break;
    pbl_sleep_ms(10);
  } while (pbl_uptime_ms() - start < 1500);
  if (!(ocr & PBL_BIT(31)))
    return -ETIMEDOUT;
  if (!(ocr & 0x00ff8000U))
    return -ENOTSUP;
  s_card.block_addressed = (ocr & PBL_BIT(30)) != 0;
  ret = prv_cmd(2, 0, R2);
  if (ret != 0)
    return ret;
  for (unsigned i = 0; i < 4; i++)
    s_card.cid[i] = rd(RESP + 4 * i);
  ret = prv_cmd(3, 0, R1);
  if (ret != 0)
    return ret;
  s_rca = rd(RESP) & 0xffff0000U;
  if (s_rca == 0)
    return -EIO;
  ret = prv_cmd(9, s_rca, R2);
  if (ret != 0)
    return ret;
  uint32_t csd[4];
  for (unsigned i = 0; i < 4; i++)
    csd[i] = rd(RESP + 4 * i);
  switch (prv_csd_bits(csd, 126, 2)) {
    case 0:
      s_card.sectors = (((uint64_t)prv_csd_bits(csd, 62, 12) + 1U)
                        << (prv_csd_bits(csd, 47, 3) + 2 + prv_csd_bits(csd, 80, 4))) /
                       512;
      break;
    case 1:
      s_card.sectors = ((uint64_t)prv_csd_bits(csd, 48, 22) + 1U) * 1024;
      break;
    default:
      return -ENOTSUP;
  }
  ret = prv_cmd(7, s_rca, R1);
  if (ret != 0)
    return ret;
  if (rd(RESP) & R1_ERRORS)
    return -EIO;
  ret = PBL_POLL_SLEEP_MS(!(rd(STATUS) & PBL_BIT(9)), 1, 100);
  if (ret != 0)
    return ret;
  if (!s_card.block_addressed) {
    ret = prv_cmd(16, 512, R1);
    if (ret != 0)
      return ret;
    if (rd(RESP) & R1_ERRORS)
      return -EIO;
  }
  ret = prv_clock(2); // 5 MHz for the one-bit data path.
  return ret ? ret : prv_read_sector(0);
}

int sd_card_probe(void) {
  if (s_host == nullptr || !pbl_device_is_ready(&s_host->dev))
    return -ENODEV;
  s_ready = false;
  s_cache_valid = false;
  int ret = -EIO;
  for (unsigned attempt = 0; attempt < 3; attempt++) {
    memset(&s_card, 0, sizeof(s_card));
    ret = prv_identify();
    if (ret == 0) {
      s_ready = true;
      break;
    }
  }
  return ret;
}

const struct sd_card *sd_card_info(void) {
  return &s_card;
}

DSTATUS disk_initialize(void) {
  return s_ready ? 0 : STA_NOINIT;
}

DRESULT disk_readp(BYTE *buf, DWORD sector, UINT offset, UINT count) {
  if (!s_ready)
    return RES_NOTRDY;
  if (offset > 512 || count > 512 - offset || buf == nullptr)
    return RES_PARERR;
  if ((!s_cache_valid || sector != s_cached) && prv_read_sector(sector) != 0) {
    s_ready = false;
    return RES_ERROR;
  }
  memcpy(buf, s_sector + offset, count);
  return RES_OK;
}

static int prv_init(const struct pbl_device *dev) {
  s_host = (const struct sd_host *)dev;
  return 0;
}

#define PBL_SDMMC_DEFINE(sym, addr, pin_state, ...)                        \
  PBL_DEVICE_STATE_DEFINE(sym);                                            \
  PBL_DEVICE_INSTANCE_DEV(sym, struct sd_host) {                           \
    .dev = PBL_DEVICE_INIT(sym, "SDMMC", prv_init, nullptr), .base = addr, \
    .pins = PBL_PINCTRL(pin_state, nullptr, 0), __VA_ARGS__                \
  }
#include PBL_DT_INSTANCES(esp_esp32s3_sdmmc)
