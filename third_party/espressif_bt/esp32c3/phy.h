/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// The PHY library's interface, from ESP-IDF 5.5.5 components/esp_phy/include/esp_phy_init.h and
// esp_private/phy.h, and the bring-up phy.c derives from ESP-IDF.

#pragma once

#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t params[128]; /*!< opaque PHY initialization parameters */
} esp_phy_init_data_t;

typedef enum {
  PHY_MODEM_WIFI = 1,
  PHY_MODEM_BT = 2,
  PHY_MODEM_IEEE802154 = 4,
} esp_phy_modem_t;

typedef struct {
  uint8_t version[4];   /*!< PHY version */
  uint8_t mac[6];       /*!< The MAC address of the station */
  uint8_t opaque[1894]; /*!< calibration data */
} esp_phy_calibration_data_t;

typedef enum {
  PHY_RF_CAL_PARTIAL = 0x00000000,
  PHY_RF_CAL_NONE = 0x00000001,
  PHY_RF_CAL_FULL = 0x00000002,
} esp_phy_calibration_mode_t;

// libphy.a
int register_chipv7_phy(const esp_phy_init_data_t *init_data, esp_phy_calibration_data_t *cal_data,
                        esp_phy_calibration_mode_t cal_mode);
void phy_bbpll_en_usb(bool en);
char *get_phy_version_str(void);

// phy.c
void esp_wifi_bt_power_domain_on(void);
void periph_bt_module_enable(void);
void periph_bt_module_reset(void);
void esp_phy_enable(esp_phy_modem_t modem);
void esp_phy_disable(esp_phy_modem_t modem);
