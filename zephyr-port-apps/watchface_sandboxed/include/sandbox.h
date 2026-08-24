/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <zephyr/kernel.h>

#define SANDBOX_APP_SEGMENT_CAPACITY 4096U
#define SANDBOX_APP_HEAP_SIZE 16384U

typedef struct {
  uint8_t app_segment[SANDBOX_APP_SEGMENT_CAPACITY];
  uint8_t app_heap[SANDBOX_APP_HEAP_SIZE];
  struct tm localtime_result;
  uint8_t mpu_tail[32];
} SandboxAppArena;

extern SandboxAppArena g_sandbox_app_arena;

bool sandbox_prepare(struct k_thread *app_thread, uintptr_t stack_start,
                     size_t stack_size);
void sandbox_arm(void);
void sandbox_dump_active_mpu(void);
bool sandbox_userspace_buffer_is_valid(const void *buffer, size_t size);
bool sandbox_thread_is_unprivileged(void);
