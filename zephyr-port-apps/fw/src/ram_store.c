/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Phase 2 bond store. Security material and CCCDs survive reconnects during a
// boot, but intentionally do not survive a reboot; Phase 3 will seed this from
// the PRF bond stored in flash.

#include <stdbool.h>
#include <string.h>

#include <host/ble_hs.h>
#include <host/ble_store.h>

#define MAX_SEC 8
#define MAX_CCCD 16

static struct ble_store_value_sec s_our_sec[MAX_SEC];
static int s_our_sec_n;
static struct ble_store_value_sec s_peer_sec[MAX_SEC];
static int s_peer_sec_n;
static struct ble_store_value_cccd s_cccd[MAX_CCCD];
static int s_cccd_n;

static bool prv_addr_eq(const ble_addr_t *a, const ble_addr_t *b) {
  return a->type == b->type && memcmp(a->val, b->val, sizeof(a->val)) == 0;
}

static bool prv_addr_none(const ble_addr_t *address) {
  static const uint8_t zero[6];
  return memcmp(address->val, zero, sizeof(zero)) == 0;
}

static struct ble_store_value_sec *prv_sec_array(int obj_type, int *count) {
  if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
    *count = s_our_sec_n;
    return s_our_sec;
  }
  *count = s_peer_sec_n;
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

  int count;
  struct ble_store_value_sec *values = prv_sec_array(obj_type, &count);
  int skip = key->sec.idx;
  for (int i = 0; i < count; i++) {
    if (!prv_addr_none(&key->sec.peer_addr) &&
        !prv_addr_eq(&key->sec.peer_addr, &values[i].peer_addr)) {
      continue;
    }
    if (skip-- > 0) {
      continue;
    }
    value->sec = values[i];
    return 0;
  }
  return BLE_HS_ENOENT;
}

static int prv_write(int obj_type, const union ble_store_value *value) {
  if (obj_type == BLE_STORE_OBJ_TYPE_CCCD) {
    for (int i = 0; i < s_cccd_n; i++) {
      if (prv_addr_eq(&value->cccd.peer_addr, &s_cccd[i].peer_addr) &&
          value->cccd.chr_val_handle == s_cccd[i].chr_val_handle) {
        s_cccd[i] = value->cccd;
        return 0;
      }
    }
    if (s_cccd_n < MAX_CCCD) {
      s_cccd[s_cccd_n++] = value->cccd;
      return 0;
    }
    return BLE_HS_ENOMEM;
  }

  int count;
  struct ble_store_value_sec *values = prv_sec_array(obj_type, &count);
  for (int i = 0; i < count; i++) {
    if (prv_addr_eq(&value->sec.peer_addr, &values[i].peer_addr)) {
      values[i] = value->sec;
      return 0;
    }
  }
  if (count >= MAX_SEC) {
    return BLE_HS_ENOMEM;
  }
  values[count] = value->sec;
  if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
    s_our_sec_n++;
  } else {
    s_peer_sec_n++;
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

  int count;
  struct ble_store_value_sec *values = prv_sec_array(obj_type, &count);
  for (int i = 0; i < count; i++) {
    if (prv_addr_eq(&key->sec.peer_addr, &values[i].peer_addr)) {
      if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
        values[i] = values[--s_our_sec_n];
      } else {
        values[i] = values[--s_peer_sec_n];
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
