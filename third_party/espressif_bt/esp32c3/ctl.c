/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Guarded bring-up of the controller, and the btc shell command.
//
// The PHY init can turn the USB PHY off, and with it the USB-Serial-JTAG console, the only way
// into a board without a boot button. So the bring-up runs only when asked for, with the RTC
// watchdog armed: unless the console proves alive with esp32c3_bt_confirm() in time, the
// watchdog resets the whole chip, RTC included, which restores the USB PHY. A retained register
// records the attempt; the boot after one that never completed refuses to try again.

#include "esp32c3_regs.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/drivers/mcu.h>
#include <pbl/kernel/heap.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/malloc.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/system/rtc_registers.h>
#include <pbl/util/size.h>

#include <esp32c3_bt.h>

PBL_LOG_MODULE_DECLARE(espressif_bt, CONFIG_ESP32C3_BT_LOG_LEVEL);

// The retained register: a magic, so that noise after a power-on reads as nothing, and flags
#define STATE_MAGIC_MASK  0xffff0000U
#define STATE_MAGIC       0xb7c30000U
#define STATE_ARMED       PBL_BIT(0)
#define STATE_IN_PROGRESS PBL_BIT(1)
#define STATE_FAILED      PBL_BIT(2)

// The RTC watchdog, which the SoC init disables at every boot
#define RTC_CNTL_WDTCONFIG0_REG    (DR_REG_RTCCNTL_BASE + RTC_CNTL_RADIO_OFFSET + 0x0090U)
#define RTC_CNTL_WDTCONFIG1_REG    (DR_REG_RTCCNTL_BASE + RTC_CNTL_RADIO_OFFSET + 0x0094U)
#define RTC_CNTL_WDTFEED_REG       (DR_REG_RTCCNTL_BASE + RTC_CNTL_RADIO_OFFSET + 0x00a4U)
#define RTC_CNTL_WDTWPROTECT_REG   (DR_REG_RTCCNTL_BASE + RTC_CNTL_RADIO_OFFSET + 0x00a8U)
#define RTC_CNTL_WDT_EN            PBL_BIT(31)
#define RTC_CNTL_WDT_STG0_S        28U
#define RTC_WDT_STG_SEL_RESET_RTC  4U
#define RTC_CNTL_WDT_CPU_RST_LEN   (7U << 16)
#define RTC_CNTL_WDT_PROCPU_RST_EN PBL_BIT(11)
#define RTC_CNTL_WDT_SYS_RST_LEN   (7U << 13)
#define RTC_CNTL_WDT_FEED          PBL_BIT(31)
#define RTC_WDT_WKEY               0x50d83aa1U
// RC_SLOW, the RTC slow clock the ROM leaves: 136 kHz give or take a few percent
#define RTC_SLOW_HZ 136000U

static uint32_t s_state;
static bool s_loaded;

static void prv_state_store(void) {
  retained_write(ESP32C3_BT_STATE_REGISTER, s_state != 0U ? (STATE_MAGIC | s_state) : 0U);
}

#define STATE_STAGE_MASK  0xff00U
#define STATE_STAGE_SHIFT 8U

void esp_bt_init_stage(enum esp_bt_init_stage stage) {
  s_state = (s_state & ~STATE_STAGE_MASK) | ((uint32_t)stage << STATE_STAGE_SHIFT);
  prv_state_store();
}

// On the first look of this boot, an attempt still in progress is one that never completed
static void prv_state_load(void) {
  uint32_t v;

  if (s_loaded) {
    return;
  }
  s_loaded = true;

  v = retained_read(ESP32C3_BT_STATE_REGISTER);
  s_state = (v & STATE_MAGIC_MASK) == STATE_MAGIC ? (v & ~STATE_MAGIC_MASK) : 0U;
  if ((s_state & STATE_IN_PROGRESS) != 0U) {
    PBL_LOG_WRN("Bluetooth bring-up did not complete on the last boot: disabled");
    s_state = (s_state & ~(STATE_IN_PROGRESS | STATE_ARMED)) | STATE_FAILED;
    prv_state_store();
  }
}

static void prv_wdt_write(uint32_t config0, uint32_t config1) {
  pbl_writel(RTC_WDT_WKEY, RTC_CNTL_WDTWPROTECT_REG);
  pbl_writel(0U, RTC_CNTL_WDTCONFIG0_REG);
  pbl_writel(config1, RTC_CNTL_WDTCONFIG1_REG);
  pbl_writel(RTC_CNTL_WDT_FEED, RTC_CNTL_WDTFEED_REG);
  pbl_writel(config0, RTC_CNTL_WDTCONFIG0_REG);
  pbl_writel(0U, RTC_CNTL_WDTWPROTECT_REG);
}

