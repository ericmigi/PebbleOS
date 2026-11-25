/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// The controller's configuration and VHCI interface, from ESP-IDF 5.5.5
// components/bt/include/esp32c3/include/esp_bt.h, cut down to what bt.c uses. The structures
// must match the library: take this file and the library from the same ESP-IDF release.

#pragma once

#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>

#define ESP_BT_CTRL_CONFIG_MAGIC_VAL 0x5A5AA5A5
#define ESP_BT_CTRL_CONFIG_VERSION   0x02509280

#define ESP_BT_MODE_BLE 0x01

#define ESP_BT_SLEEP_MODE_1          1
#define ESP_BT_SLEEP_CLOCK_MAIN_XTAL 1

// esp_task.h: the controller task's FreeRTOS priority and stack, which bt.c maps
#define ESP_TASK_BT_CONTROLLER_PRIO  (25 - 2)
#define ESP_TASK_BT_CONTROLLER_STACK 3584

#define BT_CTRL_BLE_MAX_ACT_LIMIT 10
#define SLAVE_CE_LEN_MIN_DEFAULT  5

#define SCAN_DUPLICATE_TYPE_VALUE        CONFIG_BT_CTRL_SCAN_DUPL_TYPE
#define NORMAL_SCAN_DUPLICATE_CACHE_SIZE CONFIG_BT_CTRL_SCAN_DUPL_CACHE_SIZE

#define SCAN_DUPLICATE_MODE_NORMAL_ADV_ONLY     0
#define SCAN_DUPLICATE_MODE_NORMAL_ADV_MESH_ADV 1
#if CONFIG_BT_CTRL_BLE_MESH_SCAN_DUPL_EN
#error "not taken"
#else
#define SCAN_DUPLICATE_MODE            SCAN_DUPLICATE_MODE_NORMAL_ADV_ONLY
#define MESH_DUPLICATE_SCAN_CACHE_SIZE 0
#endif

#define DUPL_SCAN_CACHE_REFRESH_PERIOD     CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD
#define BT_CTRL_SCAN_BACKOFF_UPPERLIMITMAX 0

#define BT_CTRL_AGC_RECORRECT_EN    0
#define BT_CTRL_AGC_RECORRECT_NEW   0
#define BT_CTRL_CODED_AGC_RECORRECT 0

#define BT_CTRL_50_FEATURE_SUPPORT      (CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT)
#define BT_BLE_CCA_MODE                 (CONFIG_BT_BLE_CCA_MODE)
#define BT_BLE_ADV_DATA_LENGTH_ZERO_AUX (0)
#define BT_CTRL_CHAN_ASS_EN             (CONFIG_BT_CTRL_CHAN_ASS_EN)
#define BT_CTRL_LE_PING_EN              (CONFIG_BT_CTRL_LE_PING_EN)

#define AGC_RECORRECT_EN                                                  \
  ((BT_CTRL_AGC_RECORRECT_EN << 0) | (BT_CTRL_CODED_AGC_RECORRECT << 1) | \
   (BT_CTRL_AGC_RECORRECT_NEW << 2))

#define CFG_MASK_BIT_SCAN_DUPLICATE_OPTION (1 << 0)
#define CFG_MASK                           CFG_MASK_BIT_SCAN_DUPLICATE_OPTION

#define BLE_HW_TARGET_CODE_CHIP_ECO0 (0x01010000)

#define BT_CTRL_BLE_LLCP_DISC_FLAG 0

#define BT_CTRL_RUN_IN_FLASH_ONLY                         CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY
#define BT_CTRL_DTM_ENABLE                                CONFIG_BT_CTRL_DTM_ENABLE
#define BT_CTRL_BLE_MASTER                                CONFIG_BT_CTRL_BLE_MASTER
#define BT_CTRL_BLE_TEST                                  (0)
#define BLE_SECURITY_ENABLE                               (CONFIG_BT_CTRL_BLE_SECURITY_ENABLE)
#define BT_CTRL_BLE_SCAN                                  CONFIG_BT_CTRL_BLE_SCAN
#define BT_CTRL_BLE_ADV                                   CONFIG_BT_CTRL_BLE_ADV
#define BLE_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS_ENABLED 0

