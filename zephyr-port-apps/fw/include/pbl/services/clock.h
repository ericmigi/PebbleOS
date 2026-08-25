/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

bool clock_is_24h_style(void);

#include <stddef.h>
#include <stdint.h>

// Format the current wall-clock time ("H:MM"/"HH:MM", honoring 24h style) into
// buffer. Implemented in fw/src/app_service_stubs.c against rtc_get_time.
void clock_copy_time_string(char *buffer, uint8_t size);