// The eFuse's WDT_DELAY_SEL multiplies stage 0's timeout by 2 to 16, as ESP-IDF's
// rwdt_ll_config_stage() accounts for
#ifdef CONFIG_SOC_ESP32S3
#define EFUSE_RD_REPEAT_DATA1_REG 0x60007034U
#else
#define EFUSE_RD_REPEAT_DATA1_REG 0x60008834U
#endif
#define EFUSE_WDT_DELAY_SEL_MASK PBL_GENMASK(17, 16)

static void prv_wdt_arm(uint32_t seconds) {
  const uint32_t delay_sel =
      PBL_FIELD_GET(EFUSE_WDT_DELAY_SEL_MASK, pbl_readl(EFUSE_RD_REPEAT_DATA1_REG));

  prv_wdt_write(RTC_CNTL_WDT_EN | (RTC_WDT_STG_SEL_RESET_RTC << RTC_CNTL_WDT_STG0_S) |
                    RTC_CNTL_WDT_CPU_RST_LEN | RTC_CNTL_WDT_SYS_RST_LEN |
                    RTC_CNTL_WDT_PROCPU_RST_EN,
                (seconds * RTC_SLOW_HZ) >> (1U + delay_sel));
}

static bool prv_wdt_armed(void) {
  return (pbl_readl(RTC_CNTL_WDTCONFIG0_REG) & RTC_CNTL_WDT_EN) != 0U;
}

int esp32c3_bt_start(void) {
  int ret;

  prv_state_load();
  if ((s_state & STATE_FAILED) != 0U) {
    return -EPERM;
  }
  if (esp32c3_bt_controller_enabled()) {
    return 0;
  }

  s_state |= STATE_IN_PROGRESS;
  prv_state_store();
  prv_wdt_arm(CONFIG_ESP32C3_BT_CONFIRM_TIMEOUT_S);
#ifdef CONFIG_ESP32C3_BT_REQUIRE_CONFIRM
  PBL_LOG_INFO("Bluetooth bring-up: confirm within %u s or the chip resets",
               (unsigned int)CONFIG_ESP32C3_BT_CONFIRM_TIMEOUT_S);
#else
  PBL_LOG_DBG("Bluetooth bring-up: the chip resets if it hangs for %u s",
              (unsigned int)CONFIG_ESP32C3_BT_CONFIRM_TIMEOUT_S);
#endif

  const size_t heap_before = pbl_heap_used(pbl_malloc_heap());
  ret = esp32c3_bt_controller_init();
  if (ret == 0) {
    ret = esp32c3_bt_controller_enable();
  }
  if (ret != 0) {
    PBL_LOG_ERR("Bluetooth bring-up failed (%d)", ret);
    s_state |= STATE_FAILED;
    prv_state_store();
    return ret;
  }

  PBL_LOG_INFO("Bluetooth controller enabled, %u bytes of kernel heap",
               (unsigned int)(pbl_heap_used(pbl_malloc_heap()) - heap_before));

#ifndef CONFIG_ESP32C3_BT_REQUIRE_CONFIRM
  // The console's survival is taken as given: only a hang in the bring-up resets the chip
  esp32c3_bt_confirm();
#endif

  return 0;
}

void esp32c3_bt_confirm(void) {
  prv_state_load();
  prv_wdt_write(0U, 0U);
  s_state &= ~(STATE_IN_PROGRESS | STATE_STAGE_MASK);
  prv_state_store();
}

void esp32c3_bt_clear(void) {
  prv_state_load();
  s_state = 0U;
  prv_state_store();
}

void esp32c3_bt_arm(void) {
  prv_state_load();
  s_state |= STATE_ARMED;
  prv_state_store();
}

bool esp32c3_bt_armed(void) {
  prv_state_load();
#ifdef CONFIG_ESP32C3_BT_ARMED_BY_DEFAULT
  return (s_state & STATE_FAILED) == 0U;
#else
  return (s_state & (STATE_ARMED | STATE_FAILED)) == STATE_ARMED;
#endif
}

bool esp32c3_bt_failed(void) {
  prv_state_load();
  return (s_state & STATE_FAILED) != 0U;
}

#ifdef CONFIG_ESP32C3_BT_SHELL
#include <pbl/shell/shell.h>

