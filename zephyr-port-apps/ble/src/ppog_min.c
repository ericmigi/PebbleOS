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

#define PPOG_TYPE_DATA 0x0
#define PPOG_TYPE_ACK 0x1
#define PPOG_TYPE_RESET_REQUEST 0x2
#define PPOG_TYPE_RESET_COMPLETE 0x3

#define PPOG_TYPE(b) ((b) & 0x7)
#define PPOG_SN(b) ((b) >> 3)
#define PPOG_HDR(type, sn) ((uint8_t)((type) | ((sn) << 3)))

#define PPOG_WINDOW 4

static uint8_t s_pp_buf[1024];
static uint16_t s_pp_len;

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

// Reassemble the Pebble Protocol stream: [be16 length][be16 endpoint][payload].
static void prv_pp_feed(const uint8_t *data, uint16_t len) {
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
    memmove(s_pp_buf, s_pp_buf + total, s_pp_len - total);
    s_pp_len -= total;
  }
}

// --- bt_driver_cb_ppog_reversed_* hooks (called from ppog_reversed_service) ---

void bt_driver_cb_ppog_reversed_subscribed(const BTDeviceInternal *device,
                                           uint16_t conn_handle) {
  (void)device;
  s_pp_len = 0;
  printk("BLE_PPOG_SUBSCRIBED conn=%u\n", (unsigned int)conn_handle);
}

void bt_driver_cb_ppog_reversed_unsubscribed(uint16_t conn_handle) {
  s_pp_len = 0;
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
      const uint8_t rc_payload[2] = {PPOG_WINDOW, PPOG_WINDOW};
      prv_send(conn_handle, PPOG_TYPE_RESET_COMPLETE, 0, rc_payload,
               sizeof(rc_payload));
      printk("BLE_PPOG_RESET_COMPLETE_SENT\n");
      break;
    }
    case PPOG_TYPE_DATA:
      prv_pp_feed(buf + 1, len - 1);
      prv_send(conn_handle, PPOG_TYPE_ACK, sn, NULL, 0);
      break;
    case PPOG_TYPE_ACK:
    case PPOG_TYPE_RESET_COMPLETE:
    default:
      break;
  }
  free(buf);
}
