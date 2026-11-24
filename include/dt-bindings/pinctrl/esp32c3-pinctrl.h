/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef _DT_BINDINGS_PINCTRL_ESP32C3_PINCTRL_H
#define _DT_BINDINGS_PINCTRL_ESP32C3_PINCTRL_H

/*
 * ESP32-C3 pinmux cells: a pad and the GPIO matrix signals it carries, the input signal reading
 * it and the output signal driving it, by their numbers in the matrix, or ESP32C3_NOSIG.
 */
#define ESP32C3_NOSIG 0x1ff

/* The output signal of the pad's own level, as a GPIO */
#define ESP32C3_SIG_GPIO 0x80

#define ESP32C3_PINMUX(pad, in, out) ((pad) | ((in) << 6) | ((out) << 15))

#endif
