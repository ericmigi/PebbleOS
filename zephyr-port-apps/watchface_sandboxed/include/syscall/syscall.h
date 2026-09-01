/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "applib/fonts/fonts.h"

struct tm *sys_localtime_r(const time_t *timep, struct tm *result);
bool sys_app_is_watchface(void);
bool sys_resource_is_valid(ResAppNum app_num, uint32_t resource_id);
size_t sys_resource_size(ResAppNum app_num, uint32_t resource_id);
size_t sys_resource_load_range(ResAppNum app_num, uint32_t resource_id,
                               uint32_t start_bytes, uint8_t *buffer, size_t num_bytes);
bool sys_resource_bytes_are_readonly(void *bytes);
const uint8_t *sys_resource_read_only_bytes(ResAppNum app_num, uint32_t resource_id,
                                            size_t *num_bytes_out);
uint32_t sys_resource_get_and_cache(ResAppNum app_num, uint32_t resource_id);
ResAppNum sys_get_current_resource_num(void);
GFont sys_font_get_system_font(const char *font_key);
void sys_font_reload_font(FontInfo *font_info);


void sys_vibe_pattern_enqueue_step_raw(uint32_t step_duration_ms, int32_t strength);
void sys_vibe_pattern_trigger_start(void);
