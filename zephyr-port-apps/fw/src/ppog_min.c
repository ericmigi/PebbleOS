/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Minimal reversed-PPoGATT link. The watch is the GATT server; the phone
// initiates reset, then Pebble Protocol messages are reassembled and dispatched
// by endpoint. This intentionally has a single connection and no outbound
// retransmit window; the full comm_session transport remains future work.

#include "ppog_min.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/bt_driver_ppog_reversed.h>
#include <kernel/pbl_malloc.h>

#define PPOG_TYPE_DATA 0x0
#define PPOG_TYPE_ACK 0x1
#define PPOG_TYPE_RESET_REQUEST 0x2
#define PPOG_TYPE_RESET_COMPLETE 0x3

#define PPOG_TYPE(byte) ((byte) & 0x7)
#define PPOG_SN(byte) ((byte) >> 3)
#define PPOG_HEADER(type, sn) ((uint8_t)((type) | ((sn) << 3)))

#define PPOG_WINDOW 4
#define PPOG_CONN_NONE UINT16_MAX
#define PPOG_MAX_PP_PAYLOAD 248

#define PP_ENDPOINT_SYSTEM_VERSION 0x0010
#define PP_ENDPOINT_PHONE_VERSION 0x0011
#define PP_ENDPOINT_APP_RUN_STATE 0x0034
#define PP_ENDPOINT_FACTORY_REGISTRY 0x1389
#define PP_ENDPOINT_BLOB_DB 0xb1db

static uint8_t s_pp_buffer[1024];
static uint16_t s_pp_length;
static uint16_t s_conn_handle = PPOG_CONN_NONE;
static uint8_t s_tx_sn;
static bool s_session_open;

// Wire layout of the system-version response, matching
// src/fw/kernel/system_versions.c.
struct __attribute__((__packed__)) FwMeta {
  uint32_t version_timestamp;
  char version_tag[32];
  char version_short[8];
  uint8_t flags;
  uint8_t hw_platform;
  uint8_t metadata_version;
};

struct __attribute__((__packed__)) VersionsMessage {
  uint8_t command;
  struct FwMeta running_fw_metadata;
  struct FwMeta recovery_fw_metadata;
  uint32_t boot_version;
  char hw_version[9];
  char serial_number[12];
  uint8_t device_address[6];
  uint32_t resources_crc;
  uint32_t resources_timestamp;
  char iso_locale[6];
  uint16_t lang_version;
  uint64_t capabilities;
  uint8_t is_unfaithful;
  uint16_t activity_insights_version;
  uint16_t javascript_bytecode_version;
};

static bool prv_notify(uint16_t conn_handle, uint8_t type, uint8_t sn,
                       const uint8_t *payload, uint16_t payload_len) {
  uint8_t packet[4];

  if (payload_len + 1U > sizeof(packet)) {
    return false;
  }
  packet[0] = PPOG_HEADER(type, sn);
  if (payload_len != 0) {
    memcpy(packet + 1, payload, payload_len);
  }
  BTErrno rc =
      bt_driver_ppog_reversed_notify(conn_handle, packet, payload_len + 1U);
  if (rc != BTErrnoOK) {
    printk("FW_BLE_PPOG_TX_FAIL type=%u rc=%d\n", type, (int)rc);
    return false;
  }
  return true;
}

bool ppog_min_send_pp(uint16_t endpoint, const uint8_t *payload,
                      uint16_t payload_len) {
  uint8_t packet[1 + 4 + PPOG_MAX_PP_PAYLOAD];
  BTErrno rc;

  if (!s_session_open || s_conn_handle == PPOG_CONN_NONE ||
      payload_len > PPOG_MAX_PP_PAYLOAD ||
      (payload_len != 0 && payload == NULL)) {
    return false;
  }

  packet[0] = PPOG_HEADER(PPOG_TYPE_DATA, s_tx_sn);
  packet[1] = payload_len >> 8;
  packet[2] = payload_len & 0xff;
  packet[3] = endpoint >> 8;
  packet[4] = endpoint & 0xff;
  if (payload_len != 0) {
    memcpy(packet + 5, payload, payload_len);
  }

  rc = bt_driver_ppog_reversed_notify(s_conn_handle, packet,
                                       payload_len + 5U);
  printk("FW_BLE_PP_TX endpoint=0x%04x sn=%u len=%u rc=%d\n", endpoint,
         s_tx_sn, payload_len, (int)rc);
  if (rc != BTErrnoOK) {
    return false;
  }
  s_tx_sn = (s_tx_sn + 1U) & 0x1f;
  return true;
}

static void prv_session_opened(void) {
  static const uint8_t version_request = 0x00;

  if (s_session_open) {
    return;
  }
  s_session_open = true;
  s_tx_sn = 0;
  printk("FW_BLE_PPOG_UP handle=%u\n", (unsigned int)s_conn_handle);
  (void)ppog_min_send_pp(PP_ENDPOINT_PHONE_VERSION, &version_request,
                         sizeof(version_request));
}

