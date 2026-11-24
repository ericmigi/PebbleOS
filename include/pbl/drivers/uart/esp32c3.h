/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/clk.h>
#include <pbl/drivers/uart.h>
#include <pbl/kernel/irq.h>

/**
 * @defgroup drivers_uart_esp32c3 ESP32-C3
 * @ingroup drivers_uart
 * @brief @ref drivers_uart driver for the UART controllers of the ESP32-C3.
 *
 * Polled transmission, and reception a byte at a time from the receive FIFO interrupt. Only 8N1
 * without flow control.
 * @{
 */

/** @brief ESP32-C3 serial port. */
struct pbl_uart_esp32c3 {
  /** Serial port. */
  struct pbl_uart uart;
  /** Registers. */
  uintptr_t base;
  /** Interrupt line. */
  pbl_irq_t irq;
  /** Function clock, the APB clock. */
  struct pbl_clk clk;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_uart_ops pbl_uart_esp32c3_ops;
/** @endcond */

/**
 * @brief Define a serial port on an ESP32-C3 UART.
 *
 * @param sym Symbol of the port.
 * @param _name Name.
 * @param _base Registers.
 * @param _irq Interrupt line.
 * @param _clk Function clock, from PBL_CLK().
 * @param _baudrate Initial baud rate, in bits per second.
 */
#define PBL_UART_ESP32C3_DEFINE(sym, _name, _base, _irq, _clk, _baudrate)               \
  PBL_UART_STATE_DEFINE(sym);                                                           \
  PBL_DEVICE_INSTANCE(sym, struct pbl_uart_esp32c3, struct pbl_uart, uart){             \
    .uart = PBL_UART_INIT(sym, _name, nullptr, &pbl_uart_esp32c3_ops, _baudrate),       \
    .base = (_base),                                                                    \
    .irq = (_irq),                                                                      \
    .clk = _clk,                                                                        \
  };                                                                                    \
  PBL_IRQ_CONNECT_NUM(_irq, CONFIG_UART_ESP32C3_IRQ_PRIO, pbl_uart_esp32c3_irq_handler, \
                      &sym##_inst, 0)

/**
 * @brief Interrupt handler, connected by PBL_UART_ESP32C3_DEFINE().
 *
 * @param uart Port.
 */
void pbl_uart_esp32c3_irq_handler(const struct pbl_uart_esp32c3 *uart);

/** @} */
