/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Minimal reversed-PPoGATT link layer for the BLE bring-up demo. The watch is
// the GATT server; the phone is the PPoGATT client and initiates the reset.
// We answer ResetRequest with ResetComplete, ACK every Data packet, reassemble
// the Pebble Protocol stream and log each PP message. Just enough to bring the
// link up and see what CoreApp sends after the session opens.
// ponytail: single connection, RX-only ACKing, no windowing/retransmit; grow
// into the real ppogatt.c + comm_session when more than a demo is needed.

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/bt_driver_ppog_reversed.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include "fw_ota.h"
#include "notif_render.h"

#define PPOG_TYPE_DATA 0x0
#define PPOG_TYPE_ACK 0x1
#define PPOG_TYPE_RESET_REQUEST 0x2
#define PPOG_TYPE_RESET_COMPLETE 0x3

#define PPOG_TYPE(b) ((b) & 0x7)
#define PPOG_SN(b) ((b) >> 3)
#define PPOG_HDR(type, sn) ((uint8_t)((type) | ((sn) << 3)))

#define PPOG_WINDOW 4

// Sized to reassemble a full PutBytes Put request (2044 data bytes + a 9-byte
// PutRequest header) with headroom, so firmware chunks don't overflow.
static uint8_t s_pp_buf[2560];
static uint16_t s_pp_len;
static uint8_t s_tx_sn;   // our PPoGATT Data sequence number
static bool s_session_open;

// Pebble Protocol endpoints we care about for the demo.
#define PP_ENDPOINT_SYSTEM_VERSION 0x0010  // phone asks watch's version
#define PP_ENDPOINT_PHONE_VERSION 0x0011   // watch asks phone's version
#define PP_ENDPOINT_FACTORY_REGISTRY 0x1389  // phone reads mfg_color
#define PP_ENDPOINT_APP_RUN_STATE 0x0034   // phone asks running app (STATUS)
#define PP_ENDPOINT_BLOB_DB 0xb1db         // notifications (blob_db insert)
#define PP_ENDPOINT_SYSTEM_MESSAGE 0x0012  // FW update start/status/complete
#define PP_ENDPOINT_PUT_BYTES 0xBEEF       // PutBytes object transfer

