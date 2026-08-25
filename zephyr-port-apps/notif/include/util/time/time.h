/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MS_PER_SECOND 1000
#define SECONDS_PER_MINUTE 60
#define MS_PER_MINUTE (MS_PER_SECOND * SECONDS_PER_MINUTE)
#define MINUTES_PER_HOUR 60
#define SECONDS_PER_HOUR (SECONDS_PER_MINUTE * MINUTES_PER_HOUR)
#define HOURS_PER_DAY 24
#define SECONDS_PER_DAY (SECONDS_PER_HOUR * HOURS_PER_DAY)

time_t time_util_get_midnight_of(time_t ts);
bool time_util_range_spans_day(time_t start, time_t end, time_t start_of_day);

// Only reached for all-day/floating timeline items; notifications are neither,
// so the shim is an identity pass-through.
time_t time_local_to_utc(time_t local_time);

