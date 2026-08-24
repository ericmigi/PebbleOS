/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

typedef uint64_t RtcTicks;

#define RTC_TICKS_HZ 1000u

RtcTicks rtc_get_ticks(void);
