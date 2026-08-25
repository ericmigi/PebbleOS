/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <time.h>

bool clock_is_24h_style(void);

#include <stddef.h>
#include <stdint.h>

// Buffer length for a formatted time string ("HH:MM AM"), from shipping clock.h.
#define TIME_STRING_TIME_LENGTH 10
#define TIME_STRING_REQUIRED_LENGTH 32
#define TIME_STRING_DAY_DATE_LENGTH 3

// Format the current wall-clock time ("H:MM"/"HH:MM", honoring 24h style) into
// buffer. Implemented in fw/src/app_service_stubs.c against rtc_get_time.
void clock_copy_time_string(char *buffer, uint8_t size);

// Format an explicit hours:minutes into buffer, honoring 24h style. Implemented
// in fw/src/app_service_stubs.c (used by the Alarms list rows).
size_t clock_format_time(char *buffer, uint8_t size, int16_t hours, int16_t minutes,
                         bool add_space);
void clock_get_since_time(char *buffer, int buf_size, time_t timestamp);
void clock_get_until_time(char *buffer, int buf_size, time_t timestamp, int max_relative_hrs);
