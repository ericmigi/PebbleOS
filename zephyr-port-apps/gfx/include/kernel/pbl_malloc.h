/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

void *kernel_zalloc(size_t size);
void kernel_free(void *ptr);

// task_* map to the current task's heap; backed by the app heap in the fw
// applib UI shell (watchface_sandboxed/src/port.c).
void *task_malloc(size_t bytes);
void task_free(void *ptr);
