/* SPDX-License-Identifier: Apache-2.0 */

// Port closure for the timeline / blob_db item store. The port renders wall
// clock as UTC everywhere (see rtc_get_time_tm / clock glue), so local time IS
// UTC: the conversions are identities. timeline_item.c uses these to normalize
// all-day / floating pin timestamps.
// ponytail: identity because the port has no timezone service. Wire real
// offsets if timezone-aware pins matter.

#include <time.h>

time_t time_local_to_utc(time_t local_time) { return local_time; }
time_t time_utc_to_local(time_t utc_time) { return utc_time; }

#include <stdbool.h>

// --- pin_db insert-path closure (brick 2) ----------------------------------
// pin_db_insert_item pulls app-cache / app-fetch / blob-db-sync decisions and a
// uuid RNG. The port has no phone sync, no app cache, and no app fetch, so
// these are inert; rand32 wraps the hardware RNG.
// ponytail: stubs until phone sync / app install / timeline data sources land.
#include "pbl/services/blob_db/api.h"
#include "process_management/app_install_manager.h"
#include "pbl/util/uuid.h"

bool rng_rand(uint32_t *rand_out);

uint32_t rand32(void) {
  uint32_t v = 0;
  rng_rand(&v);
  return v;
}

const char *timeline_get_private_data_source(Uuid *parent_id) {
  (void)parent_id;
  return NULL;
}

bool app_install_id_from_system(AppInstallId id) {
  // System (built-in) apps use negative install ids; a pin owned by one never
  // needs to be fetched from the phone.
  return id < 0;
}

bool app_cache_entry_exists(AppInstallId app_id) {
  (void)app_id;
  return true;  // pretend cached -> never triggers an app fetch
}

void app_cache_app_launched(AppInstallId app_id) { (void)app_id; }

void blob_db_event_put(BlobDBEventType type, BlobDBId db_id, const uint8_t *key, int key_len) {
  (void)type;
  (void)db_id;
  (void)key;
  (void)key_len;
}

status_t blob_db_sync_record(BlobDBId db_id, const void *key, int key_len, time_t last_updated) {
  (void)db_id;
  (void)key;
  (void)key_len;
  (void)last_updated;
  return S_SUCCESS;
}

// pin_db_delete cascades to child reminders; the port has no reminder_db, so
// there is nothing to delete.
// ponytail: stub until reminder_db is ported.
#include "pbl/services/blob_db/reminder_db.h"
status_t reminder_db_delete_with_parent(const TimelineItemId *parent_id) {
  (void)parent_id;
  return S_SUCCESS;
}

// layout_layer.c's verifier table references every layout family; the port only
// compiles alarm_layout (+ notification_layout). The other families have no pins
// in the port, so their verifiers are permissive stubs (accept). alarm and
// notification use their real verifiers.
#include <stdbool.h>
bool calendar_layout_verify(bool existing_attributes[]) { (void)existing_attributes; return true; }
bool generic_layout_verify(bool existing_attributes[]) { (void)existing_attributes; return true; }
bool health_layout_verify(bool existing_attributes[]) { (void)existing_attributes; return true; }
bool sports_layout_verify(bool existing_attributes[]) { (void)existing_attributes; return true; }
bool weather_layout_verify(bool existing_attributes[]) { (void)existing_attributes; return true; }
