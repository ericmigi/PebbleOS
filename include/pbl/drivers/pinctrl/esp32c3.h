/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/pinctrl.h>

#include <dt-bindings/pinctrl/esp32c3-pinctrl.h>

/**
 * @defgroup drivers_pinctrl_esp32c3 ESP32-C3 pin controller
 * @ingroup drivers_pinctrl
 * @brief @ref drivers_pinctrl controller of the ESP32-C3 pads, routing the peripheral signals
 * through the GPIO matrix.
 *
 * A pin is a pinmux cell, ESP32C3_PINMUX(pad, in, out) from
 * <dt-bindings/pinctrl/esp32c3-pinctrl.h>, or'ed with its pad options. The pad is switched to the
 * matrix; an input signal enables its input and reads it, an output signal drives it, with the
 * peripheral's own output enable. A pin without options gets no pull and push-pull, and keeps its
 * drive strength. The USB pins, GPIO18 and GPIO19, are the USB-Serial-JTAG's and are refused.
 *
 * @code{.c}
 * PBL_PINCTRL_STATE_DEFINE(s_spi_default, ESP32C3_PINCTRL,
 *                          PBL_PINCTRL_ESP32C3_PINMUX(ESP32C3_PINMUX(8, ESP32C3_NOSIG, 63)),
 *                          PBL_PINCTRL_ESP32C3_PINMUX(ESP32C3_PINMUX(7, 64, ESP32C3_NOSIG)) |
 *                              PBL_PINCTRL_ESP32C3_PULL_UP);
 * @endcode
 * @{
 */

/** @cond INTERNAL_HIDDEN */
#define PBL_PINCTRL_ESP32C3_PAD_MSK   0x0000003FU
#define PBL_PINCTRL_ESP32C3_IN_MSK    0x00007FC0U
#define PBL_PINCTRL_ESP32C3_OUT_MSK   0x00FF8000U
#define PBL_PINCTRL_ESP32C3_DRIVE_SET (1U << 27)
#define PBL_PINCTRL_ESP32C3_DRIVE_MSK 0x30000000U
/** @endcond */

/** @brief A pinmux cell, from ESP32C3_PINMUX(). */
#define PBL_PINCTRL_ESP32C3_PINMUX(cell) ((uint32_t)(cell))

/**
 * @name Pad options
 * @{
 */
/** Pull-up. */
#define PBL_PINCTRL_ESP32C3_PULL_UP (1U << 24)
/** Pull-down. */
#define PBL_PINCTRL_ESP32C3_PULL_DOWN (1U << 25)
/** Open-drain output. */
#define PBL_PINCTRL_ESP32C3_OPEN_DRAIN (1U << 26)
/** Drive strength, 5, 10, 20 or 40 mA. */
#define PBL_PINCTRL_ESP32C3_DRIVE(ma) \
  ((((ma) >= 40   ? 3U                \
     : (ma) >= 20 ? 2U                \
     : (ma) >= 10 ? 1U                \
                  : 0U)               \
    << 28) |                          \
   PBL_PINCTRL_ESP32C3_DRIVE_SET)
/** @} */

/** @brief ESP32-C3 pin controller. */
struct pbl_pinctrl_esp32c3 {
  /** Pin controller. */
  struct pbl_pinctrl_controller ctrl;
  /** IO_MUX registers. */
  uintptr_t base;
  /** GPIO matrix registers. */
  uintptr_t matrix;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_pinctrl_controller pbl_pinctrl_esp32c3;
extern const struct pbl_pinctrl_controller_ops pbl_pinctrl_esp32c3_ops;
/** @endcond */

/** The pin controller. */
#define ESP32C3_PINCTRL (&pbl_pinctrl_esp32c3)

/**
 * @brief Define the ESP32-C3 pin controller.
 *
 * @param sym Symbol of the controller.
 * @param _name Name.
 * @param _base IO_MUX registers.
 * @param _matrix GPIO matrix registers.
 */
#define PBL_PINCTRL_ESP32C3_DEFINE(sym, _name, _base, _matrix)                                \
  PBL_DEVICE_STATE_DEFINE(sym);                                                               \
  PBL_DEVICE_INSTANCE(sym, struct pbl_pinctrl_esp32c3, struct pbl_pinctrl_controller, ctrl) { \
    .ctrl = PBL_PINCTRL_CONTROLLER_INIT(sym, _name, nullptr, &pbl_pinctrl_esp32c3_ops, 22),   \
    .base = (_base), .matrix = (_matrix),                                                     \
  }

/** @} */