#define HCI_RX_MAX   4U
#define HCI_RX_BYTES 64U

static struct {
  uint16_t len;
  uint8_t data[HCI_RX_BYTES];
} s_rx[HCI_RX_MAX];
static volatile uint32_t s_rx_count;
static PBL_SEM_DEFINE(s_rx_sem, 0, 1);

static int prv_raw_recv(uint8_t *data, uint16_t len) {
  uint32_t n = s_rx_count;

  if (n < HCI_RX_MAX) {
    s_rx[n].len = len;
    memcpy(s_rx[n].data, data, len < HCI_RX_BYTES ? len : HCI_RX_BYTES);
    s_rx_count = n + 1U;
  }
  pbl_sem_give(&s_rx_sem);
  return 0;
}

static void prv_raw_send_available(void) {
}

static const struct esp32c3_bt_vhci_callbacks s_raw_cb = {
  .send_available = prv_raw_send_available,
  .recv = prv_raw_recv,
};

static void prv_print_hex(const struct pbl_shell *sh, const char *prefix, const uint8_t *data,
                          size_t len) {
  char line[3U * HCI_RX_BYTES + 1U];
  size_t n = 0U;

  for (size_t i = 0U; i < len && i < HCI_RX_BYTES; i++) {
    static const char hex[] = "0123456789abcdef";

    line[n++] = hex[data[i] >> 4];
    line[n++] = hex[data[i] & 0xfU];
    line[n++] = ' ';
  }
  line[n > 0U ? n - 1U : 0U] = '\0';
  pbl_shell_print(sh, "%s%s", prefix, line);
}

