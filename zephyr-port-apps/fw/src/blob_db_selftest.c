/* SPDX-License-Identifier: Apache-2.0 */

// Smoke test for the timeline_item_storage foundation (the PFS/settings_file
// keyed store that pin_db / notif_db / reminder_db sit on). Inserts a minimal
// valid serialized timeline item, reads it back, and deletes it. Runs once at
// boot and logs PASS/FAIL. First brick of the blob_db port.
// ponytail: remove once pin_db + a real pin round-trip is wired.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "pbl/services/blob_db/timeline_item_storage.h"
#include "pbl/services/timeline/item.h"
#include "pbl/util/uuid.h"

time_t rtc_get_time(void);

void fw_blob_db_selftest(void) {
  static TimelineItemStorage s_storage;
  char name[] = "tis_test";
  timeline_item_storage_init(&s_storage, name, 4096, 0 /* no max age */);

  const Uuid key = (Uuid){.byte0 = 0x11, .byte1 = 0x22, .byte15 = 0xAB};

  // Minimal valid serialized item: header only, no attributes/actions/payload.
  SerializedTimelineItemHeader hdr = {0};
  hdr.common.id = key;
  hdr.common.timestamp = rtc_get_time();
  hdr.common.duration = 1;
  hdr.common.layout = 1;  // any valid id; the port's layout_verify accepts all
  hdr.payload_length = 0;
  hdr.num_attributes = 0;
  hdr.num_actions = 0;

  const int val_len = sizeof(hdr);
  status_t rv = timeline_item_storage_insert(&s_storage, (const uint8_t *)&key, sizeof(key),
                                             (const uint8_t *)&hdr, val_len, false);
  int len = timeline_item_storage_get_len(&s_storage, (const uint8_t *)&key, sizeof(key));
  bool nonempty = !timeline_item_storage_is_empty(&s_storage);

  SerializedTimelineItemHeader out = {0};
  status_t rr = timeline_item_storage_read(&s_storage, (const uint8_t *)&key, sizeof(key),
                                           (uint8_t *)&out, val_len);
  // flags/status are stored inverted; compare the id + timestamp which are not.
  bool id_ok = (memcmp(&out.common.id, &key, sizeof(Uuid)) == 0);
  bool ts_ok = (out.common.timestamp == hdr.common.timestamp);

  status_t rd = timeline_item_storage_delete(&s_storage, (const uint8_t *)&key, sizeof(key));
  bool empty_after = timeline_item_storage_is_empty(&s_storage);

  bool pass = (rv == S_SUCCESS && len == val_len && nonempty && rr == S_SUCCESS && id_ok &&
               ts_ok && rd == S_SUCCESS && empty_after);
  printk("BLOBDB_SELFTEST insert=%d len=%d nonempty=%d read=%d id_ok=%d ts_ok=%d del=%d "
         "empty_after=%d => %s\n",
         (int)rv, len, nonempty, (int)rr, id_ok, ts_ok, (int)rd, empty_after,
         pass ? "PASS" : "FAIL");

  timeline_item_storage_deinit(&s_storage);
}
