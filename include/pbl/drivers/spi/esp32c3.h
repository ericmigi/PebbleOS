/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/clk.h>
#include <pbl/drivers/pinctrl.h>
#include <pbl/drivers/spi.h>

/**
 * @defgroup drivers_spi_esp32c3 ESP32-C3 SPI
 * @ingroup drivers_spi
 * @brief @ref drivers_spi driver for GPSPI2, the general purpose SPI controller of the ESP32-C3.
 *
 * The controller runs from the APB clock, given as its clock, divided down to the highest rate
 * up to the device's. Transfers are moved through its 64-byte buffer by the CPU, polled, so they
 * complete before the transfer call returns. Chip selects are GPIOs; all of them are driven
 * inactive, high, when the bus comes up, so a device sharing the bus without a driver stays
 * deselected. SCLK, MOSI and MISO are routed through the GPIO matrix by the pin states.
 *
 * @code{.c}
 * PBL_SPI_ESP32C3_DEFINE(s_spi2, "spi2", 0x60024000, PBL_CLK(&s_clk_apb, 0), s_spi2_pins,
 *                        PBL_GPIO(&s_gpio, 21, 0), PBL_GPIO(&s_gpio, 12, 0));
 * @endcode
 * @{
 */

/** @cond INTERNAL_HIDDEN */
struct pbl_spi_esp32c3_state {
  uint32_t max_hz;
  uint32_t mode;
  uint32_t hz;
  uint32_t clock;
};
/** @endcond */

/** @brief ESP32-C3 SPI bus. */
struct pbl_spi_esp32c3 {
  /** SPI bus. */
  struct pbl_spi_bus bus;
  /** Driver runtime state. */
  struct pbl_spi_esp32c3_state *state;
  /** SCLK, MOSI and MISO pins. */
  struct pbl_pinctrl pinctrl;
  /** APB clock. */
  struct pbl_clk clk;
  /** Registers. */
  uintptr_t base;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_spi_bus_ops pbl_spi_esp32c3_ops;
/** @endcond */

/**
 * @brief Define an ESP32-C3 SPI bus.
 *
 * @param sym Symbol of the bus.
 * @param _name Name.
 * @param _base Registers.
 * @param _clk APB clock, from PBL_CLK().
 * @param _pinctrl Pin states.
 * @param ... Chip select GPIOs, from PBL_GPIO().
 */
#define PBL_SPI_ESP32C3_DEFINE(sym, _name, _base, _clk, _pinctrl, ...)                        \
  PBL_SPI_BUS_STATE_DEFINE(sym, __VA_ARGS__);                                                 \
  static struct pbl_spi_esp32c3_state sym##_esp32c3_state;                                    \
  PBL_DEVICE_INSTANCE(sym, struct pbl_spi_esp32c3, struct pbl_spi_bus, bus) {                 \
    .bus = PBL_SPI_BUS_INIT(sym, _name, nullptr, &pbl_spi_esp32c3_ops),                       \
    .state = &sym##_esp32c3_state, .pinctrl = PBL_PINCTRL(_pinctrl, nullptr, 0), .clk = _clk, \
    .base = (_base),                                                                          \
  }

/** @} */
