/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/spi/esp32c3.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_spi.h>

#define SYSTEM(reg) (PBL_DT_REG_SYSTEM + ESP32C3_SYSTEM_##reg##_OFFSET)

// The register sync to the controller's clock domain takes a few of its cycles
#define UPDATE_TIMEOUT_US 100U

// Clock divider: a prescaler of 1 to 16 and a counter of 2 to 64
#define PRE_MAX 16U
#define N_MIN   2U
#define N_MAX   64U

static const struct pbl_spi_esp32c3 *prv_spi(const struct pbl_spi_bus *bus) {
  return PBL_CONTAINER_OF(bus, const struct pbl_spi_esp32c3, bus);
}

static inline uint32_t prv_getreg(const struct pbl_spi_esp32c3 *spi, uint32_t offset) {
  return pbl_readl(spi->base + offset);
}

static inline void prv_putreg(const struct pbl_spi_esp32c3 *spi, uint32_t offset, uint32_t value) {
  pbl_writel(value, spi->base + offset);
}

// The block out of reset, a transfer it was running dropped, set up as a master; the device
// settings follow at the next configure
static void prv_reset(const struct pbl_spi_esp32c3 *spi) {
  pbl_irq_lock();
  pbl_modifyl(0U, SYSTEM_PERIP_SPI2, SYSTEM(PERIP_RST_EN0));
  pbl_modifyl(SYSTEM_PERIP_SPI2, 0U, SYSTEM(PERIP_RST_EN0));
  pbl_irq_unlock();

  prv_putreg(spi, ESP32C3_SPI_CLK_GATE_OFFSET,
             SPI_CLK_GATE_CLK_EN | SPI_CLK_GATE_MST_CLK_ACTIVE | SPI_CLK_GATE_MST_CLK_SEL);
  prv_putreg(spi, ESP32C3_SPI_SLAVE_OFFSET, 0U);
  prv_putreg(spi, ESP32C3_SPI_USER1_OFFSET, 0U);
  prv_putreg(spi, ESP32C3_SPI_USER2_OFFSET, 0U);
  prv_putreg(spi, ESP32C3_SPI_DMA_CONF_OFFSET, 0U);
  prv_putreg(spi, ESP32C3_SPI_DMA_INT_ENA_OFFSET, 0U);
  prv_putreg(spi, ESP32C3_SPI_MISC_OFFSET, SPI_MISC_CS_DIS_ALL);
}

static int prv_init(const struct pbl_spi_bus *bus) {
  const struct pbl_spi_esp32c3 *spi = prv_spi(bus);
  int ret;

  ret = pbl_device_init(&spi->clk.ctrl->dev);
  if (ret != 0) {
    return ret;
  }

  pbl_irq_lock();
  pbl_modifyl(0U, SYSTEM_PERIP_SPI2, SYSTEM(PERIP_CLK_EN0));
  pbl_modifyl(0U, SYSTEM_PERIP_SPI2, SYSTEM(PERIP_RST_EN0));
  pbl_modifyl(SYSTEM_PERIP_SPI2, 0U, SYSTEM(PERIP_RST_EN0));
  pbl_irq_unlock();

  // Out of reset the version reads back; where the block is missing, e.g. QEMU, it reads 0
  if ((prv_getreg(spi, ESP32C3_SPI_DATE_OFFSET) & SPI_DATE_MASK) == 0U) {
    return -ENODEV;
  }

  prv_reset(spi);

  // Every chip select inactive, also those no driver sets up, e.g. a card sharing the bus
  for (uint8_t i = 0U; i < bus->num_cs; i++) {
    const struct pbl_gpio *cs = &bus->cs_gpios[i];

    if (!pbl_gpio_is_connected(cs)) {
      continue;
    }
    if (pbl_device_init(&cs->port->dev) != 0) {
      return -ENODEV;
    }
    ret = pbl_gpio_configure(cs, PBL_GPIO_OUTPUT_HIGH);
    if (ret != 0) {
      return ret;
    }
  }

  spi->state->max_hz = 0U;

  return pbl_pinctrl_select_state(&spi->pinctrl, PBL_PINCTRL_STATE_DEFAULT);
}

// The highest rate up to max_hz: the smallest divider pre * n of at least apb / max_hz
static int prv_clock(uint32_t apb_hz, uint32_t max_hz, uint32_t *clock, uint32_t *hz) {
  uint32_t div;
  uint32_t best_pre = 0U;
  uint32_t best_n = 0U;

  if (max_hz >= apb_hz) {
    *clock = SPI_CLOCK_EQU_SYSCLK;
    *hz = apb_hz;
    return 0;
  }

  div = PBL_DIV_ROUND_UP(apb_hz, max_hz);
  for (uint32_t n = N_MAX; n >= N_MIN; n--) {
    uint32_t pre = PBL_DIV_ROUND_UP(div, n);

    if (pre > PRE_MAX) {
      break;
    }
    if (best_n == 0U || pre * n < best_pre * best_n) {
      best_pre = pre;
      best_n = n;
    }
  }

  if (best_n == 0U) {
    return -EINVAL;
  }

  *clock = PBL_FIELD_PREP(SPI_CLOCK_PRE_MASK, best_pre - 1U) |
           PBL_FIELD_PREP(SPI_CLOCK_N_MASK, best_n - 1U) |
           PBL_FIELD_PREP(SPI_CLOCK_H_MASK, best_n / 2U - 1U) |
           PBL_FIELD_PREP(SPI_CLOCK_L_MASK, best_n - 1U);
  *hz = apb_hz / (best_pre * best_n);

  return 0;
}

