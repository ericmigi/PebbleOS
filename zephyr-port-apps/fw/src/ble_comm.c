/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "ble_comm.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#define BLE_INIT_STACK_SIZE 8192
#define BLE_HOST_STACK_SIZE 6144
#define BLE_INIT_PRIORITY 6
#define BLE_HOST_PRIORITY 3

extern int ble_transport_sf32lb52_status(void);
extern const char *ble_transport_sf32lb52_failure_where(void);
extern void ble_transport_sf32lb52_report_sync_timeout(void);

extern void pebble_pairing_service_init(void);
extern void pebble_pairing_service_notify_connectivity(uint16_t conn_handle);
extern void ppog_reversed_service_init(void);
extern void ram_store_init(void);

K_THREAD_STACK_DEFINE(s_init_stack, BLE_INIT_STACK_SIZE);
K_THREAD_STACK_DEFINE(s_host_stack, BLE_HOST_STACK_SIZE);
static struct k_thread s_init_thread;
static struct k_thread s_host_thread;
static struct k_work_delayable s_sync_timeout;
static bool s_init_started;
static bool s_synchronized;

static uint8_t s_advertisement[31];
static uint8_t s_scan_response[23];
static uint8_t s_advertisement_len;
static uint8_t s_scan_response_len;

static void prv_fail(const char *where, int code) {
  printk("FW_BLE_FAIL %s %d\n", where, code);
}

static void prv_build_advertisement(const char *name) {
  uint8_t *out = s_advertisement;
  size_t name_len = strlen(name);

  *out++ = 2;
  *out++ = 0x01;  // Flags
  *out++ = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  *out++ = 3;
  *out++ = 0x03;  // Complete list of 16-bit service UUIDs
  *out++ = 0xd9;
  *out++ = 0xfe;

  *out++ = name_len + 1;
  *out++ = 0x09;  // Complete local name
  memcpy(out, name, name_len);
  out += name_len;

  *out++ = 2;
  *out++ = 0x0a;  // TX power
  *out++ = 0;
  s_advertisement_len = out - s_advertisement;

  out = s_scan_response;
  *out++ = 22;
  *out++ = 0xff;  // Manufacturer-specific data
  *out++ = 0xea;
  *out++ = 0x0e;
  *out++ = 0;
  memcpy(out, "PT2BLE000000", 12);
  out += 12;
  *out++ = 18;  // ObelixPVT
  *out++ = 0;
  *out++ = 0;
  *out++ = 1;
  *out++ = 0;
  *out++ = 0;
  s_scan_response_len = out - s_scan_response;
}

static int prv_start_advertising(void);

static int prv_gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        printk("FW_BLE_CONNECTED handle=%u\n",
               (unsigned int)event->connect.conn_handle);
      } else {
        prv_fail("connect", event->connect.status);
        (void)prv_start_advertising();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      printk("FW_BLE_DISCONNECTED reason=0x%x\n", event->disconnect.reason);
      (void)prv_start_advertising();
      break;
    case BLE_GAP_EVENT_ENC_CHANGE:
      if (event->enc_change.status == 0) {
        printk("FW_BLE_ENCRYPTED handle=%u\n",
               (unsigned int)event->enc_change.conn_handle);
        pebble_pairing_service_notify_connectivity(
            event->enc_change.conn_handle);
      } else {
        prv_fail("encryption", event->enc_change.status);
      }
      break;
    case BLE_GAP_EVENT_PAIRING_COMPLETE:
      if (event->pairing_complete.status == 0) {
        printk("FW_BLE_PAIRED handle=%u\n",
               (unsigned int)event->pairing_complete.conn_handle);
      } else {
        prv_fail("pairing", event->pairing_complete.status);
      }
      break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
      if (rc != 0) {
        return rc;
      }
      ble_store_util_delete_peer(&desc.peer_id_addr);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
      break;
  }
  return 0;
}

