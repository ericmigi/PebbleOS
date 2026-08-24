/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

void *applib_malloc(size_t size);
void *applib_zalloc(size_t size);
void applib_free(void *ptr);

#define applib_type_malloc(type) applib_malloc(sizeof(type))
#define applib_type_zalloc(type) applib_zalloc(sizeof(type))
#define applib_type_size(type) (sizeof(type))
