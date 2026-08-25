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
#include <time.h>

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
