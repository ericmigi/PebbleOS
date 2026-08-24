/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/timeutil.h>

#include "applib/tick_timer_service.h"
#include "applib/tick_timer_service_private.h"
#include "kernel/events.h"
#include "kernel/kernel_applib_state.h"
#include "pbl/services/new_timer/new_timer_service.h"
#include "pbl/services/regular_timer.h"

#define KERNEL_STACK_SIZE 3072
#define KERNEL_PRIORITY 5

static struct k_thread s_kernel_thread;
static K_THREAD_STACK_DEFINE(s_kernel_stack, KERNEL_STACK_SIZE);

void kernel_runtime_set_kernel_thread(struct k_thread *thread);
bool kernel_runtime_take_event(PebbleEvent *event);
void kernel_runtime_dispatch_event(PebbleEvent *event);

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  ARG_UNUSED(units_changed);
  time_t timestamp = timeutil_timegm(tick_time);
  printk("TICK %lld %02d:%02d:%02d\n", (long long)timestamp, tick_time->tm_hour,
         tick_time->tm_min, tick_time->tm_sec);
}

static void prv_kernel_main(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  TickTimerServiceState *state = kernel_applib_get_tick_timer_service_state();
  tick_timer_service_state_init(state);
  tick_timer_service_init();
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);

  printk("KERNEL_UP\n");

  while (true) {
    PebbleEvent event;
    if (kernel_runtime_take_event(&event)) {
      kernel_runtime_dispatch_event(&event);
    }
  }
}

int main(void) {
  new_timer_service_init();
  regular_timer_init();

  kernel_runtime_set_kernel_thread(&s_kernel_thread);
  k_thread_create(&s_kernel_thread, s_kernel_stack, K_THREAD_STACK_SIZEOF(s_kernel_stack),
                  prv_kernel_main, NULL, NULL, NULL, KERNEL_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(&s_kernel_thread, "KernelMain");
  return 0;
}
