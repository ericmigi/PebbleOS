/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <time.h>

typedef enum {
  SECOND_UNIT = 1 << 0,
  MINUTE_UNIT = 1 << 1,
  HOUR_UNIT = 1 << 2,
  DAY_UNIT = 1 << 3,
  MONTH_UNIT = 1 << 4,
  YEAR_UNIT = 1 << 5,
} TimeUnits;

typedef void (*TickHandler)(struct tm *tick_time, TimeUnits units_changed);

void tick_timer_service_subscribe(TimeUnits tick_units, TickHandler handler);
void tick_timer_service_unsubscribe(void);
