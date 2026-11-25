/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

// GPIO matrix, the pins' levels, output enables, interrupts and signal routing

#ifdef CONFIG_SOC_ESP32S3
#define ESP32C3_GPIO_NUM_PINS 49U
// ESP32-S3 USB-Serial-JTAG D- and D+ are GPIO19 and GPIO20
#define ESP32C3_GPIO_IS_USB(pin) ((pin) == 19U || (pin) == 20U)
#else
#define ESP32C3_GPIO_NUM_PINS    22U
// The USB-Serial-JTAG's D- and D+: reconfiguring them drops the console and esptool's reset
#define ESP32C3_GPIO_IS_USB(pin) ((pin) == 18U || (pin) == 19U)
#endif

#define ESP32C3_GPIO_OUT_W1TS_OFFSET     0x0008
#define ESP32C3_GPIO_OUT_W1TC_OFFSET     0x000c
#define ESP32C3_GPIO_OUT1_W1TS_OFFSET    0x0014
#define ESP32C3_GPIO_OUT1_W1TC_OFFSET    0x0018
#define ESP32C3_GPIO_ENABLE_W1TS_OFFSET  0x0024
#define ESP32C3_GPIO_ENABLE_W1TC_OFFSET  0x0028
#define ESP32C3_GPIO_ENABLE1_W1TS_OFFSET 0x0030
#define ESP32C3_GPIO_ENABLE1_W1TC_OFFSET 0x0034
#define ESP32C3_GPIO_IN_OFFSET           0x003c
#define ESP32C3_GPIO_IN1_OFFSET          0x0040
#define ESP32C3_GPIO_STATUS_W1TC_OFFSET  0x004c
#define ESP32C3_GPIO_STATUS1_W1TC_OFFSET 0x0058
// The status of the pins whose interrupt is enabled
#define ESP32C3_GPIO_PCPU_INT_OFFSET            0x005c
#define ESP32C3_GPIO_PCPU_INT1_OFFSET           0x0068
#define ESP32C3_GPIO_PIN_OFFSET(n)              (0x0074 + 4U * (n))
#define ESP32C3_GPIO_FUNC_IN_SEL_CFG_OFFSET(s)  (0x0154 + 4U * (s))
#define ESP32C3_GPIO_FUNC_OUT_SEL_CFG_OFFSET(n) (0x0554 + 4U * (n))

#define GPIO_PIN_PAD_DRIVER    PBL_BIT(2)
#define GPIO_PIN_INT_TYPE_MASK PBL_GENMASK(9, 7)
#define GPIO_PIN_WAKEUP_ENABLE PBL_BIT(10)
// Bit 0 of the field is the CPU's interrupt, bit 1 its NMI
#define GPIO_PIN_INT_ENA_MASK PBL_GENMASK(17, 13)
#define GPIO_PIN_INT_ENA_CPU  1U

#define GPIO_INT_TYPE_RISING  1U
#define GPIO_INT_TYPE_FALLING 2U
#define GPIO_INT_TYPE_ANY     3U
#define GPIO_INT_TYPE_LOW     4U
#define GPIO_INT_TYPE_HIGH    5U

// An input signal reads a pin through the matrix when SEL is set
#ifdef CONFIG_SOC_ESP32S3
#define GPIO_FUNC_IN_SEL_MASK PBL_GENMASK(5, 0)
#define GPIO_FUNC_IN_INV      PBL_BIT(6)
#define GPIO_FUNC_IN_SEL      PBL_BIT(7)

// Output signal 256 is the pin's own level in OUT; OEN_SEL takes the enable from ENABLE
#define GPIO_FUNC_OUT_SEL_MASK PBL_GENMASK(8, 0)
#define GPIO_FUNC_OUT_INV      PBL_BIT(9)
#define GPIO_FUNC_OUT_OEN_SEL  PBL_BIT(10)
#define GPIO_FUNC_OUT_GPIO     0x100U
#else
#define GPIO_FUNC_IN_SEL_MASK  PBL_GENMASK(4, 0)
#define GPIO_FUNC_IN_INV       PBL_BIT(5)
#define GPIO_FUNC_IN_SEL       PBL_BIT(6)

// Output signal 128 is the pin's own level in OUT; OEN_SEL takes the enable from ENABLE
#define GPIO_FUNC_OUT_SEL_MASK PBL_GENMASK(7, 0)
#define GPIO_FUNC_OUT_INV      PBL_BIT(8)
#define GPIO_FUNC_OUT_OEN_SEL  PBL_BIT(9)
#define GPIO_FUNC_OUT_GPIO     0x80U
#endif

// IO_MUX, the pads: function, pulls, input enable and drive strength

#define ESP32C3_IO_MUX_GPIO_OFFSET(n) (0x0004 + 4U * (n))

#define IO_MUX_FUN_PD       PBL_BIT(7)
#define IO_MUX_FUN_PU       PBL_BIT(8)
#define IO_MUX_FUN_IE       PBL_BIT(9)
#define IO_MUX_FUN_DRV_MASK PBL_GENMASK(11, 10)
#define IO_MUX_MCU_SEL_MASK PBL_GENMASK(14, 12)
#define IO_MUX_FILTER_EN    PBL_BIT(15)

// Function 1 of every pad is the GPIO matrix
#define IO_MUX_FUNC_GPIO 1U

// RTC_CNTL: the pads' hold, which freezes them across a reset or sleep
#define ESP32C3_RTC_CNTL_DIG_PAD_HOLD_OFFSET 0x00d4