static int prv_start_advertising(void) {
  struct ble_gap_adv_params params = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,
      .itvl_min = BLE_GAP_ADV_ITVL_MS(20),
      .itvl_max = BLE_GAP_ADV_ITVL_MS(20),
  };
  uint8_t own_addr_type;
  int rc;

  rc = ble_gap_adv_set_data(s_advertisement, s_advertisement_len);
  if (rc != 0) {
    prv_fail("adv_data", rc);
    return rc;
  }
  rc = ble_gap_adv_rsp_set_data(s_scan_response, s_scan_response_len);
  if (rc != 0) {
    prv_fail("scan_rsp", rc);
    return rc;
  }
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    prv_fail("addr_type", rc);
    return rc;
  }
  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params,
                         prv_gap_event, NULL);
  if (rc != 0) {
    prv_fail("adv_start", rc);
    return rc;
  }
  printk("FW_BLE_ADV\n");
  return 0;
}

static void prv_sync_timeout(struct k_work *work) {
  (void)work;
  if (!s_synchronized) {
    ble_transport_sf32lb52_report_sync_timeout();
  }
}

static void prv_reset_cb(int reason) {
  prv_fail("host_reset", reason);
}

static void prv_sync_cb(void) {
  uint8_t own_addr_type;
  uint8_t address[6];
  char name[32];
  int rc;

  s_synchronized = true;
  (void)k_work_cancel_delayable(&s_sync_timeout);

  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    prv_fail("ensure_addr", rc);
    return;
  }
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    prv_fail("addr_type", rc);
    return;
  }
  rc = ble_hs_id_copy_addr(own_addr_type, address, NULL);
  if (rc != 0) {
    prv_fail("copy_addr", rc);
    return;
  }

  snprintf(name, sizeof(name), "Pebble Time 2 %02X%02X", address[1],
           address[0]);
  rc = ble_svc_gap_device_name_set(name);
  if (rc != 0) {
    prv_fail("device_name", rc);
    return;
  }

  printk("FW_BLE_CONTROLLER_UP\n");
  printk("FW_BLE_ADDR %02X:%02X:%02X:%02X:%02X:%02X\n", address[5],
         address[4], address[3], address[2], address[1], address[0]);
  prv_build_advertisement(name);
  (void)prv_start_advertising();
}

static void prv_host_main(void *arg1, void *arg2, void *arg3) {
  (void)arg1;
  (void)arg2;
  (void)arg3;
  nimble_port_run();
}

static void prv_init_main(void *arg1, void *arg2, void *arg3) {
  k_tid_t host_tid;
  int rc;

  (void)arg1;
  (void)arg2;
  (void)arg3;
  printk("FW_BLE_INIT\n");

  nimble_port_init();
  rc = ble_transport_sf32lb52_status();
  if (rc != 0) {
    const char *where = ble_transport_sf32lb52_failure_where();
    prv_fail(where != NULL ? where : "ipc_lcpu", rc);
    return;
  }

  ble_svc_gap_init();
  ble_svc_gatt_init();
  pebble_pairing_service_init();
  ppog_reversed_service_init();
  ram_store_init();

  ble_hs_cfg.sync_cb = prv_sync_cb;
  ble_hs_cfg.reset_cb = prv_reset_cb;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  host_tid = k_thread_create(
      &s_host_thread, s_host_stack, K_THREAD_STACK_SIZEOF(s_host_stack),
      prv_host_main, NULL, NULL, NULL, BLE_HOST_PRIORITY, 0, K_NO_WAIT);
  if (host_tid == NULL) {
    prv_fail("host_thread", -ENOMEM);
    return;
  }
  k_thread_name_set(host_tid, "NimbleHost");
  printk("FW_BLE_HOST_UP\n");

  k_work_init_delayable(&s_sync_timeout, prv_sync_timeout);
  (void)k_work_schedule(&s_sync_timeout, K_SECONDS(5));
  ble_hs_sched_start();
}

void fw_ble_init(void) {
  k_tid_t init_tid;

  if (s_init_started) {
    return;
  }
  s_init_started = true;
  init_tid = k_thread_create(
      &s_init_thread, s_init_stack, K_THREAD_STACK_SIZEOF(s_init_stack),
      prv_init_main, NULL, NULL, NULL, BLE_INIT_PRIORITY, 0, K_NO_WAIT);
  if (init_tid == NULL) {
    prv_fail("init_thread", -ENOMEM);
    return;
  }
  k_thread_name_set(init_tid, "BleInit");
}
