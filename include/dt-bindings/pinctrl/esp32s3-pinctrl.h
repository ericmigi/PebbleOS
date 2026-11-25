/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef _DT_BINDINGS_PINCTRL_ESP32S3_PINCTRL_H
#define _DT_BINDINGS_PINCTRL_ESP32S3_PINCTRL_H

/*
 * ESP32-S3 pinmux cells (compatible with the shared esp,esp32c3-pinctrl driver):
 * a pad (0..48) and the GPIO matrix input/output signal indices, or ESP32S3_NOSIG.
 */
#define ESP32S3_NOSIG    0x1ff
#define ESP32S3_SIG_GPIO 0x100

/* GPSPI2 signals in the ESP32-S3 GPIO matrix (soc/gpio_sig_map.h) */
#define ESP32S3_SIG_FSPICLK 101
#define ESP32S3_SIG_FSPIQ   102
#define ESP32S3_SIG_FSPID   103

/* I2C0 signals in the ESP32-S3 GPIO matrix */
#define ESP32S3_SIG_I2CEXT0_SCL 89
#define ESP32S3_SIG_I2CEXT0_SDA 90

#define ESP32S3_PINMUX(pad, in, out) ((pad) | ((in) << 6) | ((out) << 15))

#endif
