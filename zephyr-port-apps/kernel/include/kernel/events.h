/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>
#include <time.h>

#include "kernel/pebble_tasks.h"

typedef enum {
  PEBBLE_NULL_EVENT = 0,
  PEBBLE_TICK_EVENT = 15,
} PebbleEventType;

typedef struct {
  time_t tick_time;
} PebbleTickEvent;

typedef struct {
  PebbleTickEvent clock_tick;
  PebbleTaskBitset task_mask;
  PebbleEventType type;
} PebbleEvent;

void event_put(PebbleEvent *event);