static int prv_cmd_status(const struct pbl_shell *sh, size_t argc, char **argv) {
  uint8_t mac[6];
  size_t len = sizeof(mac);

  prv_state_load();
  pbl_shell_print(sh, "state: %s%s%s%s", s_state == 0U ? "off" : "",
                  (s_state & STATE_ARMED) != 0U ? "armed " : "",
                  (s_state & STATE_IN_PROGRESS) != 0U ? "in-progress " : "",
                  (s_state & STATE_FAILED) != 0U ? "failed" : "");
  static const char *const stages[] = {
    [EspBtStage_None] = "none",
    [EspBtStage_Power] = "power domain",
    [EspBtStage_Rom] = "ROM data",
    [EspBtStage_Osi] = "OS interface registration",
    [EspBtStage_Clocks] = "controller clocks",
    [EspBtStage_Reset] = "controller reset",
    [EspBtStage_LpClock] = "low-power clock source",
    [EspBtStage_LpDiv] = "low-power clock divider",
    [EspBtStage_ControllerInit] = "link-layer initialize",
    [EspBtStage_Features] = "HCI features",
    [EspBtStage_Phy] = "PHY power",
    [EspBtStage_UsbPll] = "USB PLL preservation",
    [EspBtStage_Calibrate] = "PHY calibration",
    [EspBtStage_Enable] = "link-layer enable",
    [EspBtStage_Coex] = "coexistence initialization",
  };
  uint32_t stage = (s_state & STATE_STAGE_MASK) >> STATE_STAGE_SHIFT;
  if (stage != 0 && stage < PBL_ARRAY_SIZE(stages)) {
    pbl_shell_print(sh, "last bring-up stage: %s", stages[stage]);
  }
  pbl_shell_print(sh, "CPU measured against SYSTIMER: %u MHz", (unsigned)esp_bt_measured_cpu_mhz());
  pbl_shell_print(sh, "controller: %s, watchdog %s",
                  esp32c3_bt_controller_enabled() ? "enabled" : "off",
                  prv_wdt_armed() ? "armed" : "off");
  if (mcu_get_serial(mac, &len) == 0 && len == sizeof(mac)) {
    pbl_shell_print(sh, "expected BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                    mac[3], mac[4], (uint8_t)(mac[5] + 2U));
  }
  if (esp32c3_bt_controller_enabled()) {
    uint32_t source, count;

    pbl_shell_print(sh, "version: %s", esp32c3_bt_controller_version());
    uint32_t isr_us, wake_us;

    esp32c3_bt_timing_stats(&isr_us, &wake_us);
    pbl_shell_print(sh, "since last: longest ISR %" PRIu32 " us, ISR to thread %" PRIu32 " us",
                    isr_us, wake_us);
    for (uint32_t i = 0U; esp32c3_bt_intr_stats(i, &source, &count); i++) {
      pbl_shell_print(sh, "interrupt source %" PRIu32 ": %" PRIu32 " calls", source, count);
    }
  }
  pbl_shell_print(sh, "kernel heap: %u used of %u, %u headroom at worst",
                  (unsigned int)pbl_heap_used(pbl_malloc_heap()),
                  (unsigned int)pbl_heap_size(pbl_malloc_heap()),
                  (unsigned int)pbl_heap_min_headroom(pbl_malloc_heap()));
  return 0;
}

static int prv_cmd_start(const struct pbl_shell *sh, size_t argc, char **argv) {
  int ret = esp32c3_bt_start();

  pbl_shell_print(sh, "start: %d", ret);
  return ret;
}

static int prv_cmd_confirm(const struct pbl_shell *sh, size_t argc, char **argv) {
  esp32c3_bt_confirm();
  pbl_shell_print(sh, "confirmed, watchdog off");
  return 0;
}

static int prv_cmd_arm(const struct pbl_shell *sh, size_t argc, char **argv) {
  esp32c3_bt_arm();
  pbl_shell_print(sh, "armed: %s at the next boot", esp32c3_bt_armed() ? "NimBLE uses it" : "no");
  return 0;
}

static int prv_cmd_clear(const struct pbl_shell *sh, size_t argc, char **argv) {
  esp32c3_bt_clear();
  pbl_shell_print(sh, "cleared");
  return 0;
}

// btc hci 01 03 0c 00: an H4 packet, type byte first; prints what comes back within 500 ms
static int prv_cmd_hci(const struct pbl_shell *sh, size_t argc, char **argv) {
  static bool s_registered;
  uint8_t pkt[64];
  size_t len = 0U;

  if (!esp32c3_bt_controller_enabled()) {
    pbl_shell_error(sh, "controller off: btc start");
    return -EINVAL;
  }
  if (esp32c3_bt_armed()) {
    pbl_shell_error(sh, "NimBLE owns the controller");
    return -EBUSY;
  }
  // Hex bytes, several to an argument if need be: the shell takes few arguments
  for (size_t i = 1U; i < argc; i++) {
    const size_t n = strlen(argv[i]);

    if (n == 0U || n % 2U != 0U || len + n / 2U > sizeof(pkt)) {
      pbl_shell_error(sh, "bad bytes '%s'", argv[i]);
      return -EINVAL;
    }
    for (size_t j = 0U; j < n; j += 2U) {
      char byte[3] = {argv[i][j], argv[i][j + 1U], '\0'};
      char *end;
      unsigned long b = strtoul(byte, &end, 16);

      if (*end != '\0') {
        pbl_shell_error(sh, "bad bytes '%s'", argv[i]);
        return -EINVAL;
      }
      pkt[len++] = (uint8_t)b;
    }
  }
  if (!s_registered) {
    int ret = esp32c3_bt_vhci_register(&s_raw_cb);

    if (ret != 0) {
      pbl_shell_error(sh, "register: %d", ret);
      return ret;
    }
    s_registered = true;
  }

  s_rx_count = 0U;
  pbl_sem_reset(&s_rx_sem);
  if (!esp32c3_bt_vhci_send_available()) {
    pbl_shell_error(sh, "controller busy");
    return -EBUSY;
  }
  prv_print_hex(sh, "> ", pkt, len);
  esp32c3_bt_vhci_send(pkt, (uint16_t)len);
  while (pbl_sem_take(&s_rx_sem, PBL_MSEC(500)) == 0) {
  }
  for (uint32_t i = 0U; i < s_rx_count; i++) {
    prv_print_hex(sh, "< ", s_rx[i].data, s_rx[i].len);
  }
  if (s_rx_count == 0U) {
    pbl_shell_print(sh, "< nothing");
  }
  return 0;
}

// The analog and clock state a direct boot leaves, which ESP-IDF's rtc_init() would have set
static const struct {
  const char *name;
  uint32_t addr;
} s_regs[] = {
#ifdef CONFIG_SOC_ESP32S3
  {"RTC_CNTL_DIG_PWC", RTC_CNTL_DIG_PWC_REG},       {"RTC_CNTL_DIG_ISO", RTC_CNTL_DIG_ISO_REG},
  {"RTC_CNTL_WDTCONFIG0", RTC_CNTL_WDTCONFIG0_REG}, {"SYSCON_WIFI_CLK_EN", SYSTEM_WIFI_CLK_EN_REG},
  {"SYSCON_WIFI_RST_EN", SYSTEM_WIFI_RST_EN_REG},
#else
  {"RTC_CNTL_OPTIONS0", 0x60008000U},
  {"RTC_CNTL_ANA_CONF", 0x60008034U},
  {"RTC_CNTL_RESET_STATE", 0x60008038U},
  {"RTC_CNTL_CLK_CONF", 0x60008070U},
  {"RTC_CNTL_SLOW_CLK_CONF", 0x60008074U},
  {"RTC_CNTL_BIAS_CONF", 0x6000807cU},
  {"RTC_CNTL_REG", 0x60008080U},
  {"RTC_CNTL_PWC", 0x60008084U},
  {"RTC_CNTL_DIG_PWC", 0x60008088U},
  {"RTC_CNTL_DIG_ISO", 0x6000808cU},
  {"RTC_CNTL_WDTCONFIG0", 0x60008090U},
  {"RTC_CNTL_STORE4", 0x600080b8U},
  {"RTC_CNTL_STORE5", 0x600080bcU},
  {"RTC_CNTL_USB_CONF", 0x600080ecU},
  {"RTC_CNTL_SENSOR_CTRL", 0x6000811cU},
  {"SYSCON_WIFI_CLK_EN", 0x60026014U},
  {"SYSCON_WIFI_RST_EN", 0x60026018U},
  {"SYSTEM_CPU_PER_CONF", 0x600c0008U},
  {"SYSTEM_SYSCLK_CONF", 0x600c0058U},
  {"SYSTEM_BT_LPCK_DIV_INT", 0x600c0020U},
  {"SYSTEM_BT_LPCK_DIV_FRAC", 0x600c0024U},
  {"PWDET_CONF", 0x6000e060U},
  {"USB_SERIAL_JTAG_CONF0", 0x60043018U},
  {"USB_SERIAL_JTAG_FRAM_NUM", 0x60043024U},
  {"USB_SERIAL_JTAG_FRAM_NUM", 0x60043024U},
#endif
};

static int prv_cmd_regs(const struct pbl_shell *sh, size_t argc, char **argv) {
  for (size_t i = 0U; i < sizeof(s_regs) / sizeof(s_regs[0]); i++) {
    pbl_shell_print(sh, "%-24s %08" PRIx32 " = %08" PRIx32, s_regs[i].name, s_regs[i].addr,
                    pbl_readl(s_regs[i].addr));
  }
  return 0;
}

// btc rd ADDR [COUNT]: read words, for the registers regs does not list
static int prv_cmd_rd(const struct pbl_shell *sh, size_t argc, char **argv) {
  unsigned long addr = strtoul(argv[1], nullptr, 16);
  unsigned long count = argc > 2 ? strtoul(argv[2], nullptr, 0) : 1UL;

  if ((addr & 3UL) != 0UL || addr < 0x3fc80000UL || count == 0UL || count > 64UL) {
    pbl_shell_error(sh, "bad address or count");
    return -EINVAL;
  }
  for (unsigned long i = 0UL; i < count; i++) {
    pbl_shell_print(sh, "%08lx = %08" PRIx32, addr + 4UL * i, pbl_readl(addr + 4UL * i));
  }
  return 0;
}

static const struct pbl_shell_cmd sub_btc[] = {
  PBL_SHELL_CMD_ARG(status, nullptr, "State, controller, expected address", prv_cmd_status, 1, 0),
  PBL_SHELL_CMD_ARG(start, nullptr, "Bring the controller up (watchdog armed)", prv_cmd_start, 1,
                    0),
  PBL_SHELL_CMD_ARG(confirm, nullptr, "The console works: stop the watchdog", prv_cmd_confirm, 1,
                    0),
  PBL_SHELL_CMD_ARG(arm, nullptr, "Let NimBLE use the controller from the next boot", prv_cmd_arm,
                    1, 0),
  PBL_SHELL_CMD_ARG(clear, nullptr, "Forget a failed attempt and the arming", prv_cmd_clear, 1, 0),
  PBL_SHELL_CMD_ARG(hci, nullptr, "Send an H4 packet <hex bytes...>, print the answer", prv_cmd_hci,
                    2, 31),
  PBL_SHELL_CMD_ARG(regs, nullptr, "Dump clock, power and USB registers", prv_cmd_regs, 1, 0),
  PBL_SHELL_CMD_ARG(rd, nullptr, "Read words <hex address> [count]", prv_cmd_rd, 2, 1),
  PBL_SHELL_SUBCMD_SET_END,
};

PBL_SHELL_CMD_REGISTER(btc, sub_btc, "Espressif Bluetooth controller", nullptr);
#endif
