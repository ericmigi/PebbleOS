/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "applib/fonts/fonts.h"

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
