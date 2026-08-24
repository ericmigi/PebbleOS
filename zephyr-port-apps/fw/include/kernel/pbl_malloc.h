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
