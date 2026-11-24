/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/uart.h>
#include <pbl/kernel/irq.h>

/**
 * @defgroup drivers_uart_esp32c3_usb_serial_jtag ESP32-C3 USB-Serial-JTAG
 * @ingroup drivers_uart
 * @brief @ref drivers_uart driver for the serial port of the USB-Serial-JTAG controller of the
 * ESP32-C3, a USB CDC-ACM port the chip provides on its USB pins.
 *
 * The port has no line settings: any baud rate is accepted. Bytes go out in packets of up to 64,
 * when the FIFO is full or on a flush. With no host reading, a byte waits for room at most
 * @c CONFIG_UART_ESP32C3_USB_SERIAL_JTAG_TX_TIMEOUT_MS, and the bytes after it are dropped at
 * once until there is room again: the port never blocks the system. While reception is disabled,
 * the received bytes stay in the controller.
 * @{
 */

/** @brief ESP32-C3 USB-Serial-JTAG serial port. */
struct pbl_uart_esp32c3_usj {
  /** Serial port. */
  struct pbl_uart uart;
  /** Registers. */
  uintptr_t base;
  /** Interrupt line. */
  pbl_irq_t irq;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_uart_ops pbl_uart_esp32c3_usj_ops;
/** @endcond */

/**
 * @brief Define the serial port of an ESP32-C3 USB-Serial-JTAG controller.
 *
 * @param sym Symbol of the port.
 * @param _name Name.
 * @param _base Registers.
 * @param _irq Interrupt line.
 */
#define PBL_UART_ESP32C3_USJ_DEFINE(sym, _name, _base, _irq)                       \
  PBL_UART_STATE_DEFINE(sym);                                                      \
  PBL_DEVICE_INSTANCE(sym, struct pbl_uart_esp32c3_usj, struct pbl_uart, uart){    \
    .uart = PBL_UART_INIT(sym, _name, nullptr, &pbl_uart_esp32c3_usj_ops, 115200), \
    .base = (_base),                                                               \
    .irq = (_irq),                                                                 \
  };                                                                               \
  PBL_IRQ_CONNECT_NUM(_irq, CONFIG_UART_ESP32C3_USB_SERIAL_JTAG_IRQ_PRIO,          \
                      pbl_uart_esp32c3_usj_irq_handler, &sym##_inst, 0)

/**
 * @brief Interrupt handler, connected by PBL_UART_ESP32C3_USJ_DEFINE().
 *
 * @param usj Port.
 */
void pbl_uart_esp32c3_usj_irq_handler(const struct pbl_uart_esp32c3_usj *usj);

/** @} */
