/* SPDX-License-Identifier: Apache-2.0 */

// Privileged system-app launch core: the port's analog of the shipping
// app_manager/process_manager path for a built-in (firmware) app.
//
// Shipping (see zephyr-port-notes/SYSTEM-APPS-BUILDOUT.md) runs each app on its
// own PebbleTask_App FreeRTOS task: prv_app_task_main() calls the app's
// md->main_func(), which runs applib app_event_loop() until a
// PEBBLE_PROCESS_DEINIT_EVENT, then returns to the launcher. System apps leave
// md->is_unprivileged == false, so the task stays privileged (no MPU drop).
//
// The port adapts that to the fw scaffold's single KernelMain UI loop: the
// launcher is just "the app at window-stack depth 0", so a privileged system app
// launches by calling md->main_func() inline on KernelMain (privileged, no MPU
// sandbox — unlike fw_sandbox_launch()). main_func pushes its window onto the
// shared window stack and calls this file's app_event_loop(), which re-uses the
// launcher's event pump and returns when the app's window is popped (BACK past
// its root). No separate App task, no event forwarding — everything runs on the
// existing KernelMain loop that the launcher already drives.
//
// ponytail: KernelMain, not a separate PebbleTask_App task. The launcher proves
// applib UI (window/layer/menu/click/tick) runs fine on KernelMain; the one
// App-task assertion a plain watchface trips (layer_get_unobstructed_bounds) is
// satisfied by the full-bounds shim below. Apps that need true App-task
// isolation (their own heap/stack guard, App-task click timers, real
// PEBBLE_PROCESS_DEINIT routing) are the P3 upgrade: give this function a
// dedicated PebbleTask_App thread + event forwarding, mirroring
// fw_sandbox_launch()'s thread model minus the MPU.

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// Zephyr and Pebble both declare sign_extend() with different signatures; load
// Zephyr's under a private name before the Pebble headers (mirrors launcher_ui.c
// and sandbox_graphics_state.h).
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#include <zephyr/sys/printk.h>

#include "applib/app_focus_service.h"
#include "applib/graphics/graphics.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/layer.h"
#include "applib/ui/window.h"
#include "process_management/pebble_process_md.h"

#include "launcher_ui.h"
#include "system_app.h"

// Provided by watchface_sandboxed/src/port.c (the fw graphics shell).
uint8_t *watchface_framebuffer_bytes(size_t *size_out, uint16_t *stride_out);
extern bool g_fw_privileged_window;

// Provided by fw/src/port.c.
time_t rtc_get_time(void);

// ---------------------------------------------------------------------------
// app_state: user data. One app runs at a time on KernelMain, so a single slot
// mirrors the per-App-task app_state in shipping.
// ---------------------------------------------------------------------------
static void *s_app_user_data;

void app_state_set_user_data(void *data) { s_app_user_data = data; }
void *app_state_get_user_data(void) { return s_app_user_data; }

// ---------------------------------------------------------------------------
// App heap. System apps are privileged, so the kernel heap is fine (shipping
// carves a separate app RAM segment; that isolation is the P3 upgrade).
// ---------------------------------------------------------------------------
void *app_zalloc_check(size_t size) {
  void *memory = k_calloc(1, size);
  __ASSERT_NO_MSG(memory || size == 0);
  return memory;
}

void *app_malloc_check(size_t size) {
  void *memory = k_malloc(size);
  __ASSERT_NO_MSG(memory || size == 0);
  return memory;
}

void app_free(void *ptr) { k_free(ptr); }

// ---------------------------------------------------------------------------
// app_focus_service: the fw scaffold has no notification/modal overlay, so an
// app is always in focus. Stubbed (a watchface only uses this to redraw on
// regaining focus, which never happens here).
// ---------------------------------------------------------------------------
void app_focus_service_subscribe_handlers(AppFocusHandlers handlers) {
  ARG_UNUSED(handlers);
}

void app_focus_service_unsubscribe(void) {}

