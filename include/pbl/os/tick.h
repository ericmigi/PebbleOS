/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#ifdef __ZEPHYR__
typedef uint32_t TickType_t;
#else
#include "portmacro.h"
#endif

TickType_t milliseconds_to_ticks(uint32_t milliseconds);

uint32_t ticks_to_milliseconds(TickType_t ticks);
