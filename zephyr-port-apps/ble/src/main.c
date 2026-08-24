/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/bt_driver_advert.h>
#include <bluetooth/id.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_npl.h>
#include <nimble/nimble_port.h>
#include <pbl/os/mutex.h>
#include <pbl/os/semaphore.h>
#include <pbl/os/tick.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#define HOST_STACK_SIZE 6144
#define HOST_PRIORITY 3

extern int ble_transport_sf32lb52_status(void);
extern const char *ble_transport_sf32lb52_failure_where(void);
extern void ble_transport_sf32lb52_dump_ipc(void);
extern void ble_transport_sf32lb52_report_sync_timeout(void);

K_THREAD_STACK_DEFINE(s_host_stack, HOST_STACK_SIZE);
static struct k_thread s_host_thread;
static struct k_work_delayable s_sync_timeout;

struct BringupAdvertisement {
  BLEAdData header;
  uint8_t bytes[62];
};

static struct BringupAdvertisement s_advertisement;
static bool s_synchronized;

static void prv_fail(const char *where, int code) {
  printk("BLE_FAIL %s %d\n", where, code);
}

static bool prv_npl_smoke_test(void) {
  struct ble_npl_mutex mutex;
  struct ble_npl_sem sem;
  PebbleMutex *pbl_mutex;
  PebbleSemaphore *pbl_sem;

  pbl_mutex = mutex_create();
  pbl_sem = semaphore_create();
  if (pbl_mutex == NULL || pbl_sem == NULL) {
    if (pbl_sem != NULL) {
      semaphore_destroy(pbl_sem);
    }
    if (pbl_mutex != NULL) {
      mutex_destroy(pbl_mutex);
    }
    return false;
  }
  mutex_lock(pbl_mutex);
  mutex_unlock(pbl_mutex);
  semaphore_give(pbl_sem);
  const bool pbl_os_ok = semaphore_take_with_timeout(pbl_sem, 0) &&
                         ticks_to_milliseconds(milliseconds_to_ticks(1000)) == 1000;
  semaphore_destroy(pbl_sem);
  mutex_destroy(pbl_mutex);
  if (!pbl_os_ok) {
    return false;
  }

  if (ble_npl_mutex_init(&mutex) != BLE_NPL_OK ||
      ble_npl_mutex_pend(&mutex, 0) != BLE_NPL_OK ||
      ble_npl_mutex_release(&mutex) != BLE_NPL_OK ||
      ble_npl_sem_init(&sem, 0) != BLE_NPL_OK ||
      ble_npl_sem_release(&sem) != BLE_NPL_OK ||
      ble_npl_sem_pend(&sem, 0) != BLE_NPL_OK) {
    return false;
  }

  ble_npl_time_t ticks;
  uint32_t ms;
  return ble_npl_time_ms_to_ticks(1000, &ticks) == BLE_NPL_OK &&
         ble_npl_time_ticks_to_ms(ticks, &ms) == BLE_NPL_OK && ms == 1000;
}

static void prv_build_pebble_advertisement(const char *name) {
  uint8_t *out = s_advertisement.bytes;
  const size_t name_len = strlen(name);

  *out++ = 2;
  *out++ = 0x01;
  *out++ = GAP_LE_AD_FLAGS_GEN_DISCOVERABLE_MASK |
           GAP_LE_AD_FLAGS_BR_EDR_NOT_SUPPORTED_MASK;

  *out++ = 3;
  *out++ = 0x03;
  *out++ = 0xd9;
  *out++ = 0xfe;

  *out++ = name_len + 1;
  *out++ = 0x09;
  memcpy(out, name, name_len);
  out += name_len;

  *out++ = 2;
  *out++ = 0x0a;
  *out++ = 0;
  s_advertisement.header.ad_data_length = out - s_advertisement.bytes;

  *out++ = 22;
  *out++ = 0xff;
  *out++ = 0xea;
  *out++ = 0x0e;
  *out++ = 0;
  memcpy(out, "PT2BLE000000", 12);
  out += 12;
  *out++ = 18;
  *out++ = 0;
  *out++ = 0;
  *out++ = 1;
  *out++ = 0;
  *out++ = 0;
  s_advertisement.header.scan_resp_data_length =
      out - s_advertisement.bytes - s_advertisement.header.ad_data_length;
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
  BTDeviceAddress address;
  char name[BT_DEVICE_NAME_BUFFER_SIZE];
  int rc;

  s_synchronized = true;
  (void)k_work_cancel_delayable(&s_sync_timeout);
  printk("BLE_CONTROLLER_UP\n");

  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    prv_fail("ensure_addr", rc);
    return;
  }

  bt_driver_id_copy_local_identity_address(&address);
  snprintf(name, sizeof(name), "Pebble Time 2 %02X%02X", address.octets[1], address.octets[0]);
  bt_driver_id_set_local_device_name(name);
  printk("BLE_ADDR " BT_DEVICE_ADDRESS_FMT "\n", BT_DEVICE_ADDRESS_XPLODE(address));

  prv_build_pebble_advertisement(name);
  if (!bt_driver_advert_set_advertising_data(&s_advertisement.header)) {
    prv_fail("adv_data", -EIO);
    return;
  }
  if (!bt_driver_advert_advertising_enable(20, 20)) {
    prv_fail("adv_start", -EIO);
    return;
  }
  printk("BLE_ADV_START\n");
}

static void prv_host_main(void *arg1, void *arg2, void *arg3) {
  (void)arg1;
  (void)arg2;
  (void)arg3;
  nimble_port_run();
}

int main(void) {
  k_tid_t host_tid;
  int rc;

  if (!prv_npl_smoke_test()) {
    prv_fail("npl", -EINVAL);
    return 0;
  }
  printk("BLE_NPL_OK\n");

  nimble_port_init();
  rc = ble_transport_sf32lb52_status();
  if (rc != 0) {
    const char *where = ble_transport_sf32lb52_failure_where();
    prv_fail(where != NULL ? where : "ipc_lcpu", rc);
    return 0;
  }

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_hs_cfg.sync_cb = prv_sync_cb;
  ble_hs_cfg.reset_cb = prv_reset_cb;

  host_tid = k_thread_create(&s_host_thread, s_host_stack,
                             K_THREAD_STACK_SIZEOF(s_host_stack), prv_host_main,
                             NULL, NULL, NULL, HOST_PRIORITY, 0, K_NO_WAIT);
  if (host_tid == NULL) {
    prv_fail("host_thread", -ENOMEM);
    return 0;
  }
  k_thread_name_set(host_tid, "NimbleHost");
  printk("BLE_HOST_UP\n");

  k_work_init_delayable(&s_sync_timeout, prv_sync_timeout);
  (void)k_work_schedule(&s_sync_timeout, K_SECONDS(5));
  ble_hs_sched_start();

  while (true) {
    k_sleep(K_SECONDS(60));
  }
  return 0;
}
