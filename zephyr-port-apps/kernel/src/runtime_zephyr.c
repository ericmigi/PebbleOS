/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "applib/event_service_client.h"
#include "applib/tick_timer_service_private.h"
#include "kernel/events.h"
#include "kernel/kernel_applib_state.h"
#include "kernel/pebble_tasks.h"
#include "pbl/drivers/rtc.h"
#include "pbl/services/event_service.h"
#include "pbl/util/attributes.h"

#define EVENT_QUEUE_DEPTH 8

static EventServiceInfo *s_event_client;
static EventServiceAddSubscriberCallback s_add_subscriber;
static EventServiceRemoveSubscriberCallback s_remove_subscriber;
static struct k_thread *s_kernel_thread;

K_MSGQ_DEFINE(s_event_queue, sizeof(PebbleEvent), EVENT_QUEUE_DEPTH, sizeof(uint32_t));

time_t kernel_wall_clock_get(void);

RtcTicks rtc_get_ticks(void) {
  return k_uptime_ticks();
}

time_t rtc_get_time(void) {
  return kernel_wall_clock_get();
}

void rtc_get_time_ms(time_t *seconds, uint16_t *milliseconds) {
  *seconds = rtc_get_time();
  *milliseconds = k_uptime_get_32() % 1000;
}

struct tm *sys_localtime_r(const time_t *timep, struct tm *result) {
  return gmtime_r(timep, result);
}

bool clock_is_24h_style(void) {
  return true;
}

bool sys_app_is_watchface(void) {
  return false;
}

PebbleTask pebble_task_get_current(void) {
  return k_current_get() == s_kernel_thread ? PebbleTask_KernelMain : PebbleTask_NewTimers;
}

TaskHandle_t pebble_task_get_handle_for_task(PebbleTask task) {
  if (task == PebbleTask_KernelMain) {
    return s_kernel_thread;
  }
  return NULL;
}

void kernel_runtime_set_kernel_thread(struct k_thread *thread) {
  s_kernel_thread = thread;
}

TickTimerServiceState *kernel_applib_get_tick_timer_service_state(void) {
  static TickTimerServiceState state;
  return &state;
}

TickTimerServiceState *app_state_get_tick_timer_service_state(void) {
  return kernel_applib_get_tick_timer_service_state();
}

TickTimerServiceState *worker_state_get_tick_timer_service_state(void) {
  return kernel_applib_get_tick_timer_service_state();
}

void event_service_init(PebbleEventType type, EventServiceAddSubscriberCallback add_subscriber,
                        EventServiceRemoveSubscriberCallback remove_subscriber) {
  if (type == PEBBLE_TICK_EVENT) {
    s_add_subscriber = add_subscriber;
    s_remove_subscriber = remove_subscriber;
  }
}

void event_service_client_subscribe(EventServiceInfo *service_info) {
  s_event_client = service_info;
  if (s_add_subscriber != NULL) {
    s_add_subscriber(PebbleTask_KernelMain);
  }
}

void event_service_client_unsubscribe(EventServiceInfo *service_info) {
  if (s_event_client != service_info) {
    return;
  }
  if (s_remove_subscriber != NULL) {
    s_remove_subscriber(PebbleTask_KernelMain);
  }
  s_event_client = NULL;
}

void event_put(PebbleEvent *event) {
  if (k_msgq_put(&s_event_queue, event, K_MSEC(3000)) != 0) {
    printk("EVENT_QUEUE_FULL\n");
    k_panic();
  }
}

bool kernel_runtime_take_event(PebbleEvent *event) {
  return k_msgq_get(&s_event_queue, event, K_FOREVER) == 0;
}

void kernel_runtime_dispatch_event(PebbleEvent *event) {
  if (s_event_client != NULL && s_event_client->type == event->type) {
    s_event_client->handler(event, s_event_client->context);
  }
}

NORETURN passert_failed(const char *filename, int line_number, const char *message, ...) {
  ARG_UNUSED(message);
  printk("PASSERT %s:%d\n", filename, line_number);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN passert_failed_no_message(const char *filename, int line_number) {
  printk("PASSERT %s:%d\n", filename, line_number);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN passert_failed_no_message_with_lr(const char *filename, int line_number, uint32_t lr) {
  printk("PASSERT %s:%d lr=%#x\n", filename, line_number, lr);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN wtf(void) {
  printk("WTF\n");
  k_panic();
  CODE_UNREACHABLE;
}
