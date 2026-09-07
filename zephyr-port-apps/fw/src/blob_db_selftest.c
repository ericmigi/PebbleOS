/* SPDX-License-Identifier: Apache-2.0 */

// Smoke test for the blob_db pin store (pin_db on timeline_item_storage, both
// PFS/settings_file backed). Builds a real TimelineItem pin, inserts it via
// pin_db_insert_item, reads it back via pin_db_get, checks the id round-trips.
// Runs once at boot, logs PASS/FAIL. Brick 2 of the blob_db port.
// ponytail: remove once a real producer (alarm_pin) drives pin_db.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "pbl/services/blob_db/pin_db.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/util/uuid.h"

time_t rtc_get_time(void);

void fw_blob_db_selftest(void) {
  pin_db_init();

  AttributeList attr_list = {0};
  attribute_list_add_cstring(&attr_list, AttributeIdTitle, "Test Pin");
  TimelineItemActionGroup action_group = {0};
  TimelineItem *pin = timeline_item_create_with_attributes(
      rtc_get_time(), 1 /* duration */, TimelineItemTypePin, LayoutIdGeneric, &attr_list,
      &action_group);
  if (!pin) {
    printk("BLOBDB_SELFTEST create=NULL => FAIL\n");
    return;
  }
  const Uuid id = pin->header.id;

  status_t ins = pin_db_insert_item(pin);

  int glen = pin_db_get_len((const uint8_t *)&id, sizeof(Uuid));
  printk("BLOBDB_DBG ins=%d glen=%d id0=%d id1=%d layout=%d\n", (int)ins, glen, id.byte0, id.byte1, pin->header.layout);

  TimelineItem read = {0};
  status_t got = pin_db_get(&id, &read);
  bool id_ok = (got == S_SUCCESS) && (memcmp(&read.header.id, &id, sizeof(Uuid)) == 0);

  bool pass = (ins == S_SUCCESS && id_ok);
  printk("BLOBDB_SELFTEST(pin_db) insert=%d get=%d id_ok=%d => %s\n", (int)ins, (int)got, id_ok,
         pass ? "PASS" : "FAIL");

  if (got == S_SUCCESS) {
    timeline_item_free_allocated_buffer(&read);
  }
  attribute_list_destroy_list(&attr_list);
  timeline_item_destroy(pin);
}
