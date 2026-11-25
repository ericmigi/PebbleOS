/*
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 */

// The ESP-IDF 5.5.5 configuration the code derived from ESP-IDF here is read with, so it keeps its
// CONFIG_ names. The values are those of the Arduino ESP32 core's esp32c3 sdkconfig (ESP-IDF
// 5.5.5, controller only), unless noted.

#pragma once

#define CONFIG_IDF_TARGET_ESP32C3   1
#define CONFIG_ESP32C3_REV_MIN_FULL 3

// Controller (esp_bt.h, bt.c)
#define CONFIG_BT_CTRL_MODE_EFF                 1
#define CONFIG_BT_CTRL_PINNED_TO_CORE           0
#define CONFIG_BT_CTRL_BLE_MAX_ACT_EFF          CONFIG_ESP32C3_BT_BLE_MAX_ACT
#define CONFIG_BT_CTRL_SLEEP_MODE_EFF           0
#define CONFIG_BT_CTRL_SLEEP_CLOCK_EFF          0
#define CONFIG_BT_CTRL_BLE_STATIC_ACL_TX_BUF_NB 0
#define CONFIG_BT_CTRL_HW_CCA_EFF               0
#define CONFIG_BT_CTRL_ADV_DUP_FILT_MAX         30
#define CONFIG_BT_CTRL_CE_LENGTH_TYPE_EFF       0
#define CONFIG_BT_CTRL_HCI_TL_EFF               1
#define CONFIG_BT_CTRL_TX_ANTENNA_INDEX_EFF     0
#define CONFIG_BT_CTRL_RX_ANTENNA_INDEX_EFF     0
#define CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_EFF   11
#define CONFIG_BT_CTRL_SCAN_DUPL_TYPE           0
#define CONFIG_BT_CTRL_SCAN_DUPL_CACHE_SIZE     100
// The Arduino core turns BLE mesh duplicate filtering on; NimBLE does no mesh here
#define CONFIG_BT_CTRL_BLE_MESH_SCAN_DUPL_EN          0
#define CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD 0
#define CONFIG_BT_CTRL_COEX_PHY_CODED_TX_RX_TLIM_EFF  0
#define CONFIG_BT_CTRL_HW_CCA_VAL                     75
#define CONFIG_BT_BLE_CCA_MODE                        0
#define CONFIG_BT_CTRL_CHAN_ASS_EN                    1
#define CONFIG_BT_CTRL_LE_PING_EN                     1
#define CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY              1
#define CONFIG_BT_CTRL_DTM_ENABLE                     1
#define CONFIG_BT_CTRL_BLE_MASTER                     1
#define CONFIG_BT_CTRL_BLE_SCAN                       1
#define CONFIG_BT_CTRL_BLE_SECURITY_ENABLE            1
#define CONFIG_BT_CTRL_BLE_ADV                        1
#define CONFIG_BT_CTRL_CHECK_CONFIG_EFF               1
// The host is NimBLE, built without the extended advertising of BLE 5.0
#define CONFIG_BT_NIMBLE_ENABLED            1
#define CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT 0

// PHY (phy_init.c, phy_init_data.c)
#define CONFIG_ESP_PHY_MAX_TX_POWER 20
#define CONFIG_ESP_PHY_ENABLE_USB   1
