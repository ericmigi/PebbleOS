/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// NimBLE to the ESP32-C3's own controller, over its virtual HCI: whole H4 packets both ways,
// modeled on ESP-IDF's esp_nimble_hci.c. The controller calls back in its own thread.
//
// The controller is used only once armed (btc arm, then a reset) and if its last bring-up did not
// fail; the transport answers like the fake one otherwise, so the host still synchronises. That is
// decided once per boot.

#include <stdint.h>
#include <string.h>

#include <pbl/kernel/sem.h>
#include <pbl/logging/logging.h>

#include <esp32c3_bt.h>

// clang-format off
#include <os/os_mbuf.h>
// clang-format on
#include "hci_fake.h"

#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>
#include <nimble/transport_impl.h>

PBL_LOG_MODULE_DECLARE(nimble, CONFIG_NIMBLE_LOG_LEVEL);

#define VHCI_TIMEOUT_MS 2000U

static bool s_real;
static PBL_SEM_DEFINE(s_send_avail, 1, 1);

static void prv_send_available(void) {
  pbl_sem_give(&s_send_avail);
}

static int prv_recv(uint8_t *data, uint16_t len) {
  if (len < 1U) {
    return 0;
  }

  if (data[0] == HCI_H4_EVT) {
    const uint16_t totlen = sizeof(struct ble_hci_ev) + (len > 2U ? data[2] : 0U);
    bool discardable;
    struct ble_hci_ev *ev;

    if (len < 3U || totlen > len - 1U || totlen > MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE)) {
      PBL_LOG_ERR("HCI event of %u bytes dropped", len);
      return 0;
    }
    // Advertising reports may be dropped when out of buffers
    discardable =
        data[1] == BLE_HCI_EVCODE_LE_META && len > 3U && data[3] == BLE_HCI_LE_SUBEV_ADV_RPT;
    ev = ble_transport_alloc_evt(discardable);
    if (ev == NULL) {
      if (!discardable) {
        PBL_LOG_ERR("EVT alloc failed");
      }
      return 0;
    }
    memcpy(ev, &data[1], totlen);
    (void)ble_transport_to_hs_evt(ev);
  } else if (data[0] == HCI_H4_ACL) {
    struct os_mbuf *om;

    // The header, then a payload of at most BLE_TRANSPORT_ACL_SIZE, its length in bytes 3-4
    if (len - 1U < BLE_HCI_DATA_HDR_SZ ||
        len - 1U - BLE_HCI_DATA_HDR_SZ > MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE) ||
        (uint16_t)(data[3] | (data[4] << 8)) != len - 1U - BLE_HCI_DATA_HDR_SZ) {
      PBL_LOG_ERR("HCI ACL of %u bytes dropped", len);
      return 0;
    }
    om = ble_transport_alloc_acl_from_ll();
    if (om == NULL) {
      PBL_LOG_ERR("ACL alloc failed");
      return 0;
    }
    if (os_mbuf_append(om, &data[1], len - 1U) != 0) {
      os_mbuf_free_chain(om);
      return 0;
    }
    (void)ble_transport_to_hs_acl(om);
  }

  return 0;
}

static const struct esp32c3_bt_vhci_callbacks s_vhci_cb = {
  .send_available = prv_send_available,
  .recv = prv_recv,
};

void ble_transport_ll_init(void) {
  int ret;

  if (!esp32c3_bt_armed()) {
    PBL_LOG_INFO("ESP32-C3 controller not armed: fake HCI");
    return;
  }

  ret = esp32c3_bt_start();
  if (ret == 0) {
    ret = esp32c3_bt_vhci_register(&s_vhci_cb);
  }
  if (ret != 0) {
    PBL_LOG_ERR("ESP32-C3 controller unavailable (%d): fake HCI", ret);
    return;
  }

  s_real = true;
}

static int prv_send(uint8_t *pkt, uint16_t len) {
  if (pbl_sem_take(&s_send_avail, PBL_MSEC(VHCI_TIMEOUT_MS)) != 0) {
    return BLE_ERR_MEM_CAPACITY;
  }
  esp32c3_bt_vhci_send(pkt, len);
  return 0;
}

int ble_transport_to_ll_cmd_impl(void *buf) {
  struct ble_hci_cmd *cmd = buf;
  uint8_t pkt[1U + sizeof(*cmd) + UINT8_MAX];
  uint16_t len;
  int rc;

  if (!s_real) {
    return hci_fake_cmd(buf);
  }

  len = (uint16_t)(sizeof(*cmd) + cmd->length);
  pkt[0] = HCI_H4_CMD;
  memcpy(&pkt[1], cmd, len);
  ble_transport_free(buf);

  rc = prv_send(pkt, len + 1U);
  if (rc != 0) {
    PBL_LOG_ERR("HCI command not sent (%d)", rc);
  }
  return rc;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om) {
  uint8_t pkt[1U + MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE) + BLE_HCI_DATA_HDR_SZ];
  uint16_t len = OS_MBUF_PKTLEN(om);
  int rc = 0;

  if (s_real && len > 0U && len <= sizeof(pkt) - 1U) {
    pkt[0] = HCI_H4_ACL;
    (void)os_mbuf_copydata(om, 0, len, &pkt[1]);
    rc = prv_send(pkt, len + 1U);
  }
  os_mbuf_free_chain(om);

  return rc;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om) {
  os_mbuf_free_chain(om);
  return 0;
}
