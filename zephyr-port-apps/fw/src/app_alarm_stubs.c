/* SPDX-License-Identifier: Apache-2.0 */

// Minimal alarm service for the ported Alarms app. Serves a single read-only
// canned alarm (07:00, every day) so the real Alarms app launches straight into
// its real alarm-list menu (the non-empty path in alarms.c:prv_handle_init) and
// renders the real menu UI. Edits/creates/deletes are inert; nothing persists
// and no alarm ever fires.
//
// ponytail: one hard-coded alarm, no storage / scheduling /
// PEBBLE_ALARM_CLOCK_EVENT. Returning >=1 alarm renders the canonical alarm-list
// menu with zero extra applib closure (the empty-list path would instead open
// the new-alarm editor, pulling the whole time-selection/day-picker closure).
// Upgrade path: port src/fw/services/normal/alarms/alarm.c (blob_db-backed store
// + timers) to replace this whole file.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "pbl/services/alarms/alarm.h"

#define STUB_ALARM_ID 0
#define STUB_ALARM_HOUR 7
#define STUB_ALARM_MINUTE 0

static bool s_stub_scheduled_days[DAYS_PER_WEEK] = {true, true, true, true, true, true, true};

static AlarmInfo prv_stub_alarm(void) {
  return (AlarmInfo){
    .hour = STUB_ALARM_HOUR,
    .minute = STUB_ALARM_MINUTE,
    .kind = ALARM_KIND_EVERYDAY,
    .scheduled_days = &s_stub_scheduled_days,
    .enabled = true,
    .is_smart = false,
    .sound_enabled = true,
    .vibrate_enabled = true,
    .tone = AlarmTone_Reveille,
  };
}

AlarmId alarm_create(const AlarmInfo *info) {
  (void)info;
  return ALARM_INVALID_ID;
}

void alarm_set_time(AlarmId id, int hour, int minute) {
  (void)id;
  (void)hour;
  (void)minute;
}

void alarm_set_kind(AlarmId id, AlarmKind kind) {
  (void)id;
  (void)kind;
}

void alarm_set_custom(AlarmId id, const bool scheduled_days[DAYS_PER_WEEK]) {
  (void)id;
  (void)scheduled_days;
}

void alarm_set_smart(AlarmId id, bool smart) {
  (void)id;
  (void)smart;
}

void alarm_set_sound_enabled(AlarmId id, bool enabled) {
  (void)id;
  (void)enabled;
}

void alarm_set_vibrate_enabled(AlarmId id, bool enabled) {
  (void)id;
  (void)enabled;
}

void alarm_set_tone(AlarmId id, AlarmTone tone) {
  (void)id;
  (void)tone;
}

bool alarm_get_info(AlarmId id, AlarmInfo *info_out) {
  if (id != STUB_ALARM_ID || !info_out) {
    return false;
  }
  *info_out = prv_stub_alarm();
  info_out->scheduled_days = NULL;  // matches shipping contract (see alarm.h)
  return true;
}

AlarmId alarm_get_most_recent_id(void) { return ALARM_INVALID_ID; }

bool alarm_get_custom_days(AlarmId id, bool scheduled_days[DAYS_PER_WEEK]) {
  if (id != STUB_ALARM_ID || !scheduled_days) {
    return false;
  }
  memcpy(scheduled_days, s_stub_scheduled_days, sizeof(s_stub_scheduled_days));
  return true;
}

void alarm_set_enabled(AlarmId id, bool enable) {
  (void)id;
  (void)enable;
}

void alarm_delete(AlarmId id) { (void)id; }

bool alarm_get_enabled(AlarmId id) { return id == STUB_ALARM_ID; }

bool alarm_get_hours_minutes(AlarmId id, int *hour_out, int *minute_out) {
  if (id != STUB_ALARM_ID) {
    return false;
  }
  if (hour_out) {
    *hour_out = STUB_ALARM_HOUR;
  }
  if (minute_out) {
    *minute_out = STUB_ALARM_MINUTE;
  }
  return true;
}

bool alarm_get_kind(AlarmId id, AlarmKind *kind_out) {
  if (id != STUB_ALARM_ID) {
    return false;
  }
  if (kind_out) {
    *kind_out = ALARM_KIND_EVERYDAY;
  }
  return true;
}

bool alarm_get_next_enabled_alarm(time_t *next_alarm_time_out) {
  (void)next_alarm_time_out;
  return false;
}

bool alarm_is_next_enabled_alarm_smart(void) { return false; }

bool alarm_get_time_until(AlarmId id, time_t *time_out) {
  (void)id;
  (void)time_out;
  return false;
}

void alarm_set_snooze_alarm(void) {}

uint16_t alarm_get_snooze_delay(void) { return 10; }

void alarm_set_snooze_delay(uint16_t delay_m) { (void)delay_m; }

void alarm_dismiss_alarm(void) {}

void alarm_for_each(AlarmForEach cb, void *context) {
  if (cb) {
    AlarmInfo info = prv_stub_alarm();
    cb(STUB_ALARM_ID, &info, context);
  }
}

bool alarm_can_schedule(void) { return true; }

void alarm_handle_clock_change(void) {}

void alarm_init(void) {}

void alarm_service_enable_alarms(bool enable) { (void)enable; }

const char *alarm_get_string_for_kind(AlarmKind kind, bool all_caps) {
  (void)kind;
  (void)all_caps;
  return "";
}

void alarm_get_string_for_custom(bool scheduled_days[DAYS_PER_WEEK], char *alarm_day_text) {
  (void)scheduled_days;
  if (alarm_day_text) {
    alarm_day_text[0] = '\0';
  }
}


// Report "already opened at the current version" so the app skips its first-run
// expandable dialog and goes straight to the alarm list / new-alarm editor.
