/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <zephyr/kernel.h>

#include "kernel/task_timer.h"
#include "kernel/task_timer_manager.h"
#include "pbl/os/semaphore.h"
#include "pbl/os/tick.h"
#include "pbl/services/new_timer/new_timer.h"

#define NEW_TIMER_STACK_SIZE 2048
#define NEW_TIMER_PRIORITY 2
#define NEW_TIMER_WORK_ITEMS 5

typedef struct {
  NewTimerWorkCallback callback;
  void *data;
} NewTimerWorkItem;

static TaskTimerManager s_manager;
static PebbleSemaphore *s_wake_semaphore;
static struct k_timer s_deadline_timer;
static struct k_thread s_thread;
static K_THREAD_STACK_DEFINE(s_stack, NEW_TIMER_STACK_SIZE);
K_MSGQ_DEFINE(s_work_queue, sizeof(NewTimerWorkItem), NEW_TIMER_WORK_ITEMS, sizeof(void *));

void pebble_zephyr_semaphore_give(SemaphoreHandle_t semaphore) {
  semaphore_give((PebbleSemaphore *)semaphore);
}

static void prv_deadline_expired(struct k_timer *timer) {
  ARG_UNUSED(timer);
  semaphore_give(s_wake_semaphore);
}

static void prv_service_loop(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  while (true) {
    TickType_t ticks_to_wait = task_timer_manager_execute_expired_timers(&s_manager);
    if (ticks_to_wait == portMAX_DELAY) {
      semaphore_take(s_wake_semaphore);
    } else {
      // task_timer deadlines are in Zephyr ticks. Keep them in that domain so
      // the 10 kHz pt2 clock does not lose sub-millisecond wakeups.
      k_timer_start(&s_deadline_timer, K_TICKS(ticks_to_wait - 1), K_NO_WAIT);
      semaphore_take(s_wake_semaphore);
      k_timer_stop(&s_deadline_timer);
    }

    NewTimerWorkItem work;
    while (k_msgq_get(&s_work_queue, &work, K_NO_WAIT) == 0) {
      work.callback(work.data);
    }
  }
}

void new_timer_service_init(void) {
  s_wake_semaphore = semaphore_create();
  k_timer_init(&s_deadline_timer, prv_deadline_expired, NULL);
  task_timer_manager_init(&s_manager, (SemaphoreHandle_t)s_wake_semaphore);
  k_thread_create(&s_thread, s_stack, K_THREAD_STACK_SIZEOF(s_stack), prv_service_loop, NULL, NULL,
                  NULL, NEW_TIMER_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(&s_thread, "NewTimer");
}

TimerID new_timer_create(void) {
  return task_timer_create(&s_manager);
}

bool new_timer_start(TimerID timer, uint32_t timeout_ms, NewTimerCallback callback, void *data,
                     uint32_t flags) {
  return task_timer_start(&s_manager, timer, timeout_ms, callback, data, flags);
}

bool new_timer_stop(TimerID timer) {
  return task_timer_stop(&s_manager, timer);
}

bool new_timer_scheduled(TimerID timer, uint32_t *expire_ms) {
  return task_timer_scheduled(&s_manager, timer, expire_ms);
}

void new_timer_delete(TimerID timer) {
  task_timer_delete(&s_manager, timer);
}

void *new_timer_debug_get_current_callback(void) {
  return task_timer_manager_get_current_cb(&s_manager);
}

bool new_timer_add_work_callback(NewTimerWorkCallback callback, void *data) {
  NewTimerWorkItem work = {.callback = callback, .data = data};
  if (k_msgq_put(&s_work_queue, &work, K_MSEC(50)) != 0) {
    return false;
  }
  semaphore_give(s_wake_semaphore);
  return true;
}

bool new_timer_add_work_callback_from_isr(NewTimerWorkCallback callback, void *data) {
  NewTimerWorkItem work = {.callback = callback, .data = data};
  if (k_msgq_put(&s_work_queue, &work, K_NO_WAIT) != 0) {
    return false;
  }
  semaphore_give(s_wake_semaphore);
  return true;
}
