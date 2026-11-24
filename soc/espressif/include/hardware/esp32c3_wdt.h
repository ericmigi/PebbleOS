/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

// The RTC and timer group watchdogs, and the super watchdog, each behind its write protection

#define ESP32C3_RTC_CNTL_WDTCONFIG0_OFFSET   0x0090
#define ESP32C3_RTC_CNTL_WDTWPROTECT_OFFSET  0x00a8
#define ESP32C3_RTC_CNTL_SWD_CONF_OFFSET     0x00ac
#define ESP32C3_RTC_CNTL_SWD_WPROTECT_OFFSET 0x00b0
#define ESP32C3_RTC_CNTL_OPTIONS0_OFFSET     0x0000

#define ESP32C3_TIMG_WDTCONFIG0_OFFSET  0x0048
#define ESP32C3_TIMG_WDTWPROTECT_OFFSET 0x0064

#define WDT_WKEY         0x50d83aa1U
#define SWD_WKEY         0x8f1d312aU
#define SWD_AUTO_FEED_EN PBL_BIT(31)

#define RTC_CNTL_OPTIONS0_SW_SYS_RST PBL_BIT(31)