// Wire layout of the watch's system-version (0x0010) response, mirrored from
// src/fw/kernel/system_versions.c (struct VersionsMessage) + its sub-structs.
struct __attribute__((__packed__)) FwMeta {
  uint32_t version_timestamp;
  char version_tag[32];
  char version_short[8];
  uint8_t flags;  // is_recovery/ble/dual/slot0 bitfield + reserved
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

static void prv_send(uint16_t conn, uint8_t type, uint8_t sn, const uint8_t *pl,
                     uint16_t pl_len) {
  uint8_t pkt[16];
  if (pl_len + 1U > sizeof(pkt)) {
    return;
  }
  pkt[0] = PPOG_HDR(type, sn);
  if (pl_len) {
    memcpy(pkt + 1, pl, pl_len);
  }
  BTErrno rc = bt_driver_ppog_reversed_notify(conn, pkt, pl_len + 1U);
  if (rc != BTErrnoOK) {
    printk("BLE_PPOG_TX_ERR type=%u rc=%d\n", type, (int)rc);
  }
}

// Send a Pebble Protocol message as a PPoGATT Data packet.
// PP framing: [be16 length][be16 endpoint][payload]; PPoGATT: [type|sn<<3] + PP.
static void prv_send_pp(uint16_t conn, uint16_t endpoint, const uint8_t *payload,
                        uint16_t payload_len) {
  uint8_t pkt[260];
  if (1U + 4U + payload_len > sizeof(pkt)) {
    return;
  }
  pkt[0] = PPOG_HDR(PPOG_TYPE_DATA, s_tx_sn);
  pkt[1] = payload_len >> 8;
  pkt[2] = payload_len & 0xFF;
  pkt[3] = endpoint >> 8;
  pkt[4] = endpoint & 0xFF;
  if (payload_len) {
    memcpy(pkt + 5, payload, payload_len);
  }
  BTErrno rc = bt_driver_ppog_reversed_notify(conn, pkt, 5U + payload_len);
  printk("BLE_PP_TX endpoint=0x%04x sn=%u len=%u rc=%d\n", endpoint, s_tx_sn,
         payload_len, (int)rc);
  s_tx_sn = (s_tx_sn + 1) & 0x1F;
}

// Exposed to putbytes_min.c so the FW-update receive path can send its ACK/NACK
// and system-message responses over the shared PPoGATT link.
void ppog_min_send_pp(uint16_t conn, uint16_t endpoint, const uint8_t *payload,
                      uint16_t payload_len) {
  prv_send_pp(conn, endpoint, payload, payload_len);
}

// After the PPoGATT session opens, the watch drives the app-layer handshake by
// requesting the phone's version (endpoint 0x11, command 0x00). CoreApp gates
// notification delivery on this exchange completing.
static void prv_session_opened(uint16_t conn) {
  if (s_session_open) {
    return;
  }
  s_session_open = true;
  s_tx_sn = 0;
  const uint8_t version_request[1] = {0x00};  // CommSessionVersionCommandRequest
  prv_send_pp(conn, PP_ENDPOINT_PHONE_VERSION, version_request,
              sizeof(version_request));
  printk("BLE_PP_SESSION_OPEN sent phone-version request\n");
}

// Reply to the phone's system-version request (endpoint 0x0010) with a valid
// VersionsMessage so CoreApp marks the watch ready and starts sending data.
static void prv_send_system_version(uint16_t conn) {
  struct VersionsMessage m;
  memset(&m, 0, sizeof(m));
  m.command = 0x01;  // VERSION_RESPONSE
  // CoreApp's PebbleConnector rejects any running FW below v3.0 ("FW below v3.0
  // isn't supported; going into recovery mode") and classifies the watch
  // ConnectedPebbleDeviceInRecovery, which suppresses blob_db notification
  // delivery. Report a >=3.0 running version so it classifies us a normal
  // ConnectedPebbleDevice and forwards notifications.
  strncpy(m.running_fw_metadata.version_tag, "v4.0.0-zephyr",
          sizeof(m.running_fw_metadata.version_tag) - 1);
  strncpy(m.running_fw_metadata.version_short, "v4.0.0",
          sizeof(m.running_fw_metadata.version_short) - 1);
  m.running_fw_metadata.flags = 0x02;      // is_ble_firmware
  m.running_fw_metadata.hw_platform = 18;  // ObelixPVT
  m.running_fw_metadata.metadata_version = 1;
  // Populate recovery_fw_metadata too. If it is left zeroed the phone parses
  // recoveryFwVersion=null and PebbleConnector logs "No recovery FW installed!!!
  // going into recovery mode" -> classifies the watch as
  // ConnectedPebbleDeviceInRecovery, which suppresses notification (blob_db)
  // delivery. A real watch always reports a PRF/recovery image; mirror that so
  // CoreApp treats us as a normal ConnectedPebbleDevice and forwards blob_db.
  strncpy(m.recovery_fw_metadata.version_tag, "v0.0.1-zephyr-prf",
          sizeof(m.recovery_fw_metadata.version_tag) - 1);
  strncpy(m.recovery_fw_metadata.version_short, "v0.0.1",
          sizeof(m.recovery_fw_metadata.version_short) - 1);
  m.recovery_fw_metadata.flags = 0x03;      // is_recovery_firmware | is_ble_firmware
  m.recovery_fw_metadata.hw_platform = 18;  // ObelixPVT
  m.recovery_fw_metadata.metadata_version = 1;
  strncpy(m.hw_version, "obelix", sizeof(m.hw_version) - 1);
  strncpy(m.iso_locale, "en_US", sizeof(m.iso_locale) - 1);
  // Capabilities: run_state, log_dump, ext music, ext notification, lang_pack,
  // app_msg_8k, activity, voice, notif_filtering, unread_coredump, smooth_fw,
  // custom_vibe, continue_fw.
  // NB: blob_db_version_support (bit 22, 0x400000) is intentionally CLEARED.
  // With it set, CoreApp drives notifications over BlobDB v2 (endpoint 0xb2db)
  // and first waits on a v2 version handshake this minimal firmware doesn't
  // answer, so it never pushes the notification. Clearing it makes CoreApp use
  // legacy BlobDB v1 (0xb1db), which pushes the INSERT directly -> rendered.
  m.capabilities = 0x160C6FFULL & ~0x400000ULL;  // = 0x120C6FF
  prv_send_pp(conn, PP_ENDPOINT_SYSTEM_VERSION, (const uint8_t *)&m, sizeof(m));
  printk("BLE_PP_SYSVER_SENT len=%u\n", (unsigned int)sizeof(m));
}

// A CoreApp notification arrives as a blob_db INSERT on endpoint 0xb1db. Wire
// layout of the payload (after the 4-byte PP header): [cmd:1][token:2][db_id:1]
// then, for INSERT (0x01): [key_size:1][key][value_size:2 LE][value]; for
// INSERT_WITH_TIMESTAMP (0x0D) a 4-byte timestamp sits before key_size. db_id
// 0x04 is the notifications DB; the value is a serialized TimelineItem. Extract
// it and hand the raw bytes to the folded PebbleOS render path.
static void prv_handle_blob_db(uint16_t plen, const uint8_t *p) {
  if (plen < 4) {
    return;
  }
  uint8_t cmd = p[0];
  uint8_t db_id = p[3];
  uint16_t off = 4;  // past cmd + token(2) + db_id(1)
  if (cmd == 0x0D) {
    off += 4;  // INSERT_WITH_TIMESTAMP: skip the conflict-resolution timestamp
  } else if (cmd != 0x01) {
    printk("BLE_BLOB_SKIP cmd=0x%02x\n", cmd);
    return;
  }
  if (db_id != 0x04) {  // BlobDBIdNotifs
    printk("BLE_BLOB_OTHER_DB db=0x%02x\n", db_id);
    return;
  }
  if (off + 1u > plen) {
    return;
  }
  uint8_t key_size = p[off++];
  off += key_size;  // skip the notification key (its uuid)
  if (off + 2u > plen) {
    return;
  }
  uint16_t value_size = (uint16_t)(p[off] | (p[off + 1] << 8));
  off += 2;
  if (off + value_size > plen) {
    printk("BLE_BLOB_TRUNC off=%u val=%u plen=%u\n", off, value_size, plen);
    return;
  }
  printk("BLE_BLOB_INSERT db=0x%02x key=%u val=%u\n", db_id, key_size,
         value_size);
  notif_render_blob_db_value(p + off, value_size);
}

// Reassemble the Pebble Protocol stream: [be16 length][be16 endpoint][payload].
static void prv_pp_feed(uint16_t conn, const uint8_t *data, uint16_t len) {
  if (s_pp_len + len > sizeof(s_pp_buf)) {
    s_pp_len = 0;  // overflow guard; resync
    return;
  }
  memcpy(s_pp_buf + s_pp_len, data, len);
  s_pp_len += len;

  while (s_pp_len >= 4) {
    uint16_t msg_len = (s_pp_buf[0] << 8) | s_pp_buf[1];
    uint16_t endpoint = (s_pp_buf[2] << 8) | s_pp_buf[3];
    uint16_t total = 4 + msg_len;
    if (s_pp_len < total) {
      break;  // wait for more
    }
    printk("BLE_PP_MSG endpoint=0x%04x len=%u b0=%02x b1=%02x\n", endpoint,
           msg_len, msg_len > 0 ? s_pp_buf[4] : 0,
           msg_len > 1 ? s_pp_buf[5] : 0);
    if (endpoint == PP_ENDPOINT_SYSTEM_VERSION && msg_len >= 1 &&
        s_pp_buf[4] == 0x00) {
      prv_send_system_version(conn);
    } else if (endpoint == PP_ENDPOINT_FACTORY_REGISTRY && msg_len >= 1 &&
               s_pp_buf[4] == 0x00) {
      // Read of "mfg_color": reply {0x01, len=4, 0,0,0, color}.
      const uint8_t color_resp[6] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x01};
      prv_send_pp(conn, PP_ENDPOINT_FACTORY_REGISTRY, color_resp,
                  sizeof(color_resp));
      printk("BLE_PP_FACTORY_COLOR_SENT\n");
    } else if (endpoint == PP_ENDPOINT_APP_RUN_STATE && msg_len >= 1 &&
               s_pp_buf[4] == 0x03) {
      // STATUS request: reply AppRunStateStart {cmd=0x01, 16-byte uuid}. The
      // phone's Negotiator only accepts cmd 0x01 + a NON-ZERO uuid, so fill a
      // fixed non-zero uuid (no real app is running).
      uint8_t run_resp[17] = {0x01};
      memset(run_resp + 1, 0x5a, 16);
      prv_send_pp(conn, PP_ENDPOINT_APP_RUN_STATE, run_resp, sizeof(run_resp));
      printk("BLE_PP_APP_RUN_STATE_SENT\n");
    } else if (endpoint == PP_ENDPOINT_BLOB_DB) {
      prv_handle_blob_db(msg_len, &s_pp_buf[4]);
    } else if (endpoint == PP_ENDPOINT_SYSTEM_MESSAGE) {
      fw_ota_handle_system_msg(conn, &s_pp_buf[4], msg_len);
    } else if (endpoint == PP_ENDPOINT_PUT_BYTES) {
      fw_ota_handle_putbytes(conn, &s_pp_buf[4], msg_len);
    }
    memmove(s_pp_buf, s_pp_buf + total, s_pp_len - total);
    s_pp_len -= total;
  }
}

