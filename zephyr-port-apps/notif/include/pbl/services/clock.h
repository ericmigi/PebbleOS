/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <time.h>

#define TIME_STRING_REQUIRED_LENGTH 32
#define TIME_STRING_DAY_DATE_LENGTH 3

bool clock_is_24h_style(void);
void clock_get_since_time(char *buffer, int buf_size, time_t timestamp);
void clock_get_until_time(char *buffer, int buf_size, time_t timestamp, int max_relative_hrs);
void clock_get_time_number(char *buffer, size_t buffer_size, time_t timestamp);
void clock_get_time_word(char *buffer, size_t buffer_size, time_t timestamp);
void clock_copy_time_string_timestamp(char *buffer, size_t buffer_size, time_t timestamp);
