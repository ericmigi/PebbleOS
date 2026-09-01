/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>
#include <time.h>

typedef uint64_t RtcTicks;

#define RTC_TICKS_HZ CONFIG_SYS_CLOCK_TICKS_PER_SEC

RtcTicks rtc_get_ticks(void);
time_t rtc_get_time(void);
void rtc_get_time_ms(time_t *seconds, uint16_t *milliseconds);
void rtc_get_time_tm(struct tm *time_tm);
bool rtc_is_timezone_set(void);
