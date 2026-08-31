/* SPDX-License-Identifier: Apache-2.0 */

// Port-side backends for the REAL launcher app (apps/system/launcher/default).
// Same idea as apps_port_glue.c: the launcher UI + glance code is compiled 1:1;
// the services behind the glances are RAM/no-op backends reflecting this
// firmware's actual state (no phone, no music, no notifications, no weather).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Zephyr vs Pebble sign_extend(); same dance as launcher_ui.c.
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend

#include "applib/battery_state_service.h"
#include "applib/template_string.h"

#include "pbl/services/app_glances/app_glance_service.h"
#include "pbl/services/bluetooth/bluetooth_ctl.h"
#include "pbl/services/clock.h"
#include "pbl/services/comm_session/session.h"
#include "pbl/services/notifications/alerts_private.h"
#include "pbl/services/notifications/do_not_disturb.h"
// item.h supplies TimelineItem ahead of notification_storage.h (shipping gets
// it via kernel/events.h; the port's reduced events shadow does not carry it).
#include "pbl/services/timeline/item.h"
#include "pbl/services/notifications/notification_storage.h"
#include "pbl/services/weather/weather_service.h"
#include "pbl/services/activity/health_util.h"
#include "pbl/services/activity/workout_service.h"

#include "process_management/app_install_manager.h"
#include "process_management/pebble_process_info.h"
#include "process_management/pebble_process_md.h"

#include "app_registry.h"

// ---------------------------------------------------------------------------
// task heap variants used by text_node/timeline_resources (task_malloc lives in
// watchface_sandboxed/src/port.c).
// ---------------------------------------------------------------------------
void *task_malloc(size_t bytes);
void *task_malloc_check(size_t bytes);

void *task_zalloc(size_t bytes) {
  void *ptr = task_malloc(bytes);
  if (ptr) {
    memset(ptr, 0, bytes);
  }
  return ptr;
}

void *task_zalloc_check(size_t bytes) {
  void *ptr = task_malloc_check(bytes);
  memset(ptr, 0, bytes);
  return ptr;
}

// ---------------------------------------------------------------------------
// Battery: qemu has no battery model wired; mirror the reference emulator's
// steady state (full, unplugged).
// ---------------------------------------------------------------------------
BatteryChargeState battery_state_service_peek(void) {
  return (BatteryChargeState) {
    .charge_percent = 100,
    .is_charging = false,
    .is_plugged = false,
  };
}

// ---------------------------------------------------------------------------
// Connectivity / alerts state: nothing connected, nothing muted.
// ---------------------------------------------------------------------------
// The reference emulator always has its qemu-serial system session up, so the
// settings glance shows the "connected" icon; mirror that with an opaque
// non-NULL handle (callers only null-check it).
CommSession *comm_session_get_system_session(void) {
  static int s_dummy_session;
  return (CommSession *)&s_dummy_session;
}
bool bt_ctl_is_airplane_mode_on(void) { return false; }
bool do_not_disturb_is_active(void) { return false; }
AlertMask alerts_get_mask(void) { return AlertMaskAllOn; }

// ---------------------------------------------------------------------------
// Notifications: empty store.
// ---------------------------------------------------------------------------
bool notification_storage_get(const Uuid *id, TimelineItem *item_out) {
  (void)id;
  (void)item_out;
  return false;
}

void notification_storage_iterate(bool (*iter_callback)(void *data, SerializedTimelineItemHeader *
                                                        header_id),
                                  void *data) {
  (void)iter_callback;
  (void)data;
}

// ---------------------------------------------------------------------------
// Weather / workout: no data.
// ---------------------------------------------------------------------------
WeatherLocationForecast *weather_service_create_default_forecast(void) { return NULL; }
void weather_service_destroy_default_forecast(WeatherLocationForecast *forecast) {
  (void)forecast;
}

bool workout_service_is_workout_ongoing(void) { return false; }
bool workout_service_get_current_workout_type(ActivitySessionType *type_out) {
  (void)type_out;
  return false;
}
bool workout_service_get_current_workout_info(int32_t *steps_out, int32_t *duration_s_out,
                                              int32_t *distance_m_out, int32_t *current_bpm_out,
                                              HRZone *current_hr_zone_out) {
  (void)steps_out;
  (void)duration_s_out;
  (void)distance_m_out;
  (void)current_bpm_out;
  (void)current_hr_zone_out;
  return false;
}

