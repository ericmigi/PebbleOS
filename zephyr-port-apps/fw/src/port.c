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
  // The FreeRTOS reference under QEMU runs with the 12h default (Settings ->
  // Date & Time -> Time Format shows "12h").
  return false;
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

// Real applib animation engine state (KernelMain UI + apps share the single
// port event loop; apps get their own AnimationState like shipping firmware).
#include "applib/ui/animation_private.h"
static AnimationState s_kernel_animation_state;
static AnimationState s_app_animation_state;

AnimationState *kernel_applib_get_animation_state(void) {
  return &s_kernel_animation_state;
}

AnimationState *app_state_get_animation_state(void) {
  return &s_app_animation_state;
}

#if defined(CONFIG_BOARD_QEMU_EMERY)
__attribute__((weak)) bool fw_compositor_anim_snap_ticks(uint64_t raw, uint64_t *out) {
  (void)raw;
  (void)out;
  return false;
}
#endif

// settings/system.c: uptime + duration formatting helpers.
uint32_t time_get_uptime_seconds(void) {
  return (uint32_t)(k_uptime_get() / 1000);
}

void time_util_split_seconds_into_parts(uint32_t seconds, uint32_t *day_part,
                                        uint32_t *hour_part, uint32_t *minute_part,
                                        uint32_t *second_part) {
  *day_part = seconds / SEC_PER_DAY;
  seconds %= SEC_PER_DAY;
  *hour_part = seconds / SEC_PER_HOUR;
  seconds %= SEC_PER_HOUR;
  *minute_part = seconds / SEC_PER_MIN;
  *second_part = seconds % SEC_PER_MIN;
}

RtcTicks sys_get_ticks(void) {
  // Quantize the animation clock to 10 ms buckets (this build's only callers
  // are applib animation.c and menu_layer's double-tap window). Frame pacing
  // (launcher_ui.c) lands animation callbacks mid-bucket, so the elapsed-ms
  // each animation frame samples is exact multiples of the frame cadence and
  // the rendered pixels are run-to-run deterministic; without this, ±1-2 ms
  // scheduling jitter flips pixels wherever a sample lands near an easing-curve
  // boundary (the FreeRTOS reference itself flips those frames run-to-run).
  const RtcTicks ticks = rtc_get_ticks();
#if defined(CONFIG_BOARD_QEMU_EMERY)
  // While a compositor transition animates, its frames must sample the
  // reference's (non-decade) instants; compositor_port.c snaps the clock onto
  // that stream instead (weak default: no compositor linked, e.g. pt2).
  RtcTicks snapped;
  if (fw_compositor_anim_snap_ticks(ticks, &snapped)) {
    return snapped;
  }
  return ticks - (ticks % (10 * RTC_TICKS_HZ / 1000));
#else
  // Hardware keeps the raw clock: quantization is a QEMU determinism aid
  // for the frame-diff harness, not shipping behavior.
  return ticks;
#endif
}

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

void *kernel_calloc(size_t count, size_t size) {
  return k_calloc(count, size);
}

void *kernel_calloc_check(size_t count, size_t size) {
  void *memory = k_calloc(count, size);
  __ASSERT_NO_MSG(memory || count == 0 || size == 0);
  return memory;
}

void kernel_free(void *ptr) {
  k_free(ptr);
}

char *kernel_strdup(const char *string) {
  size_t size = strlen(string) + 1u;
  char *copy = kernel_malloc(size);
  if (copy != NULL) {
    memcpy(copy, string, size);
  }
  return copy;
}

char *kernel_strdup_check(const char *string) {
  char *copy = kernel_strdup(string);
  __ASSERT_NO_MSG(copy != NULL);
  return copy;
}

void psleep(int millis) {
  if (millis == 0) {
    k_yield();
  } else {
    k_msleep(millis);
  }
}

void prompt_send_response(const char *response) {
  printk("%s\n", response);
}

void prompt_send_response_fmt(char *buffer, size_t buffer_size,
                              const char *format, ...) {
  va_list arguments;

  va_start(arguments, format);
  vsnprintk(buffer, buffer_size, format, arguments);
  va_end(arguments);
  printk("%s\n", buffer);
}

NORETURN pfs_port_panic(const char *file, int line, const char *format, ...) {
  va_list arguments;

  printk("FW_PFS_FAIL %s:%d ", file, line);
  va_start(arguments, format);
  vprintk(format, arguments);
  va_end(arguments);
  printk("\n");
  k_panic();
  CODE_UNREACHABLE;
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

NORETURN wtf_with_context(const char *filename, int line_number) {
  printk("WTF %s:%d task=%u thread=%p\n", filename, line_number,
         (unsigned int)pebble_task_get_current(), k_current_get());
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

// Apps share the KernelMain pump; App-targeted events (evented_timer, the
// animation service frame events) land on the shared queue.
bool process_manager_send_event_to_process(PebbleTask task, PebbleEvent *e) {
  ARG_UNUSED(task);
  event_put(e);
  return true;
}
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
