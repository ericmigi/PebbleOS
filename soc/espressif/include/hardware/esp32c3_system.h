/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

#define ESP32C3_SYSTEM_CPU_INTR_FROM_CPU_OFFSET(n) (0x0028 + 4U * (n))

#define ESP32C3_SYSTEM_CPU_PER_CONF_OFFSET 0x0008
#define ESP32C3_SYSTEM_SYSCLK_CONF_OFFSET  0x0058

// The PLL's 480 MHz divided by 6 or 3
#define SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK PBL_GENMASK(1, 0)
#define SYSTEM_CPUPERIOD_SEL_80M               0U
#define SYSTEM_CPUPERIOD_SEL_160M              1U

#define SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK PBL_GENMASK(9, 0)
#define SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK PBL_GENMASK(11, 10)
#define SYSTEM_SOC_CLK_SEL_XTAL             0U
#define SYSTEM_SOC_CLK_SEL_PLL              1U