#define BT_CONTROLLER_INIT_CONFIG_DEFAULT()                                          \
  {                                                                                  \
    .magic = ESP_BT_CTRL_CONFIG_MAGIC_VAL,                                           \
    .version = ESP_BT_CTRL_CONFIG_VERSION,                                           \
    .controller_task_stack_size = ESP_TASK_BT_CONTROLLER_STACK,                      \
    .controller_task_prio = ESP_TASK_BT_CONTROLLER_PRIO,                             \
    .controller_task_run_cpu = CONFIG_BT_CTRL_PINNED_TO_CORE,                        \
    .bluetooth_mode = CONFIG_BT_CTRL_MODE_EFF,                                       \
    .ble_max_act = CONFIG_BT_CTRL_BLE_MAX_ACT_EFF,                                   \
    .sleep_mode = CONFIG_BT_CTRL_SLEEP_MODE_EFF,                                     \
    .sleep_clock = CONFIG_BT_CTRL_SLEEP_CLOCK_EFF,                                   \
    .ble_st_acl_tx_buf_nb = CONFIG_BT_CTRL_BLE_STATIC_ACL_TX_BUF_NB,                 \
    .ble_hw_cca_check = CONFIG_BT_CTRL_HW_CCA_EFF,                                   \
    .ble_adv_dup_filt_max = CONFIG_BT_CTRL_ADV_DUP_FILT_MAX,                         \
    .coex_param_en = false,                                                          \
    .ce_len_type = CONFIG_BT_CTRL_CE_LENGTH_TYPE_EFF,                                \
    .coex_use_hooks = false,                                                         \
    .hci_tl_type = CONFIG_BT_CTRL_HCI_TL_EFF,                                        \
    .hci_tl_funcs = NULL,                                                            \
    .txant_dft = CONFIG_BT_CTRL_TX_ANTENNA_INDEX_EFF,                                \
    .rxant_dft = CONFIG_BT_CTRL_RX_ANTENNA_INDEX_EFF,                                \
    .txpwr_dft = CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_EFF,                              \
    .cfg_mask = CFG_MASK,                                                            \
    .scan_duplicate_mode = SCAN_DUPLICATE_MODE,                                      \
    .scan_duplicate_type = SCAN_DUPLICATE_TYPE_VALUE,                                \
    .normal_adv_size = NORMAL_SCAN_DUPLICATE_CACHE_SIZE,                             \
    .mesh_adv_size = MESH_DUPLICATE_SCAN_CACHE_SIZE,                                 \
    .coex_phy_coded_tx_rx_time_limit = CONFIG_BT_CTRL_COEX_PHY_CODED_TX_RX_TLIM_EFF, \
    .hw_target_code = BLE_HW_TARGET_CODE_CHIP_ECO0,                                  \
    .slave_ce_len_min = SLAVE_CE_LEN_MIN_DEFAULT,                                    \
    .hw_recorrect_en = AGC_RECORRECT_EN,                                             \
    .cca_thresh = CONFIG_BT_CTRL_HW_CCA_VAL,                                         \
    .scan_backoff_upperlimitmax = BT_CTRL_SCAN_BACKOFF_UPPERLIMITMAX,                \
    .dup_list_refresh_period = DUPL_SCAN_CACHE_REFRESH_PERIOD,                       \
    .ble_50_feat_supp = BT_CTRL_50_FEATURE_SUPPORT,                                  \
    .ble_cca_mode = BT_BLE_CCA_MODE,                                                 \
    .ble_data_lenth_zero_aux = BT_BLE_ADV_DATA_LENGTH_ZERO_AUX,                      \
    .ble_chan_ass_en = BT_CTRL_CHAN_ASS_EN,                                          \
    .ble_ping_en = BT_CTRL_LE_PING_EN,                                               \
    .ble_llcp_disc_flag = BT_CTRL_BLE_LLCP_DISC_FLAG,                                \
    .run_in_flash = BT_CTRL_RUN_IN_FLASH_ONLY,                                       \
    .dtm_en = BT_CTRL_DTM_ENABLE,                                                    \
    .enc_en = BLE_SECURITY_ENABLE,                                                   \
    .qa_test = BT_CTRL_BLE_TEST,                                                     \
    .connect_en = BT_CTRL_BLE_MASTER,                                                \
    .scan_en = BT_CTRL_BLE_SCAN,                                                     \
    .ble_aa_check = BLE_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS_ENABLED,               \
    .adv_en = BT_CTRL_BLE_ADV,                                                       \
  }

typedef struct {
  uint32_t _magic;
  uint32_t _version;
  uint32_t _reserved;
  int (*_open)(void);
  void (*_close)(void);
  void (*_finish_transfers)(void);
  void (*_recv)(uint8_t *buf, uint32_t len, void (*callback)(void *arg, uint8_t status), void *arg);
  void (*_send)(uint8_t *buf, uint32_t len, void (*callback)(void *arg, uint8_t status), void *arg);
  bool (*_flow_off)(void);
  void (*_flow_on)(void);
} esp_bt_hci_tl_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint16_t controller_task_stack_size;
  uint8_t controller_task_prio;
  uint8_t controller_task_run_cpu;
  uint8_t bluetooth_mode;
  uint8_t ble_max_act;
  uint8_t sleep_mode;
  uint8_t sleep_clock;
  uint8_t ble_st_acl_tx_buf_nb;
  uint8_t ble_hw_cca_check;
  uint16_t ble_adv_dup_filt_max;
  bool coex_param_en;
  uint8_t ce_len_type;
  bool coex_use_hooks;
  uint8_t hci_tl_type;
  esp_bt_hci_tl_t *hci_tl_funcs;
  uint8_t txant_dft;
  uint8_t rxant_dft;
  uint8_t txpwr_dft;
  uint32_t cfg_mask;
  uint8_t scan_duplicate_mode;
  uint8_t scan_duplicate_type;
  uint16_t normal_adv_size;
  uint16_t mesh_adv_size;
  uint8_t coex_phy_coded_tx_rx_time_limit;
  uint32_t hw_target_code;
  uint8_t slave_ce_len_min;
  uint8_t hw_recorrect_en;
  uint8_t cca_thresh;
  uint16_t scan_backoff_upperlimitmax;
  uint16_t dup_list_refresh_period;
  bool ble_50_feat_supp;
  uint8_t ble_cca_mode;
  uint8_t ble_data_lenth_zero_aux;
  uint8_t ble_chan_ass_en;
  uint8_t ble_ping_en;
  uint8_t ble_llcp_disc_flag;
  bool run_in_flash;
  bool dtm_en;
  bool enc_en;
  bool qa_test;
  bool connect_en;
  bool scan_en;
  bool ble_aa_check;
  bool adv_en;
} esp_bt_controller_config_t;

typedef struct vhci_host_callback {
  void (*notify_host_send_available)(void);
  int (*notify_host_recv)(uint8_t *data, uint16_t len);
} vhci_host_callback_t;