// --- bt_driver_cb_ppog_reversed_* hooks (called from ppog_reversed_service) ---

void bt_driver_cb_ppog_reversed_subscribed(const BTDeviceInternal *device,
                                           uint16_t conn_handle) {
  (void)device;
  s_pp_len = 0;
  s_session_open = false;
  s_tx_sn = 0;
  printk("BLE_PPOG_SUBSCRIBED conn=%u\n", (unsigned int)conn_handle);
}

void bt_driver_cb_ppog_reversed_unsubscribed(uint16_t conn_handle) {
  s_pp_len = 0;
  s_session_open = false;
  printk("BLE_PPOG_UNSUBSCRIBED conn=%u\n", (unsigned int)conn_handle);
}

void bt_driver_cb_ppog_reversed_data_written(uint16_t conn_handle, uint8_t *buf,
                                             uint16_t len) {
  if (len < 1) {
    free(buf);
    return;
  }
  uint8_t type = PPOG_TYPE(buf[0]);
  uint8_t sn = PPOG_SN(buf[0]);
  printk("BLE_PPOG_RX type=%u sn=%u len=%u\n", type, sn, len);

  switch (type) {
    case PPOG_TYPE_RESET_REQUEST: {
      s_pp_len = 0;
      s_session_open = false;
      const uint8_t rc_payload[2] = {PPOG_WINDOW, PPOG_WINDOW};
      prv_send(conn_handle, PPOG_TYPE_RESET_COMPLETE, 0, rc_payload,
               sizeof(rc_payload));
      printk("BLE_PPOG_RESET_COMPLETE_SENT\n");
      break;
    }
    case PPOG_TYPE_RESET_COMPLETE:
      // Both sides have reset; the session is open. Drive the app handshake.
      prv_session_opened(conn_handle);
      break;
    case PPOG_TYPE_DATA:
      prv_pp_feed(conn_handle, buf + 1, len - 1);
      prv_send(conn_handle, PPOG_TYPE_ACK, sn, NULL, 0);
      break;
    case PPOG_TYPE_ACK:
    default:
      break;
  }
  free(buf);
}
