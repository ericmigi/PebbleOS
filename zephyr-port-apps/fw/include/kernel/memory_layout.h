/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <pbl/drivers/mpu.h>

#define KERNEL_READONLY_DATA

const MpuRegion *memory_layout_get_app_region(void);
const MpuRegion *memory_layout_get_worker_region(void);
const MpuRegion *memory_layout_get_app_stack_guard_region(void);
const MpuRegion *memory_layout_get_worker_stack_guard_region(void);
const MpuRegion *memory_layout_get_kernel_main_stack_guard_region(void);
const MpuRegion *memory_layout_get_kernel_bg_stack_guard_region(void);
