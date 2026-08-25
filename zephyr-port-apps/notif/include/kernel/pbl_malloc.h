/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

void *kernel_zalloc(size_t size);
void *kernel_zalloc_check(size_t size);
void *kernel_realloc(void *ptr, size_t size);
void kernel_free(void *ptr);
void *task_malloc(size_t size);
void *task_malloc_check(size_t size);
void *task_zalloc(size_t size);
void *task_zalloc_check(size_t size);
void task_free(void *ptr);