static int prv_configure(const struct pbl_spi_bus *bus, const struct pbl_spi_dev *dev) {
  const struct pbl_spi_esp32c3 *spi = prv_spi(bus);
  struct pbl_spi_esp32c3_state *state = spi->state;
  uint32_t user = SPI_USER_USR_MOSI | SPI_USER_USR_MISO | SPI_USER_DOUTDIN;
  uint32_t misc = SPI_MISC_CS_DIS_ALL;
  uint32_t ctrl = 0U;
  uint64_t rate_mhz;
  int ret;

  if (dev->max_hz == 0U) {
    return -EINVAL;
  }

  if (dev->max_hz != state->max_hz) {
    ret = pbl_clk_get_rate(&spi->clk, &rate_mhz);
    if (ret != 0) {
      return ret;
    }

    ret = prv_clock((uint32_t)(rate_mhz / 1000U), dev->max_hz, &state->clock, &state->hz);
    if (ret != 0) {
      return ret;
    }
    state->max_hz = dev->max_hz;
  }

  // Modes 1 and 2 shift out on the other edge, 2 and 3 idle high
  if (((dev->mode & PBL_SPI_CPHA) != 0U) != ((dev->mode & PBL_SPI_CPOL) != 0U)) {
    user |= SPI_USER_CK_OUT_EDGE;
  }
  if ((dev->mode & PBL_SPI_CPOL) != 0U) {
    misc |= SPI_MISC_CK_IDLE_EDGE;
  }
  if ((dev->mode & PBL_SPI_LSB_FIRST) != 0U) {
    ctrl |= SPI_CTRL_WR_BIT_ORDER | SPI_CTRL_RD_BIT_ORDER;
  }

  prv_putreg(spi, ESP32C3_SPI_CLOCK_OFFSET, state->clock);
  prv_putreg(spi, ESP32C3_SPI_USER_OFFSET, user);
  prv_putreg(spi, ESP32C3_SPI_MISC_OFFSET, misc);
  prv_putreg(spi, ESP32C3_SPI_CTRL_OFFSET, ctrl);

  return 0;
}

// The controller stays clocked: gating it is not worth the idle level questions yet
static void prv_enable(const struct pbl_spi_bus *bus) {
}

static void prv_disable(const struct pbl_spi_bus *bus) {
}

static int prv_chunk(const struct pbl_spi_esp32c3 *spi, const uint8_t *tx, uint8_t *rx,
                     uint32_t len) {
  uint32_t bits = len * 8U;
  // Twice the time on the wire, and some
  uint32_t timeout_us = 100U + (uint32_t)((2ULL * bits * 1000000U) / spi->state->hz);
  int ret;

  for (uint32_t i = 0U; i < len; i += 4U) {
    uint32_t word = 0U;

    for (uint32_t j = 0U; j < 4U && i + j < len; j++) {
      word |= (uint32_t)((tx != nullptr) ? tx[i + j] : 0U) << (8U * j);
    }
    prv_putreg(spi, ESP32C3_SPI_W_OFFSET(i / 4U), word);
  }

  prv_putreg(spi, ESP32C3_SPI_MS_DLEN_OFFSET, bits - 1U);

  prv_putreg(spi, ESP32C3_SPI_CMD_OFFSET, SPI_CMD_UPDATE);
  ret = PBL_POLL_BUSY_US((prv_getreg(spi, ESP32C3_SPI_CMD_OFFSET) & SPI_CMD_UPDATE) == 0U, 0U,
                         UPDATE_TIMEOUT_US);
  if (ret != 0) {
    return ret;
  }

  prv_putreg(spi, ESP32C3_SPI_DMA_INT_CLR_OFFSET, SPI_INT_TRANS_DONE);
  prv_putreg(spi, ESP32C3_SPI_CMD_OFFSET, SPI_CMD_USR);
  ret = PBL_POLL_BUSY_US(
      (prv_getreg(spi, ESP32C3_SPI_DMA_INT_RAW_OFFSET) & SPI_INT_TRANS_DONE) != 0U, 0U, timeout_us);
  if (ret != 0) {
    return ret;
  }

  if (rx != nullptr) {
    for (uint32_t i = 0U; i < len; i += 4U) {
      uint32_t word = prv_getreg(spi, ESP32C3_SPI_W_OFFSET(i / 4U));

      for (uint32_t j = 0U; j < 4U && i + j < len; j++) {
        rx[i + j] = (uint8_t)(word >> (8U * j));
      }
    }
  }

  return 0;
}

// Polled: the transfer is over, and reported, before this returns
static int prv_start(const struct pbl_spi_bus *bus, const struct pbl_spi_xfer *xfer) {
  const struct pbl_spi_esp32c3 *spi = prv_spi(bus);
  const uint8_t *tx = xfer->tx_buf;
  uint8_t *rx = xfer->rx_buf;

  for (uint32_t pos = 0U; pos < xfer->len; pos += ESP32C3_SPI_BUF_LEN) {
    uint32_t len = PBL_MIN(xfer->len - pos, ESP32C3_SPI_BUF_LEN);
    int ret;

    ret = prv_chunk(spi, (tx != nullptr) ? &tx[pos] : nullptr, (rx != nullptr) ? &rx[pos] : nullptr,
                    len);
    if (ret != 0) {
      // Stopped before the bus is handed on, as it may still be shifting
      prv_reset(spi);
      return ret;
    }
  }

  pbl_spi_bus_complete(bus, 0);

  return 0;
}

const struct pbl_spi_bus_ops pbl_spi_esp32c3_ops = {
  .init = prv_init,
  .configure = prv_configure,
  .enable = prv_enable,
  .disable = prv_disable,
  .start = prv_start,
};

#include PBL_DT_INSTANCES(esp_esp32c3_spi)
