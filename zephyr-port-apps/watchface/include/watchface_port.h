/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "kernel/events.h"

extern const void *const g_pbl_system_tbl[626];

void watchface_port_set_threads(struct k_thread *kernel_thread, struct k_thread *app_thread);
void watchface_port_graphics_init(void);
void watchface_port_app_state_init(void);
void watchface_port_push_frame(void);
bool watchface_port_take_kernel_event(PebbleEvent *event);
void watchface_port_dispatch_kernel_event(PebbleEvent *event);
void watchface_start_fallback(void);

const uint8_t *watchface_framebuffer_bytes(size_t *size_out, uint16_t *stride_out);
