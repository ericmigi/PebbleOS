/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Minimal in-RAM NimBLE bond store for the BLE bring-up app. Transient bonds
// (lost on reboot) are enough for the CoreApp connect/PPoGATT demo; swap in the
// flash-backed nimble_store.c once the full comm stack is folded in.
// ponytail: fixed 8-entry arrays, no persistence; upgrade to nimble_store.c
// when bonds must survive reboot.

#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#define MAX_SEC 8
#define MAX_CCCD 16

static struct ble_store_value_sec s_our_sec[MAX_SEC];
static int s_our_sec_n;
static struct ble_store_value_sec s_peer_sec[MAX_SEC];
static int s_peer_sec_n;
static struct ble_store_value_cccd s_cccd[MAX_CCCD];
static int s_cccd_n;

static bool prv_addr_eq(const ble_addr_t *a, const ble_addr_t *b) {
  return a->type == b->type && memcmp(a->val, b->val, 6) == 0;
}

static bool prv_addr_none(const ble_addr_t *a) {
  static const uint8_t zero[6];
  return memcmp(a->val, zero, 6) == 0;
}

static struct ble_store_value_sec *prv_sec_array(int obj_type, int *n) {
  if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
    *n = s_our_sec_n;
    return s_our_sec;
  }
  *n = s_peer_sec_n;
  return s_peer_sec;
}

static int prv_read(int obj_type, const union ble_store_key *key,
                    union ble_store_value *value) {
  if (obj_type == BLE_STORE_OBJ_TYPE_CCCD) {
    int skip = key->cccd.idx;
    for (int i = 0; i < s_cccd_n; i++) {
      if (!prv_addr_none(&key->cccd.peer_addr) &&
          !prv_addr_eq(&key->cccd.peer_addr, &s_cccd[i].peer_addr)) {
        continue;
      }
      if (key->cccd.chr_val_handle != 0 &&
          key->cccd.chr_val_handle != s_cccd[i].chr_val_handle) {
        continue;
      }
      if (skip-- > 0) {
        continue;
      }
      value->cccd = s_cccd[i];
      return 0;
    }
    return BLE_HS_ENOENT;
  }

  int n;
  struct ble_store_value_sec *arr = prv_sec_array(obj_type, &n);
  int skip = key->sec.idx;
  for (int i = 0; i < n; i++) {
    if (!prv_addr_none(&key->sec.peer_addr) &&
        !prv_addr_eq(&key->sec.peer_addr, &arr[i].peer_addr)) {
      continue;
    }
    if (skip-- > 0) {
      continue;
    }
    value->sec = arr[i];
    return 0;
  }
  return BLE_HS_ENOENT;
}

static int prv_write(int obj_type, const union ble_store_value *val) {
  if (obj_type == BLE_STORE_OBJ_TYPE_CCCD) {
    for (int i = 0; i < s_cccd_n; i++) {
      if (prv_addr_eq(&val->cccd.peer_addr, &s_cccd[i].peer_addr) &&
          val->cccd.chr_val_handle == s_cccd[i].chr_val_handle) {
        s_cccd[i] = val->cccd;
        return 0;
      }
    }
    if (s_cccd_n < MAX_CCCD) {
      s_cccd[s_cccd_n++] = val->cccd;
    }
    return 0;
  }

  int n;
  struct ble_store_value_sec *arr = prv_sec_array(obj_type, &n);
  for (int i = 0; i < n; i++) {
    if (prv_addr_eq(&val->sec.peer_addr, &arr[i].peer_addr)) {
      arr[i] = val->sec;
      return 0;
    }
  }
  if (n < MAX_SEC) {
    arr[n] = val->sec;
    if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
      s_our_sec_n++;
    } else {
      s_peer_sec_n++;
    }
  }
  return 0;
}

static int prv_delete(int obj_type, const union ble_store_key *key) {
  if (obj_type == BLE_STORE_OBJ_TYPE_CCCD) {
    for (int i = 0; i < s_cccd_n; i++) {
      if ((prv_addr_none(&key->cccd.peer_addr) ||
           prv_addr_eq(&key->cccd.peer_addr, &s_cccd[i].peer_addr)) &&
          (key->cccd.chr_val_handle == 0 ||
           key->cccd.chr_val_handle == s_cccd[i].chr_val_handle)) {
        s_cccd[i] = s_cccd[--s_cccd_n];
        return 0;
      }
    }
    return BLE_HS_ENOENT;
  }

  int n;
  struct ble_store_value_sec *arr = prv_sec_array(obj_type, &n);
  for (int i = 0; i < n; i++) {
    if (prv_addr_eq(&key->sec.peer_addr, &arr[i].peer_addr)) {
      if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
        arr[i] = arr[--s_our_sec_n];
      } else {
        arr[i] = arr[--s_peer_sec_n];
      }
      return 0;
    }
  }
  return BLE_HS_ENOENT;
}

void ram_store_init(void) {
  ble_hs_cfg.store_read_cb = prv_read;
  ble_hs_cfg.store_write_cb = prv_write;
  ble_hs_cfg.store_delete_cb = prv_delete;
}
