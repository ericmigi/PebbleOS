/* SPDX-License-Identifier: Apache-2.0 */

// Leaf stubs for the timeline notification layout engine: the phone-image,
// pinned-app-icon and pin/reminder DB lookups it references but that the port
// (no phone imaging, no blob_db, no reminders yet) does not provide. A plain
// notification never hits the reminder path; the notification card resolves its
// icon to a system default without a per-app icon bank. All inert.
//
// ponytail: replace with the real notification_image / pin_db / app_install
// services when phone images, reminders, or per-app notification icons land.

#include <stdbool.h>
#include <stddef.h>

#include "system/status_codes.h"
#include "process_management/app_install_manager.h"
#include "pbl/services/timeline/item.h"
#include "util/uuid.h"

struct GBitmap;

const struct GBitmap *notification_image_lock(const Uuid *item_id) {
  (void)item_id;
  return NULL;
}

void notification_image_unlock(void) {}

bool notification_image_is_pending(const Uuid *item_id) {
  (void)item_id;
  return false;
}

status_t pin_db_get(const TimelineItemId *id, TimelineItem *pin) {
  (void)id;
  (void)pin;
  return E_DOES_NOT_EXIST;
}

ResAppNum app_install_get_app_icon_bank(const AppInstallEntry *entry) {
  (void)entry;
  return 0;
}

void timeline_item_free_allocated_buffer(TimelineItem *item) {
  (void)item;
}

// Lifted from lib/util/string.c (whole file needs ctype under the minimal libc;
// this is the only symbol the timeline layout engine uses).
const char *string_strip_leading_whitespace(const char *string) {
  const char *result_string = string;
  while (*result_string != '\0') {
    if (*result_string != ' ' && *result_string != '\n') {
      break;
    }
    result_string++;
  }
  return result_string;
}