static void prv_send_system_version(void) {
  struct VersionsMessage message;

  memset(&message, 0, sizeof(message));
  message.command = 0x01;
  strncpy(message.running_fw_metadata.version_tag, "v4.0.0-zephyr",
          sizeof(message.running_fw_metadata.version_tag) - 1);
  strncpy(message.running_fw_metadata.version_short, "v4.0.0",
          sizeof(message.running_fw_metadata.version_short) - 1);
  message.running_fw_metadata.flags = 0x02;
  message.running_fw_metadata.hw_platform = 18;
  message.running_fw_metadata.metadata_version = 1;

  strncpy(message.recovery_fw_metadata.version_tag, "v0.0.1-zephyr-prf",
          sizeof(message.recovery_fw_metadata.version_tag) - 1);
  strncpy(message.recovery_fw_metadata.version_short, "v0.0.1",
          sizeof(message.recovery_fw_metadata.version_short) - 1);
  message.recovery_fw_metadata.flags = 0x03;
  message.recovery_fw_metadata.hw_platform = 18;
  message.recovery_fw_metadata.metadata_version = 1;
  strncpy(message.hw_version, "obelix", sizeof(message.hw_version) - 1);
  strncpy(message.iso_locale, "en_US", sizeof(message.iso_locale) - 1);

  // Do not advertise BlobDB v2: the minimal endpoint router does not implement
  // its version handshake. Legacy BlobDB v1 can still arrive and is logged.
  message.capabilities = 0x160c6ffULL & ~0x400000ULL;
  (void)ppog_min_send_pp(PP_ENDPOINT_SYSTEM_VERSION,
                         (const uint8_t *)&message, sizeof(message));
}

static void prv_dispatch_pp(uint16_t endpoint, const uint8_t *payload,
                            uint16_t payload_len) {
  if (endpoint == PP_ENDPOINT_SYSTEM_VERSION && payload_len >= 1 &&
      payload[0] == 0x00) {
    prv_send_system_version();
  } else if (endpoint == PP_ENDPOINT_FACTORY_REGISTRY && payload_len >= 1 &&
             payload[0] == 0x00) {
    const uint8_t color_response[6] = {0x01, 0x04, 0x00,
                                       0x00, 0x00, 0x01};
    (void)ppog_min_send_pp(PP_ENDPOINT_FACTORY_REGISTRY, color_response,
                           sizeof(color_response));
  } else if (endpoint == PP_ENDPOINT_APP_RUN_STATE && payload_len >= 1 &&
             payload[0] == 0x03) {
    uint8_t run_response[17] = {0x01};
    memset(run_response + 1, 0x5a, sizeof(run_response) - 1);
    (void)ppog_min_send_pp(PP_ENDPOINT_APP_RUN_STATE, run_response,
                           sizeof(run_response));
  } else if (endpoint == PP_ENDPOINT_BLOB_DB) {
    printk("FW_BLE_BLOB_STUB len=%u\n", payload_len);
  }
}

static void prv_feed_pp(const uint8_t *data, uint16_t length) {
  if (s_pp_length + length > sizeof(s_pp_buffer)) {
    s_pp_length = 0;
    printk("FW_BLE_PP_RX_OVERFLOW\n");
    return;
  }
  memcpy(s_pp_buffer + s_pp_length, data, length);
  s_pp_length += length;

  while (s_pp_length >= 4) {
    uint16_t payload_len = (s_pp_buffer[0] << 8) | s_pp_buffer[1];
    uint16_t endpoint = (s_pp_buffer[2] << 8) | s_pp_buffer[3];
    uint16_t total = 4U + payload_len;

    if (total > sizeof(s_pp_buffer)) {
      s_pp_length = 0;
      printk("FW_BLE_PP_RX_INVALID len=%u\n", payload_len);
      return;
    }
    if (s_pp_length < total) {
      break;
    }
    printk("FW_BLE_PP_RX endpoint=0x%04x len=%u\n", endpoint, payload_len);
    prv_dispatch_pp(endpoint, s_pp_buffer + 4, payload_len);
    memmove(s_pp_buffer, s_pp_buffer + total, s_pp_length - total);
    s_pp_length -= total;
  }
}

void bt_driver_cb_ppog_reversed_subscribed(const BTDeviceInternal *device,
                                           uint16_t conn_handle) {
  (void)device;
  s_pp_length = 0;
  s_conn_handle = conn_handle;
  s_session_open = false;
  s_tx_sn = 0;
  printk("FW_BLE_PPOG_SUBSCRIBED handle=%u\n",
         (unsigned int)conn_handle);
}

void bt_driver_cb_ppog_reversed_unsubscribed(uint16_t conn_handle) {
  if (conn_handle == s_conn_handle) {
    s_pp_length = 0;
    s_conn_handle = PPOG_CONN_NONE;
    s_session_open = false;
  }
  printk("FW_BLE_PPOG_UNSUBSCRIBED handle=%u\n",
         (unsigned int)conn_handle);
}

void bt_driver_cb_ppog_reversed_data_written(uint16_t conn_handle, uint8_t *buf,
                                             uint16_t len) {
  if (len == 0 || conn_handle != s_conn_handle) {
    kernel_free(buf);
    return;
  }

  uint8_t type = PPOG_TYPE(buf[0]);
  uint8_t sn = PPOG_SN(buf[0]);
  switch (type) {
    case PPOG_TYPE_RESET_REQUEST: {
      const uint8_t reset_complete[2] = {PPOG_WINDOW, PPOG_WINDOW};
      s_pp_length = 0;
      s_session_open = false;
      (void)prv_notify(conn_handle, PPOG_TYPE_RESET_COMPLETE, 0,
                       reset_complete, sizeof(reset_complete));
      break;
    }
    case PPOG_TYPE_RESET_COMPLETE:
      prv_session_opened();
      break;
    case PPOG_TYPE_DATA:
      if (s_session_open) {
        prv_feed_pp(buf + 1, len - 1);
      }
      (void)prv_notify(conn_handle, PPOG_TYPE_ACK, sn, NULL, 0);
      break;
    case PPOG_TYPE_ACK:
    default:
      break;
  }
  kernel_free(buf);
}
