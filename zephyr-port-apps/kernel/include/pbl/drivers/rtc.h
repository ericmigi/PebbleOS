/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>
#include <time.h>

typedef uint64_t RtcTicks;

#define RTC_TICKS_HZ CONFIG_SYS_CLOCK_TICKS_PER_SEC

RtcTicks rtc_get_ticks(void);
time_t rtc_get_time(void);
void rtc_get_time_ms(time_t *seconds, uint16_t *milliseconds);

// Used by timeline item deserialize for all-day/floating items only; the notif
// render path never hits that branch, so the port provides an identity stub.
time_t time_local_to_utc(time_t local_time);
