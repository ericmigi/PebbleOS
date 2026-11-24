/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/devicetree.h>
#include <pbl/drivers/uart/esp32c3_usb_serial_jtag.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/misc.h>

#include <hardware/esp32c3_usb_serial_jtag.h>

/*
 * The controller is set up by the ROM and stays as it is: only the FIFO, its packet handover and
 * the reception interrupt are touched here, never the USB side, so the host keeps its port and
 * esptool its reset path whatever the firmware does. The chip has one such controller.
 */

#define TX_TIMEOUT_US (CONFIG_UART_ESP32C3_USB_SERIAL_JTAG_TX_TIMEOUT_MS * 1000U)

// No host took the last packet in time: drop the bytes until there is room again
static bool s_stalled;
// Bytes are in the FIFO that no flush handed over yet
static bool s_unflushed;

static const struct pbl_uart_esp32c3_usj *prv_usj(const struct pbl_uart *uart) {
  return PBL_CONTAINER_OF(uart, const struct pbl_uart_esp32c3_usj, uart);
}

static inline uint32_t prv_getreg(const struct pbl_uart_esp32c3_usj *usj, uint32_t offset) {
  return pbl_readl(usj->base + offset);
}

static inline void prv_putreg(const struct pbl_uart_esp32c3_usj *usj, uint32_t offset,
                              uint32_t value) {
  pbl_writel(value, usj->base + offset);
}

static bool prv_tx_room(const struct pbl_uart_esp32c3_usj *usj) {
  return (prv_getreg(usj, ESP32C3_USJ_EP1_CONF_OFFSET) & USJ_EP1_CONF_SERIAL_IN_EP_DATA_FREE) != 0U;
}

// Waits for the host to take what is in the FIFO, unless it did not last time
static bool prv_wait_room(const struct pbl_uart_esp32c3_usj *usj) {
  if (prv_tx_room(usj)) {
    s_stalled = false;
    return true;
  }
  if (!s_stalled && PBL_POLL_BUSY_US(prv_tx_room(usj), 0U, TX_TIMEOUT_US) == 0) {
    return true;
  }
  s_stalled = true;

  return false;
}

static int prv_early_init(const struct pbl_uart *uart) {
  return 0;
}

static int prv_init(const struct pbl_uart *uart) {
  const struct pbl_uart_esp32c3_usj *usj = prv_usj(uart);

  pbl_modifyl(USJ_INT_SERIAL_OUT_RECV_PKT, 0U, usj->base + ESP32C3_USJ_INT_ENA_OFFSET);
  prv_putreg(usj, ESP32C3_USJ_INT_CLR_OFFSET, USJ_INT_SERIAL_OUT_RECV_PKT);
  pbl_irq_enable(usj->irq);

  return 0;
}

static int prv_configure(const struct pbl_uart *uart, const struct pbl_uart_config *cfg) {
  return 0;
}

static void prv_poll_out(const struct pbl_uart *uart, uint8_t c) {
  const struct pbl_uart_esp32c3_usj *usj = prv_usj(uart);

  if (!prv_tx_room(usj)) {
    // A full FIFO goes out by itself; one that is not waits for the handover
    prv_putreg(usj, ESP32C3_USJ_EP1_CONF_OFFSET, USJ_EP1_CONF_WR_DONE);
    if (!prv_wait_room(usj)) {
      return;
    }
  }
  s_stalled = false;
  prv_putreg(usj, ESP32C3_USJ_EP1_OFFSET, c);
  s_unflushed = true;
}

static void prv_flush(const struct pbl_uart *uart) {
  const struct pbl_uart_esp32c3_usj *usj = prv_usj(uart);

  if (!s_unflushed) {
    return;
  }
  s_unflushed = false;
  prv_putreg(usj, ESP32C3_USJ_EP1_CONF_OFFSET, USJ_EP1_CONF_WR_DONE);
  (void)prv_wait_room(usj);
}

static bool prv_rx_avail(const struct pbl_uart_esp32c3_usj *usj) {
  return (prv_getreg(usj, ESP32C3_USJ_EP1_CONF_OFFSET) & USJ_EP1_CONF_SERIAL_OUT_EP_DATA_AVAIL) !=
         0U;
}

static int prv_rx_enable(const struct pbl_uart *uart) {
  const struct pbl_uart_esp32c3_usj *usj = prv_usj(uart);

  pbl_modifyl(0U, USJ_INT_SERIAL_OUT_RECV_PKT, usj->base + ESP32C3_USJ_INT_ENA_OFFSET);
  // The interrupt is raised once per packet: bytes held back while disabled need a kick
  if (prv_rx_avail(usj)) {
    pbl_irq_set_pending(usj->irq);
  }

  return 0;
}

static void prv_rx_disable(const struct pbl_uart *uart) {
  pbl_modifyl(USJ_INT_SERIAL_OUT_RECV_PKT, 0U, prv_usj(uart)->base + ESP32C3_USJ_INT_ENA_OFFSET);
}

void pbl_uart_esp32c3_usj_irq_handler(const struct pbl_uart_esp32c3_usj *usj) {
  // Cleared first: a packet arriving while draining raises it again
  prv_putreg(usj, ESP32C3_USJ_INT_CLR_OFFSET, USJ_INT_SERIAL_OUT_RECV_PKT);

  while (usj->uart.state->rx_enabled && prv_rx_avail(usj)) {
    (void)pbl_uart_receive(&usj->uart, (uint8_t)prv_getreg(usj, ESP32C3_USJ_EP1_OFFSET));
  }
}

const struct pbl_uart_ops pbl_uart_esp32c3_usj_ops = {
  .init = prv_init,
  .early_init = prv_early_init,
  .configure = prv_configure,
  .poll_out = prv_poll_out,
  .flush = prv_flush,
  .rx_enable = prv_rx_enable,
  .rx_disable = prv_rx_disable,
};

#include PBL_DT_INSTANCES(esp_esp32c3_usb_serial_jtag)
