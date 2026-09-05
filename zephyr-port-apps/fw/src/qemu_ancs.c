/* SPDX-License-Identifier: Apache-2.0 */

// ANCS (Apple Notification Center Service) decode on the Zephyr port.
//
// On real hardware the iPhone delivers a notification in two steps over BLE
// GATT: the Notification Source characteristic notifies an 8-byte event, the
// watch issues a Get-Notification-Attributes command on the Control Point, and
// the Data Source characteristic notifies the attribute-dictionary response.
// fw_ancs_feed takes that final Data Source response blob (the exact bytes the
// GATT layer would hand up) and runs the shipping ancs_util parser on it, then
// routes Title/Subtitle/Message to the port's notification display.
//
// QEMU has no BLE radio, so the response is injected over the QEMU SPP transport
// (qemu_notif_rx.c, proto QEMU_PROTO_ANCS) instead of arriving from a phone.
// This exercises the real ANCS parsing + display seam; the GATT service
// discovery / control-point round-trip / pairing is the hardware-only remainder
// (needs the BLE stack ported into the fw app).

#include <zephyr/sys/printk.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "comm/ble/kernel_le_client/ancs/ancs_types.h"
#include "comm/ble/kernel_le_client/ancs/ancs_util.h"

void fw_notification_show(const char *title, const char *subtitle, const char *body,
                          uint32_t icon);

static void prv_copy_attr(char *dst, size_t cap, const ANCSAttribute *attr) {
  dst[0] = '\0';
  if (!attr || attr->length == 0) {
    return;
  }
  const size_t n = (attr->length < cap - 1) ? attr->length : cap - 1;
  memcpy(dst, attr->value, n);  // ANCS values are not null-terminated
  dst[n] = '\0';
}

// resp: a full ANCS Get-Notification-Attributes response
// [command_id:1][notification_uid:4][ (attr_id:1 length:2 LE value:length) ... ]
void fw_ancs_feed(const uint8_t *resp, size_t len) {
  const size_t hdr = sizeof(GetNotificationAttributesMsg);  // command_id + uid
  if (len <= hdr) {
    printk("ANCS_RX short len=%u\n", (unsigned)len);
    return;
  }

  ANCSAttribute *attrs[NUM_FETCHED_NOTIF_ATTRIBUTES] = {0};
  bool error = false;
  ancs_util_get_attr_ptrs(resp + hdr, len - hdr, s_fetched_notif_attributes,
                          NUM_FETCHED_NOTIF_ATTRIBUTES, attrs, &error);
  if (error) {
    printk("ANCS_RX parse error\n");
    return;
  }

  char title[TITLE_MAX_LENGTH + 1];
  char subtitle[SUBTITLE_MAX_LENGTH + 1];
  char body[MESSAGE_MAX_LENGTH + 1];
  prv_copy_attr(title, sizeof(title), attrs[FetchedNotifAttributeIndexTitle]);
  prv_copy_attr(subtitle, sizeof(subtitle), attrs[FetchedNotifAttributeIndexSubtitle]);
  prv_copy_attr(body, sizeof(body), attrs[FetchedNotifAttributeIndexMessage]);

  printk("ANCS_RX title=\"%s\" body=\"%s\"\n", title, body);
  fw_notification_show(title, subtitle, body, 0 /* icon: ANCS fallback */);
}
