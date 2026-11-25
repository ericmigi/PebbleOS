/*
 * SPDX-FileCopyrightText: 2016-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// The registers the controller and PHY bring-up touch, from ESP-IDF 5.5.5
// components/soc/esp32c3/register/soc/{rtc_cntl,syscon}_reg.h and
// components/hal/esp32c3/include/hal/sar_ctrl_ll.h.

#pragma once

#include <pbl/util/bits.h>

#define ESP32C3_XTAL_HZ 40000000U

#define DR_REG_RTCCNTL_BASE 0x60008000U
#define DR_REG_SYSCON_BASE  0x60026000U

#define RTC_CNTL_DIG_PWC_REG    (DR_REG_RTCCNTL_BASE + 0x0088U)
#define RTC_CNTL_WIFI_FORCE_PD  PBL_BIT(17)
#define RTC_CNTL_BT_FORCE_PD    PBL_BIT(11)
#define RTC_CNTL_DIG_ISO_REG    (DR_REG_RTCCNTL_BASE + 0x008CU)
#define RTC_CNTL_WIFI_FORCE_ISO PBL_BIT(28)
#define RTC_CNTL_BT_FORCE_ISO   PBL_BIT(22)

#define SYSTEM_WIFI_CLK_EN_REG (DR_REG_SYSCON_BASE + 0x014U)
// Clock bits used by both Wi-Fi and Bluetooth: 0-3, 7-10, 19-23
#define SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M 0x78078FU
// Clock bit of the PHY calibration, 22
#define SYSTEM_WIFI_CLK_PHY_EN_M 0x400000U

#define SYSTEM_WIFI_RST_EN_REG  (DR_REG_SYSCON_BASE + 0x018U)
#define SYSTEM_WIFIBB_RST       PBL_BIT(0)
#define SYSTEM_FE_RST           PBL_BIT(1)
#define SYSTEM_WIFIMAC_RST      PBL_BIT(2)
#define SYSTEM_BTBB_RST         PBL_BIT(3)
#define SYSTEM_BTMAC_RST        PBL_BIT(4)
#define SYSTEM_RW_BTMAC_RST     PBL_BIT(9)
#define SYSTEM_RW_BTLP_RST      PBL_BIT(10)
#define SYSTEM_RW_BTMAC_REG_RST PBL_BIT(11)
#define SYSTEM_RW_BTLP_REG_RST  PBL_BIT(12)
#define SYSTEM_BTBB_REG_RST     PBL_BIT(13)

#define MODEM_RESET_FIELD_WHEN_PU                                                                \
  (SYSTEM_WIFIBB_RST | SYSTEM_FE_RST | SYSTEM_WIFIMAC_RST | SYSTEM_BTBB_RST | SYSTEM_BTMAC_RST | \
   SYSTEM_RW_BTMAC_RST | SYSTEM_RW_BTMAC_REG_RST | SYSTEM_BTBB_REG_RST)

// periph_ll_get_rst_en_mask(PERIPH_BT_MODULE)
#define BT_MODULE_RST_MASK                                                            \
  (SYSTEM_BTBB_RST | SYSTEM_BTBB_REG_RST | SYSTEM_RW_BTMAC_RST | SYSTEM_RW_BTLP_RST | \
   SYSTEM_RW_BTMAC_REG_RST | SYSTEM_RW_BTLP_REG_RST)

// The SAR ADC power the PHY's power detector forces, apart from the ADC's own
#define PWDET_CONF_REG        0x6000E060U
#define PWDET_SAR_POWER_FORCE PBL_BIT(7)
#define PWDET_SAR_POWER_CNTL  PBL_BIT(6)
