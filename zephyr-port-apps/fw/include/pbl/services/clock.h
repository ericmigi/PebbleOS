/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

bool clock_is_24h_style(void);

#include <stddef.h>
#include <stdint.h>

// Buffer lengths for formatted time strings, from shipping clock.h.
#define TIME_STRING_REQUIRED_LENGTH 20
#define TIME_STRING_TIME_LENGTH 10

#include <time.h>

// Format a timestamp's time-of-day into buffer (launcher alarms glance).
// Implemented in fw/src/shell_glue.c.
size_t clock_copy_time_string_timestamp(char *buffer, uint8_t size, time_t timestamp);

// Format the current wall-clock time ("H:MM"/"HH:MM", honoring 24h style) into
// buffer. Implemented in fw/src/app_service_stubs.c against rtc_get_time.
void clock_copy_time_string(char *buffer, uint8_t size);

// Format an explicit hours:minutes into buffer, honoring 24h style. Implemented
// in fw/src/app_service_stubs.c (used by the Alarms list rows).
size_t clock_format_time(char *buffer, uint8_t size, int16_t hours, int16_t minutes,
                         bool add_space);
