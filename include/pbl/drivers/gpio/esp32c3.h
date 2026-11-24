/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/intc.h>
#include <pbl/kernel/irq.h>

/**
 * @defgroup drivers_gpio_esp32c3 ESP32-C3
 * @ingroup drivers_gpio
 * @brief @ref drivers_gpio driver for the GPIO matrix of the ESP32-C3.
 *
 * One port of 22 pins. A pin configured as a GPIO is driven from the matrix's own output
 * register, its pad switched to the matrix and its input enabled, so an output reads back; the
 * pulls are those of the pad. An output is driven to its initial level before it is enabled.
 * GPIO18 and GPIO19 are the USB-Serial-JTAG's and are refused, -EBUSY.
 *
 * The port is also the interrupt controller of its pins, on edges and levels, all on the one
 * GPIO interrupt, of priority @c CONFIG_GPIO_ESP32C3_IRQ_PRIO.
 * @{
 */

/** @brief ESP32-C3 GPIO port. */
struct pbl_gpio_esp32c3 {
  /** GPIO port. */
  struct pbl_gpio_port port;
  /** GPIO matrix registers. */
  uintptr_t base;
  /** Interrupt line. */
  pbl_irq_t irq;
  /** IO_MUX registers. */
  uintptr_t io_mux;
};

/** @cond INTERNAL_HIDDEN */
struct pbl_gpio_esp32c3_intc {
  struct pbl_intc intc;
  const struct pbl_gpio_esp32c3 *port;
};

extern const struct pbl_gpio_port_ops pbl_gpio_esp32c3_ops;
extern const struct pbl_intc_ops pbl_gpio_esp32c3_intc_ops;
int pbl_gpio_esp32c3_init(const struct pbl_device *dev);
void pbl_gpio_esp32c3_irq_handler(const struct pbl_gpio_esp32c3 *gpio);
/** @endcond */

/**
 * @brief Define the ESP32-C3 GPIO port and its interrupt controller, sym_intc.
 *
 * @param sym Symbol of the port.
 * @param _name Name.
 * @param _base GPIO matrix registers.
 * @param _irq Interrupt line.
 * @param _num_pins Number of pins.
 * @param _io_mux IO_MUX registers.
 */
#define PBL_GPIO_ESP32C3_DEFINE(sym, _name, _base, _irq, _num_pins, _io_mux)                      \
  extern const struct pbl_intc sym##_intc;                                                        \
  PBL_DEVICE_STATE_DEFINE(sym);                                                                   \
  PBL_INTC_STATE_DEFINE(sym##_intc, _num_pins);                                                   \
  PBL_DEVICE_INSTANCE(sym, struct pbl_gpio_esp32c3, struct pbl_gpio_port, port){                  \
    .port = PBL_GPIO_PORT_INIT(sym, _name, pbl_gpio_esp32c3_init, nullptr, &pbl_gpio_esp32c3_ops, \
                               _num_pins, &sym##_intc),                                           \
    .base = (_base),                                                                              \
    .irq = (_irq),                                                                                \
    .io_mux = (_io_mux),                                                                          \
  };                                                                                              \
  PBL_DEVICE_INSTANCE(sym##_intc, struct pbl_gpio_esp32c3_intc, struct pbl_intc, intc){           \
    .intc = PBL_INTC_INIT(sym##_intc, _name "_intc", nullptr, &sym##_inst.port.dev,               \
                          &pbl_gpio_esp32c3_intc_ops),                                            \
    .port = &sym##_inst,                                                                          \
  };                                                                                              \
  PBL_IRQ_CONNECT_NUM(_irq, CONFIG_GPIO_ESP32C3_IRQ_PRIO, pbl_gpio_esp32c3_irq_handler,           \
                      &sym##_inst, 0)

/** @} */