// ---------------------------------------------------------------------------
// Unobstructed area: no obstruction service in the port, so the unobstructed
// bounds are always the full layer bounds. Overrides applib/ui/layer.c's
// versions (renamed away for this build in CMakeLists) which assert
// PebbleTask_App and pull in the unobstructed_area_service.
// ---------------------------------------------------------------------------
void layer_get_unobstructed_bounds(const Layer *layer, GRect *bounds_out) {
  layer_get_bounds(layer, bounds_out);
}

GRect layer_get_unobstructed_bounds_by_value(const Layer *layer) {
  GRect bounds;
  layer_get_bounds(layer, &bounds);
  return bounds;
}

// ---------------------------------------------------------------------------
// Minimal window_* the launcher does not already provide. window_create/destroy,
// window_get_root_layer, window_set_background_color, the click glue and
// app_window_stack_push live in port.c / launcher_ui.c; these cover the embedded
// (window_init) window a system app uses. Mirrors port.c's window_create body.
// ---------------------------------------------------------------------------
static void prv_window_bg_update_proc(Layer *layer, GContext *ctx) {
  Window *window = (Window *)layer;
  graphics_context_set_fill_color(ctx, window->background_color);
  graphics_fill_rect(ctx, &layer->bounds);
}

void window_init(Window *window, const char *debug_name) {
  memset(window, 0, sizeof(*window));
  size_t fb_size = 0;
  uint16_t stride = 0;
  (void)watchface_framebuffer_bytes(&fb_size, &stride);
  const GRect frame = GRect(0, 0, stride, stride ? (int16_t)(fb_size / stride) : 0);
  layer_init(&window->layer, &frame);
  window->layer.window = window;
  window->layer.update_proc = prv_window_bg_update_proc;
  window->background_color = GColorWhite;
  window->is_fullscreen = true;
  window->debug_name = debug_name;
}

void window_set_window_handlers(Window *window, const WindowHandlers *handlers) {
  window->window_handlers = *handlers;
}

void window_set_user_data(Window *window, void *data) { window->user_data = data; }

void *window_get_user_data(const Window *window) { return window->user_data; }

void window_deinit(Window *window) { layer_deinit(&window->layer); }

// ---------------------------------------------------------------------------
// rtc_get_time_tm: the port formats wall-clock as UTC (matching the sandbox
// watchface), so gmtime_r, not localtime_r.
// ---------------------------------------------------------------------------
void rtc_get_time_tm(struct tm *time_tm) {
  time_t now = rtc_get_time();
  gmtime_r(&now, time_tm);
}

// ---------------------------------------------------------------------------
// app_event_loop: the applib entry a system app's main() calls. Hands the
// app's window (just pushed via app_window_stack_push) to the shared launcher
// window stack + pump, then pumps until BACK pops it (returns to main() ->
// deinit -> fw_system_app_launch). Analog of shipping app.c:app_event_loop_common
// looping on sys_get_pebble_event until PEBBLE_PROCESS_DEINIT_EVENT.
// ---------------------------------------------------------------------------
void app_event_loop(void) {
  Window *window = app_window_stack_get_top_window();
  const int base_depth = fw_window_stack_depth();
  if (window) {
    fw_window_stack_push(window);  // renders it + applies its (empty) click config
  }
  printk("SYS_APP_LOOP depth=%d\n", fw_window_stack_depth());

  // Pump until the app's window is popped by BACK (depth returns to base).
  while (fw_window_stack_depth() > base_depth) {
    fw_ui_pump_once();
  }
}

// ---------------------------------------------------------------------------
// The launch entry point (called by the launcher on SELECT).
// ---------------------------------------------------------------------------
void fw_system_app_launch(const PebbleProcessMd *md) {
  if (!md || !md->main_func) {
    printk("SYS_APP_LAUNCH_FAIL no-md\n");
    return;
  }

  const char *name = "?";
  if (md->process_storage == ProcessStorageBuiltin) {
    name = ((const PebbleProcessMdSystem *)md)->name;
  }
  printk("SYS_APP_LAUNCH %s\n", name);

  s_app_user_data = NULL;
  g_fw_privileged_window = true;
  md->main_func();  // push window (load runs) -> app_event_loop -> deinit
  g_fw_privileged_window = false;

  printk("SYS_APP_EXIT %s\n", name);
}
