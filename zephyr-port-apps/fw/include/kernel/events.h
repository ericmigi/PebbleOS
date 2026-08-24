/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "FreeRTOS.h"
#include "kernel/pebble_tasks.h"
#include "pbl/util/list.h"

typedef enum {
  PEBBLE_NULL_EVENT = 0,
  PEBBLE_TICK_EVENT = 15,
  PEBBLE_CALLBACK_EVENT = 27,
  PEBBLE_SUBSCRIPTION_EVENT = 29,
  PEBBLE_NUM_EVENTS = 80,
} PebbleEventType;

typedef void (*CallbackEventCallback)(void *data);

typedef struct {
  time_t tick_time;
} PebbleTickEvent;

typedef struct {
  CallbackEventCallback callback;
  void *data;
} PebbleCallbackEvent;

typedef struct {
  bool subscribe;
  PebbleTask task : 8;
  PebbleEventType event_type;
  void *event_queue;
} PebbleSubscriptionEvent;

typedef struct {
  union {
    PebbleTickEvent clock_tick;
    PebbleCallbackEvent callback;
    PebbleSubscriptionEvent subscription;
  };
  PebbleTaskBitset task_mask;
  PebbleEventType type : 8;
} PebbleEvent;

void events_init(void);
void event_put(PebbleEvent *event);
bool event_put_isr(PebbleEvent *event);
void event_put_from_process(PebbleTask task, PebbleEvent *event);
bool event_try_put_from_process(PebbleTask task, PebbleEvent *event);
bool event_take_timeout(PebbleEvent *event, int timeout_ms);
void **event_get_buffer(PebbleEvent *event);
void event_deinit(PebbleEvent *event);
void event_cleanup(PebbleEvent *event);
void event_reset_from_process_queue(PebbleTask task);
QueueHandle_t event_get_to_kernel_queue(PebbleTask task);
QueueHandle_t event_kernel_to_kernel_event_queue(void);
BaseType_t event_queue_cleanup_and_reset(QueueHandle_t queue);
