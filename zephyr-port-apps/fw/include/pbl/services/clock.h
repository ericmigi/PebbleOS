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

// --- Date & Time settings surface (settings/time.c), mirroring shipping
// include/pbl/services/clock.h. Backed by RAM state in apps_port_glue.c.
#define TIMEZONE_NAME_LENGTH 32

bool clock_is_timezone_set(void);
bool clock_time_source_is_manual(void);
void clock_set_manual_time_source(bool manual);
bool clock_timezone_source_is_manual(void);
void clock_set_manual_timezone_source(bool manual);
void clock_get_timezone_region(char *region_name, const size_t buffer_size);
void clock_set_timezone_by_region_id(uint16_t region_id);
void clock_set_time(time_t utc_time);
void clock_set_24h_style(bool is_24h_style);
void clock_get_time_tm(struct tm *time_tm);
void clock_request_time_from_phone(void);
