/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

void *kernel_zalloc(size_t size);
void kernel_free(void *ptr);
