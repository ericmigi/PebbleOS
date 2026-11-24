/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/uart/esp32c3.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>
#include <pbl/util/misc.h>

#include <hardware/esp32c3_uart.h>

// Some function clock cycles
#define REG_UPDATE_TIMEOUT_US 100U

// The divider has 4 fractional bits
#define CLKDIV_FRAC_BITS 4U

static const struct pbl_uart_esp32c3 *prv_esp(const struct pbl_uart *uart) {
  return PBL_CONTAINER_OF(uart, const struct pbl_uart_esp32c3, uart);
}

static inline uint32_t prv_getreg(const struct pbl_uart_esp32c3 *esp, uint32_t offset) {
  return pbl_readl(esp->base + offset);
}

static inline void prv_putreg(const struct pbl_uart_esp32c3 *esp, uint32_t offset, uint32_t value) {
  pbl_writel(value, esp->base + offset);
}

static void prv_flush(const struct pbl_uart *uart) {
  const struct pbl_uart_esp32c3 *esp = prv_esp(uart);

  while (PBL_FIELD_GET(UART_STATUS_TXFIFO_CNT_MASK, prv_getreg(esp, ESP32C3_UART_STATUS_OFFSET)) !=
         0U) {
  }
}

// Reception interrupts once a byte is in, from the first one
static int prv_early_init(const struct pbl_uart *uart) {
  const struct pbl_uart_esp32c3 *esp = prv_esp(uart);
  int ret;

  ret = pbl_device_init(&esp->clk.ctrl->dev);
  if (ret != 0) {
    return ret;
  }

  prv_putreg(esp, ESP32C3_UART_INT_ENA_OFFSET, 0U);
  prv_putreg(esp, ESP32C3_UART_INT_CLR_OFFSET, UART_INT_ALL);
  pbl_modifyl(UART_CONF1_RXFIFO_FULL_THRHD_MASK,
              PBL_FIELD_PREP(UART_CONF1_RXFIFO_FULL_THRHD_MASK, 1U),
              esp->base + ESP32C3_UART_CONF1_OFFSET);

  return 0;
}

static int prv_init(const struct pbl_uart *uart) {
  int ret = prv_early_init(uart);

  if (ret != 0) {
    return ret;
  }

  pbl_irq_enable(prv_esp(uart)->irq);

  return 0;
}

static int prv_configure(const struct pbl_uart *uart, const struct pbl_uart_config *cfg) {
  const struct pbl_uart_esp32c3 *esp = prv_esp(uart);
  uint64_t rate_mhz;
  uint32_t div;
  int ret;

  if (cfg->parity != PBL_UART_PARITY_NONE || cfg->flow_ctrl != PBL_UART_FLOW_CTRL_NONE ||
      cfg->baudrate == 0U) {
    return -ENOTSUP;
  }

  ret = pbl_clk_get_rate(&esp->clk, &rate_mhz);
  if (ret != 0) {
    return ret;
  }

  div = (uint32_t)(((rate_mhz << CLKDIV_FRAC_BITS) / 1000U + cfg->baudrate / 2U) / cfg->baudrate);
  if ((div >> CLKDIV_FRAC_BITS) == 0U ||
      (div >> CLKDIV_FRAC_BITS) > PBL_FIELD_GET(UART_CLKDIV_INT_MASK, UART_CLKDIV_INT_MASK)) {
    return -ENOTSUP;
  }

  prv_flush(uart);
  prv_putreg(
      esp, ESP32C3_UART_CLKDIV_OFFSET,
      PBL_FIELD_PREP(UART_CLKDIV_INT_MASK, div >> CLKDIV_FRAC_BITS) |
          PBL_FIELD_PREP(UART_CLKDIV_FRAG_MASK, div & PBL_GENMASK(CLKDIV_FRAC_BITS - 1U, 0)));

  // QEMU never clears the bit: give up waiting, the setting applies regardless
  pbl_modifyl(0U, UART_ID_REG_UPDATE, esp->base + ESP32C3_UART_ID_OFFSET);
  (void)PBL_POLL_BUSY_US((prv_getreg(esp, ESP32C3_UART_ID_OFFSET) & UART_ID_REG_UPDATE) == 0U, 0U,
                         REG_UPDATE_TIMEOUT_US);

  return 0;
}

static void prv_poll_out(const struct pbl_uart *uart, uint8_t c) {
  const struct pbl_uart_esp32c3 *esp = prv_esp(uart);

  while (PBL_FIELD_GET(UART_STATUS_TXFIFO_CNT_MASK, prv_getreg(esp, ESP32C3_UART_STATUS_OFFSET)) >=
         UART_FIFO_LEN - 1U) {
  }
  prv_putreg(esp, ESP32C3_UART_FIFO_OFFSET, c);
}

static int prv_rx_enable(const struct pbl_uart *uart) {
  pbl_modifyl(0U, UART_INT_RXFIFO_FULL, prv_esp(uart)->base + ESP32C3_UART_INT_ENA_OFFSET);

  return 0;
}

// The interrupt is level-triggered: masked, the bytes stay in the FIFO
static void prv_rx_disable(const struct pbl_uart *uart) {
  pbl_modifyl(UART_INT_RXFIFO_FULL, 0U, prv_esp(uart)->base + ESP32C3_UART_INT_ENA_OFFSET);
}

void pbl_uart_esp32c3_irq_handler(const struct pbl_uart_esp32c3 *esp) {
  // Reading an empty FIFO returns garbage: go by the count
  while (esp->uart.state->rx_enabled &&
         PBL_FIELD_GET(UART_STATUS_RXFIFO_CNT_MASK, prv_getreg(esp, ESP32C3_UART_STATUS_OFFSET)) !=
             0U) {
    (void)pbl_uart_receive(&esp->uart, (uint8_t)prv_getreg(esp, ESP32C3_UART_FIFO_OFFSET));
  }

  prv_putreg(esp, ESP32C3_UART_INT_CLR_OFFSET, UART_INT_RXFIFO_FULL | UART_INT_RXFIFO_OVF);
}

const struct pbl_uart_ops pbl_uart_esp32c3_ops = {
  .init = prv_init,
  .early_init = prv_early_init,
  .configure = prv_configure,
  .poll_out = prv_poll_out,
  .flush = prv_flush,
  .rx_enable = prv_rx_enable,
  .rx_disable = prv_rx_disable,
};

#include PBL_DT_INSTANCES(esp_esp32c3_uart)