// ---------------------------------------------------------------------------
// Third-party app glance slices: none stored.
// ---------------------------------------------------------------------------
bool app_glance_service_get_current_slice(const Uuid *app_uuid, AppGlanceSliceInternal *slice_out) {
  (void)app_uuid;
  (void)slice_out;
  return false;
}

bool template_string_evaluate(const char *input_template_string, char *output, size_t output_size,
                              TemplateStringEvalConditions *reeval_cond,
                              const TemplateStringVars *vars, TemplateStringError *error) {
  (void)reeval_cond;
  (void)vars;
  (void)error;
  if (output && output_size) {
    strncpy(output, input_template_string ? input_template_string : "", output_size - 1);
    output[output_size - 1] = '\0';
  }
  return true;
}

// ---------------------------------------------------------------------------
// clock: timestamp formatter used by the alarms glance subtitle.
// ---------------------------------------------------------------------------
size_t clock_copy_time_string_timestamp(char *buffer, uint8_t size, time_t timestamp) {
  struct tm tm_val;
  gmtime_r(&timestamp, &tm_val);
  return clock_format_time(buffer, size, tm_val.tm_hour, tm_val.tm_min, true);
}

time_t time_util_get_midnight_of(time_t ts) {
  // The port's wall clock is UTC (see rtc_get_time_tm), so day boundaries are
  // plain UTC-day boundaries.
  return ts - (ts % (24 * 60 * 60));
}

// %a-only strftime (weekday abbreviation), all the launcher alarms glance uses.
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm_val) {
  static const char *const s_days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  if (!s || max == 0) {
    return 0;
  }
  const char *src = "";
  if (format && !strcmp(format, "%a") && tm_val->tm_wday >= 0 && tm_val->tm_wday < 7) {
    src = s_days[tm_val->tm_wday];
  }
  strncpy(s, src, max - 1);
  s[max - 1] = '\0';
  return strlen(s);
}

int health_util_format_hours_minutes_seconds(char *buffer, size_t buffer_size, int duration_s,
                                             bool leading_zero, void *i18n_owner) {
  (void)i18n_owner;
  const int hours = duration_s / 3600;
  const int minutes = (duration_s % 3600) / 60;
  const int seconds = duration_s % 60;
  return snprintf(buffer, buffer_size, leading_zero ? "%02d:%02d:%02d" : "%d:%02d:%02d", hours,
                  minutes, seconds);
}

// ---------------------------------------------------------------------------
// Misc launcher-stack link satisfiers.
// ---------------------------------------------------------------------------
#include "applib/graphics/gbitmap_sequence.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/timeline_resources.h"
#include "pbl/services/weather/weather_types.h"
#include "pbl/services/activity/activity.h"

void *_applib_type_zalloc_GBitmapSequence(void) { return task_zalloc(sizeof(GBitmapSequence)); }

// Glance slices never exist in the port, so attribute lookups take defaults.
const char *attribute_get_string(const AttributeList *attr_list, AttributeId id,
                                 char *default_value) {
  (void)attr_list;
  (void)id;
  return default_value;
}

TimelineResourceId weather_type_get_timeline_resource_id(WeatherType weather_type) {
  (void)weather_type;
  return TIMELINE_RESOURCE_TIMELINE_WEATHER;
}

bool workout_utils_find_ongoing_activity_session(ActivitySession *session_out) {
  (void)session_out;
  return false;
}

// ---------------------------------------------------------------------------
// app_install_manager: synthesize entries from the fw registry (generic glance
// SDK-version check path; only reached for non-system apps).
// ---------------------------------------------------------------------------
bool app_install_get_entry_for_install_id(AppInstallId install_id, AppInstallEntry *entry) {
  const size_t count = fw_app_registry_count();
  for (size_t i = 0; i < count; ++i) {
    const FwAppRegistryEntry *reg = fw_app_registry_get(i);
    if (!reg || reg->install_id != install_id || !reg->md) {
      continue;
    }
    const PebbleProcessMdSystem *md = (const PebbleProcessMdSystem *)reg->md;
    *entry = (AppInstallEntry) {
      .install_id = install_id,
      .type = AppInstallStorageFw,
      .visibility = reg->md->visibility,
      .process_type = reg->md->process_type,
      .uuid = reg->md->uuid,
      .sdk_version = { .major = PROCESS_INFO_CURRENT_SDK_VERSION_MAJOR,
                       .minor = PROCESS_INFO_CURRENT_SDK_VERSION_MINOR },
    };
    strncpy(entry->name, md->name ? md->name : "", sizeof(entry->name) - 1);
    return true;
  }
  return false;
}
