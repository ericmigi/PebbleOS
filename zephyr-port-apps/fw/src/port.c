/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "applib/event_service_client.h"
#include "applib/tick_timer_service_private.h"
#include "kernel/events.h"
#include "kernel/kernel_applib_state.h"
#include "kernel/memory_layout.h"
#include "pbl/drivers/rtc.h"
#include "pbl/services/event_service.h"
#include "pbl/util/list.h"
#include "system/passert.h"
#include "system/reset.h"
#include "syscall/syscall.h"

time_t kernel_wall_clock_get(void);

void pebble_zephyr_core_event_loop_init(void) {
  printk("FW_EVENT_LOOP_UP\n");
}

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

bool rng_rand(uint32_t *rand_out) {
  ARG_UNUSED(rand_out);
  return false;
}

static EventServiceInfo s_event_service_state;
static TickTimerServiceState s_tick_timer_state;

EventServiceInfo *kernel_applib_get_event_service_state(void) {
  return &s_event_service_state;
}

TickTimerServiceState *kernel_applib_get_tick_timer_service_state(void) {
  return &s_tick_timer_state;
}

EventServiceInfo *app_state_get_event_service_state(void) {
  return &s_event_service_state;
}

TickTimerServiceState *app_state_get_tick_timer_service_state(void) {
  return &s_tick_timer_state;
}

EventServiceInfo *worker_state_get_event_service_state(void) {
  return &s_event_service_state;
}

TickTimerServiceState *worker_state_get_tick_timer_service_state(void) {
  return &s_tick_timer_state;
}

void sys_event_service_client_subscribe(EventServiceInfo *handler) {
  PebbleSubscriptionEvent subscription = {
    .subscribe = true,
    .task = PebbleTask_KernelMain,
    .event_type = handler->type,
    .event_queue = event_kernel_to_kernel_event_queue(),
  };
  event_service_subscribe_from_kernel_main(&subscription);
}

void sys_event_service_client_unsubscribe(EventServiceInfo *state, EventServiceInfo *handler) {
  list_remove(&handler->list_node, NULL, NULL);
  if (list_find(&state->list_node, event_service_filter, (void *)(uintptr_t)handler->type)) {
    return;
  }
  PebbleSubscriptionEvent subscription = {
    .subscribe = false,
    .task = PebbleTask_KernelMain,
    .event_type = handler->type,
  };
  event_service_handle_subscription(&subscription);
}

void *kernel_malloc(size_t size) {
  return k_malloc(size);
}

void *kernel_malloc_check(size_t size) {
  void *memory = k_malloc(size);
  __ASSERT_NO_MSG(memory || size == 0);
  return memory;
}

void *kernel_zalloc(size_t size) {
  return k_calloc(1, size);
}

void *kernel_zalloc_check(size_t size) {
  void *memory = k_calloc(1, size);
  __ASSERT_NO_MSG(memory || size == 0);
  return memory;
}

void kernel_free(void *ptr) {
  k_free(ptr);
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

void passert_check_task(PebbleTask expected_task) {
  PBL_ASSERTN(pebble_task_get_current() == expected_task);
}

void passert_check_not_task(PebbleTask unexpected_task) {
  PBL_ASSERTN(pebble_task_get_current() != unexpected_task);
}

void util_assertion_failed(const char *filename, int line) {
  passert_failed_no_message(filename, line);
}

void util_log(const char *filename, int line, const char *string) {
  printk("%s:%d %s\n", filename, line, string);
}

void util_dbgserial_str(const char *string) {
  printk("%s", string);
}

static MpuRegion s_empty_region;

const MpuRegion *memory_layout_get_app_region(void) { return &s_empty_region; }
const MpuRegion *memory_layout_get_worker_region(void) { return &s_empty_region; }
const MpuRegion *memory_layout_get_app_stack_guard_region(void) { return NULL; }
const MpuRegion *memory_layout_get_worker_stack_guard_region(void) { return NULL; }
const MpuRegion *memory_layout_get_kernel_main_stack_guard_region(void) { return NULL; }
const MpuRegion *memory_layout_get_kernel_bg_stack_guard_region(void) { return NULL; }

void mpu_init_region_from_region(MpuRegion *copy, const MpuRegion *from, bool allow_user_access) {
  ARG_UNUSED(allow_user_access);
  *copy = *from;
}

void mpu_set_task_configurable_regions(MemoryRegion_t *regions,
                                       const MpuRegion **region_ptrs) {
  ARG_UNUSED(region_ptrs);
  memset(regions, 0, sizeof(MemoryRegion_t) * portNUM_CONFIGURABLE_REGIONS);
}

void task_watchdog_bit_set(PebbleTask task) { ARG_UNUSED(task); }
void task_watchdog_mask_set(PebbleTask task) { ARG_UNUSED(task); }
void system_task_watchdog_feed(void);
void mcu_fpu_cleanup(void) { }

void reboot_reason_set(RebootReason *reason) {
  ARG_UNUSED(reason);
}

NORETURN reset_due_to_software_failure(void) {
  printk("FW_FATAL software_failure\n");
  k_panic();
  CODE_UNREACHABLE;
}
