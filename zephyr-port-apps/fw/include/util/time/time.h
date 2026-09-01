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

// Midnight (UTC) of the day containing ts (fw/src/shell_glue.c).
time_t time_util_get_midnight_of(time_t ts);

// Zephyr's minimal libc has no strftime; the launcher alarms glance formats
// weekday names with it. %a-only implementation in fw/src/shell_glue.c.
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

// Zephyr's minimal libc also lacks mktime; the port's wall clock is UTC so it
// is timegm (fw/src/apps_port_glue.c).
time_t mktime(struct tm *tm_val);

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

// TimezoneInfo mirrors the shipping struct (settings/time.c + the timezone
// database service use it; the port never programs RTC registers with it).
#define TZ_LEN 6
typedef struct TimezoneInfo {
  char tm_zone[TZ_LEN - 1];
  uint8_t dst_id;
  int16_t timezone_id;
  int32_t tm_gmtoff;
  time_t dst_start;
  time_t dst_end;
} TimezoneInfo;

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

void time_util_split_seconds_into_parts(uint32_t seconds, uint32_t *day_part,
                                        uint32_t *hour_part, uint32_t *minute_part,
                                        uint32_t *second_part);
uint32_t time_get_uptime_seconds(void);
