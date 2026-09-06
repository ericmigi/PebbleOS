/* SPDX-License-Identifier: Apache-2.0 */

// Port closure for the real alarm service (src/fw/services/alarms/alarm.c).
// Everything alarm.c needs (settings_file, new_timer, system_task, mutex,
// analytics, activity, timeline) is already compiled into the fw app EXCEPT:
//   - cron (src/fw/services/cron/service.c) drags in the timezone/DST service
//     and Pebble's extended struct tm (tm_gmtoff) + mktime, none of which the
//     port's UTC-only libc has. The port renders wall-clock as UTC everywhere,
//     so a compact UTC cron implemented here is exact for the port and avoids
//     that whole rabbit hole.
//   - alarm_pin.c (timeline pins for alarms) — not needed for create/persist/
//     fire; stubbed no-op.
//
// ponytail: single active cron job (alarm.c only ever schedules
// s_next_alarm_cron), one static new_timer. cron here handles minute/hour/wday
// (mday/month are always CRON_*_ANY for alarms). Upgrade to the real cron
// service if timezone-correct scheduling or multiple concurrent jobs matter.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include <pebbleos/cron.h>
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/alarms/alarm.h"

#include "pbl/util/uuid.h"

#define SECONDS_PER_DAY (24 * 60 * 60)

time_t rtc_get_time(void);

// --- UTC cron ---------------------------------------------------------------

// Next fire time >= (or > when !may_be_instant) `from` matching job's
// hour:minute on an allowed weekday, with offset_seconds applied after.
static time_t prv_cron_next(const CronJob *job, time_t from) {
  int hour = job->hour < 0 ? 0 : job->hour;
  int minute = job->minute < 0 ? 0 : job->minute;
  uint8_t wday_mask = job->flags & 0x7f;  // 0 == WDAY_ANY
  bool may_be_instant = (job->flags & 0x80) != 0;

  // UTC midnight of `from` (from may be negative in tests; keep modulo positive).
  time_t midnight = from - (((from % SECONDS_PER_DAY) + SECONDS_PER_DAY) % SECONDS_PER_DAY);

  for (int i = 0; i <= 8; i++) {
    time_t day_mid = midnight + (time_t)i * SECONDS_PER_DAY;
    struct tm t;
    gmtime_r(&day_mid, &t);
    bool allowed = (wday_mask == 0) || (wday_mask & (1 << t.tm_wday));
    if (!allowed) {
      continue;
    }
    time_t cand = day_mid + (time_t)hour * 3600 + (time_t)minute * 60 + job->offset_seconds;
    if (cand > from || (may_be_instant && cand == from)) {
      return cand;
    }
  }
  // Fallback: a week out (should be unreachable for a valid weekday mask).
  return from + 7 * SECONDS_PER_DAY;
}

time_t cron_job_get_execute_time_from_epoch(const CronJob *job, time_t local_epoch) {
  return prv_cron_next(job, local_epoch);
}

time_t cron_job_get_execute_time(const CronJob *job) {
  return prv_cron_next(job, rtc_get_time());
}

static TimerID s_cron_timer;
static CronJob *s_cron_job;

static void prv_cron_timer_cb(void *data) {
  CronJob *job = data;
  if (job && job->cb) {
    job->cb(job, job->cb_data);
  }
}

time_t cron_job_schedule(CronJob *job) {
  time_t now = rtc_get_time();
  time_t exec = prv_cron_next(job, now);
  job->cached_execute_time = exec;
  if (s_cron_timer == 0) {
    s_cron_timer = new_timer_create();
  }
  s_cron_job = job;
  uint32_t ms = exec > now ? (uint32_t)(exec - now) * 1000 : 1;
  new_timer_start(s_cron_timer, ms, prv_cron_timer_cb, job, 0);
  return exec;
}

bool cron_job_unschedule(CronJob *job) {
  if (s_cron_job != job) {
    return false;
  }
  s_cron_job = NULL;
  if (s_cron_timer != 0) {
    new_timer_stop(s_cron_timer);
  }
  return true;
}

// --- alarm timeline pins (milestone 2) --------------------------------------

void alarm_pin_add(time_t alarm_time, AlarmId id, AlarmType type, AlarmKind kind, Uuid *uuid_out) {
  (void)alarm_time;
  (void)id;
  (void)type;
  (void)kind;
  (void)uuid_out;
}

void alarm_pin_remove(Uuid *alarm_id) { (void)alarm_id; }

// --- activity (smart alarm) + timeline refresh stubs ------------------------
// No activity tracking in the port -> smart alarms behave as basic; no sleep
// metrics. timeline_event_refresh is a no-op until timeline pins land (ms 2).
#include "pbl/services/activity/activity.h"
#include "pbl/services/timeline/event.h"

bool activity_tracking_on(void) { return false; }

bool activity_get_metric(ActivityMetric metric, uint32_t history_len, int32_t *history) {
  (void)metric;
  (void)history_len;
  (void)history;
  return false;
}

void timeline_event_refresh(void) {}
