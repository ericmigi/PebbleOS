/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

void *kernel_malloc(size_t size);
void *kernel_malloc_check(size_t size);
void *kernel_zalloc(size_t size);
void *kernel_zalloc_check(size_t size);
void *kernel_calloc(size_t count, size_t size);
void *kernel_calloc_check(size_t count, size_t size);
void kernel_free(void *ptr);
char *kernel_strdup(const char *string);
char *kernel_strdup_check(const char *string);

// task_* map to the current task's heap; the applib UI shell (watchface port.c)
// backs these with the app heap.
void *task_malloc(size_t bytes);
void task_free(void *ptr);

// app_* heap for privileged built-in system apps (system_app.c backs these with
// the kernel heap; shipping carves a dedicated app RAM segment).
void *app_malloc(size_t bytes);
void *app_malloc_check(size_t bytes);
void *app_zalloc_check(size_t size);
void app_free(void *ptr);
