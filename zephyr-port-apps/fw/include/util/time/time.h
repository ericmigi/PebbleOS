/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port stub shadowing shipping src/fw/util/time/time.h. The shipping header
// redeclares localtime_r/gmtime_r/localtime/gmtime WITHOUT the `restrict`
// qualifiers Zephyr's minimal libc uses, which the compiler rejects as
// conflicting types (and drags in a second `struct tm`). No port source needs
// the shipping extras (TimezoneInfo, time_t_to_string, ...); a system app only
// wants the standard time.h types. A future ported app that needs those extras
// should reconcile the restrict qualifiers in the real header instead of adding
// them here.
#include <stdint.h>
#include <time.h>

uint16_t time_ms(time_t *tloc, uint16_t *out_ms);
time_t time_local_to_utc(time_t local_time);

#define DAYS_PER_WEEK 7
#define MONTHS_PER_YEAR 12
#define MS_PER_SECOND (1000)
#define SECONDS_PER_MINUTE (60)
#define MS_PER_MINUTE (MS_PER_SECOND * SECONDS_PER_MINUTE)
#define MINUTES_PER_HOUR (60)
#define SECONDS_PER_HOUR (SECONDS_PER_MINUTE * MINUTES_PER_HOUR)
#define HOURS_PER_DAY (24)
#define MINUTES_PER_DAY (HOURS_PER_DAY * MINUTES_PER_HOUR)
#define SECONDS_PER_DAY (MINUTES_PER_DAY * SECONDS_PER_MINUTE)
#define SECONDS_PER_WEEK (SECONDS_PER_DAY * DAYS_PER_WEEK)

#define IS_WEEKDAY(d) ((d) >= Monday && (d) <= Friday)
#define IS_WEEKEND(d) ((d) == Saturday || (d) == Sunday)

// DayInWeek is pulled in by pbl/services/activity/activity.h (health settings).
// Mirrors the shipping util/time/time.h enum.
typedef enum DayInWeek {
  Sunday = 0,
  Monday,
  Tuesday,
  Wednesday,
  Thursday,
  Friday,
  Saturday,
} DayInWeek;
