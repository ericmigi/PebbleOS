/* SPDX-License-Identifier: Apache-2.0 */

#include "sandbox_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <zephyr/sys/printk.h>

#define sign_extend zephyr_sign_extend
#include "sandbox.h"
#undef sign_extend

#include "applib/applib_resource.h"
#include "applib/fonts/fonts.h"
#include "applib/tick_timer_service_private.h"
#include "applib/tick_timer_service.h"
#include "applib/ui/animation.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/layer.h"
#include "applib/ui/text_layer.h"
#include "applib/ui/window.h"
#include "applib/unobstructed_area_service.h"
#include "kernel/pebble_tasks.h"
#include "kernel/pbl_malloc.h"
#include "sandbox_syscall.h"
#include "sandbox_port.h"
#include "watchface_port.h"

void *task_malloc(size_t size);
void task_free(void *ptr);

static bool s_app_announced;
static uint32_t s_step_mask;

static const char *const s_step_names[SandboxStepCount] = {
    [SandboxStepAppEntry] = "app_entry",
    [SandboxStepWindowCreate] = "window_create",
    [SandboxStepTextLayerCreate] = "text_layer_create",
    [SandboxStepTickSubscribe] = "tick_subscribe",
    [SandboxStepWindowStackPush] = "window_stack_push",
    [SandboxStepEventLoop] = "event_loop",
    [SandboxStepWaitTick] = "wait_tick",
    [SandboxStepTickHandler] = "tick_handler",
    [SandboxStepRender] = "render",
};

static void prv_log_step(SandboxStep step, uintptr_t detail) {
  if ((unsigned int)step >= SandboxStepCount ||
      (s_step_mask & (1UL << step)) != 0U) {
    return;
  }

  s_step_mask |= 1UL << step;
  if (detail != 0U) {
    printk("SANDBOX_STEP %s 0x%08x\n", s_step_names[step],
           (uint32_t)detail);
  } else {
    printk("SANDBOX_STEP %s\n", s_step_names[step]);
  }
}

int __sandbox_syscall_probe(int value);

DEFINE_SYSCALL(void, sandbox_app_runtime_init, void) {
  watchface_port_app_state_init();
#if !defined(PBL_WATCHFACE_IN_FW)
  tick_timer_service_init();
#endif
}

DEFINE_SYSCALL(void, sandbox_mpu_readback, void) {
  sandbox_dump_active_mpu();
}

DEFINE_SYSCALL(int, sandbox_syscall_probe, int value) {
  if (sandbox_thread_is_unprivileged()) {
    return -1;
  }
  if (!s_app_announced) {
    s_app_announced = true;
    printk("SANDBOX_APP_UP\n");
  }
  if (value == 42) {
    printk("SANDBOX_SYSCALL_OK\n");
  }
  return value * 2;
}

DEFINE_SYSCALL(void, sandbox_app_step, SandboxStep step, uintptr_t detail) {
  prv_log_step(step, detail);
}

DEFINE_SYSCALL(bool, sandbox_wait_tick, struct tm *tick_time,
               TimeUnits *units_changed, TickHandler *handler,
               time_t *timestamp) {
  if (!sandbox_userspace_buffer_is_valid(tick_time, sizeof(*tick_time)) ||
      !sandbox_userspace_buffer_is_valid(units_changed,
                                         sizeof(*units_changed)) ||
      !sandbox_userspace_buffer_is_valid(handler, sizeof(*handler)) ||
      !sandbox_userspace_buffer_is_valid(timestamp, sizeof(*timestamp))) {
    return false;
  }
  return watchface_port_wait_tick(tick_time, units_changed, handler,
                                  timestamp);
}

DEFINE_SYSCALL(void, sandbox_render_tick, time_t timestamp) {
  watchface_port_render_tick(timestamp);
}

void sandbox_app_event_loop(void) {
  sandbox_app_step(SandboxStepEventLoop, 0U);
  for (;;) {
    struct tm tick_time;
    TimeUnits units_changed;
    TickHandler handler;
    time_t timestamp;

    sandbox_app_step(SandboxStepWaitTick, 0U);
    if (sandbox_wait_tick(&tick_time, &units_changed, &handler, &timestamp) &&
        handler) {
      sandbox_app_step(SandboxStepTickHandler, (uintptr_t)handler);
      handler(&tick_time, units_changed);
      sandbox_app_step(SandboxStepRender, 0U);
      sandbox_render_tick(timestamp);
    }
  }
}

DEFINE_SYSCALL(GFont, sandbox_fonts_get_system_font, const char *font_key) {
  return fonts_get_system_font(font_key);
}

DEFINE_SYSCALL(GFont, sandbox_fonts_load_custom_font, ResHandle handle) {
  return fonts_load_custom_font(handle);
}

DEFINE_SYSCALL(void, sandbox_fonts_unload_custom_font, GFont font) {
  fonts_unload_custom_font(font);
}

DEFINE_SYSCALL(void, sandbox_task_free, void *ptr) {
  task_free(ptr);
}

DEFINE_SYSCALL(void, sandbox_layer_add_child, Layer *parent, Layer *child) {
  layer_add_child(parent, child);
}

DEFINE_SYSCALL(GRect, sandbox_layer_get_frame_by_value, const Layer *layer) {
  return layer_get_frame_by_value(layer);
}

DEFINE_SYSCALL(void, sandbox_layer_mark_dirty, Layer *layer) {
  layer_mark_dirty(layer);
}

DEFINE_SYSCALL(void, sandbox_layer_set_frame_by_value, Layer *layer,
               GRect frame) {
  layer_set_frame_by_value(layer, frame);
}

DEFINE_SYSCALL(void, sandbox_layer_set_hidden, Layer *layer, bool hidden) {
  layer_set_hidden(layer, hidden);
}

DEFINE_SYSCALL(void *, sandbox_task_malloc, size_t size) {
  return task_malloc(size);
}

DEFINE_SYSCALL(ResHandle, sandbox_resource_get_handle, uint32_t resource_id) {
  return applib_resource_get_handle(resource_id);
}

DEFINE_SYSCALL(char *, sandbox_strcat, char *destination,
               const char *source) {
  return strcat(destination, source);
}

DEFINE_SYSCALL(char *, sandbox_strcpy, char *destination,
               const char *source) {
  return strcpy(destination, source);
}

DEFINE_SYSCALL(size_t, sandbox_strlen, const char *string) {
  return strlen(string);
}

DEFINE_SYSCALL(void, sandbox_tick_timer_service_subscribe,
               TimeUnits tick_units, TickHandler handler) {
  prv_log_step(SandboxStepTickSubscribe, (uintptr_t)handler);
#if defined(PBL_WATCHFACE_IN_FW)
  watchface_port_tick_subscribe(tick_units, handler);
#else
  tick_timer_service_subscribe(tick_units, handler);
#endif
}

DEFINE_SYSCALL(void, sandbox_tick_timer_service_unsubscribe, void) {
#if defined(PBL_WATCHFACE_IN_FW)
  watchface_port_tick_unsubscribe();
#else
  tick_timer_service_unsubscribe();
#endif
}

DEFINE_SYSCALL(Window *, sandbox_window_create, void) {
  prv_log_step(SandboxStepWindowCreate, 0U);
  return window_create();
}

DEFINE_SYSCALL(void, sandbox_window_destroy, Window *window) {
  window_destroy(window);
}

DEFINE_SYSCALL(Layer *, sandbox_window_get_root_layer, const Window *window) {
  return window_get_root_layer(window);
}

DEFINE_SYSCALL(void, sandbox_app_window_stack_push, Window *window,
               bool animated) {
  prv_log_step(SandboxStepWindowStackPush, (uintptr_t)window);
  app_window_stack_push(window, animated);
}

DEFINE_SYSCALL(void, sandbox_window_set_background_color, Window *window,
               GColor color) {
  window_set_background_color(window, color);
}

DEFINE_SYSCALL(struct tm *, sandbox_localtime, const int32_t *timep) {
  if (timep && !sandbox_userspace_buffer_is_valid(timep, sizeof(*timep))) {
    return NULL;
  }
  return watchface_port_localtime(timep);
}

DEFINE_SYSCALL(Animation *, sandbox_animation_create, void) {
  return animation_create();
}

DEFINE_SYSCALL(bool, sandbox_animation_set_duration, Animation *animation,
               uint32_t duration_ms) {
  return animation_set_duration(animation, duration_ms);
}

DEFINE_SYSCALL(bool, sandbox_animation_set_implementation,
               Animation *animation,
               const AnimationImplementation *implementation) {
  return animation_set_implementation(animation, implementation);
}

DEFINE_SYSCALL(TextLayer *, sandbox_text_layer_create, GRect frame) {
  prv_log_step(SandboxStepTextLayerCreate, 0U);
  return text_layer_create(frame);
}

DEFINE_SYSCALL(void, sandbox_text_layer_destroy, TextLayer *text_layer) {
  text_layer_destroy(text_layer);
}

DEFINE_SYSCALL(Layer *, sandbox_text_layer_get_layer,
               TextLayer *text_layer) {
  return text_layer_get_layer(text_layer);
}

DEFINE_SYSCALL(const char *, sandbox_text_layer_get_text,
               TextLayer *text_layer) {
  return text_layer_get_text(text_layer);
}

DEFINE_SYSCALL(void, sandbox_text_layer_set_background_color,
               TextLayer *text_layer, GColor color) {
  text_layer_set_background_color(text_layer, color);
}

DEFINE_SYSCALL(void, sandbox_text_layer_set_font, TextLayer *text_layer,
               GFont font) {
  text_layer_set_font(text_layer, font);
}

DEFINE_SYSCALL(void, sandbox_text_layer_set_text, TextLayer *text_layer,
               const char *text) {
  text_layer_set_text(text_layer, text);
}

DEFINE_SYSCALL(void, sandbox_text_layer_set_text_alignment,
               TextLayer *text_layer, GTextAlignment alignment) {
  text_layer_set_text_alignment(text_layer, alignment);
}

DEFINE_SYSCALL(void, sandbox_text_layer_set_text_color,
               TextLayer *text_layer, GColor color) {
  text_layer_set_text_color(text_layer, color);
}

DEFINE_SYSCALL(int32_t, sandbox_time, int32_t *tloc) {
  if (tloc && !sandbox_userspace_buffer_is_valid(tloc, sizeof(*tloc))) {
    return -1;
  }
  return watchface_port_time(tloc);
}

DEFINE_SYSCALL(GRect, sandbox_layer_get_unobstructed_bounds,
               const Layer *layer) {
  return watchface_layer_get_unobstructed_bounds_by_value(layer);
}

DEFINE_SYSCALL(void, sandbox_unobstructed_area_service_subscribe,
               UnobstructedAreaHandlers handlers, void *context) {
  app_unobstructed_area_service_subscribe(handlers, context);
}

DEFINE_SYSCALL(void, sandbox_unobstructed_area_service_unsubscribe, void) {
  app_unobstructed_area_service_unsubscribe();
}

const void *const g_pbl_system_tbl[626] = {
    [31] = sandbox_app_event_loop,
    [96] = sandbox_fonts_get_system_font,
    [97] = sandbox_fonts_load_custom_font,
    [98] = sandbox_fonts_unload_custom_font,
    [99] = sandbox_task_free,
    [138] = sandbox_layer_add_child,
    [145] = sandbox_layer_get_frame_by_value,
    [150] = sandbox_layer_mark_dirty,
    [155] = sandbox_layer_set_frame_by_value,
    [156] = sandbox_layer_set_hidden,
    [161] = sandbox_task_malloc,
    [206] = sandbox_resource_get_handle,
    [241] = sandbox_strcat,
    [243] = sandbox_strcpy,
    [245] = sandbox_strlen,
    [262] = sandbox_tick_timer_service_subscribe,
    [263] = sandbox_tick_timer_service_unsubscribe,
    [271] = sandbox_window_create,
    [272] = sandbox_window_destroy,
    [275] = sandbox_window_get_root_layer,
    [287] = sandbox_app_window_stack_push,
    [377] = sandbox_window_set_background_color,
    [379] = sandbox_localtime,
    [380] = sandbox_animation_create,
    [384] = animation_schedule,
    [388] = sandbox_animation_set_duration,
    [390] = sandbox_animation_set_implementation,
    [391] = animation_unschedule,
    [462] = sandbox_text_layer_create,
    [463] = sandbox_text_layer_destroy,
    [465] = sandbox_text_layer_get_layer,
    [466] = sandbox_text_layer_get_text,
    [467] = sandbox_text_layer_set_background_color,
    [468] = sandbox_text_layer_set_font,
    [471] = sandbox_text_layer_set_text,
    [472] = sandbox_text_layer_set_text_alignment,
    [473] = sandbox_text_layer_set_text_color,
    [519] = sandbox_time,
    [622] = sandbox_layer_get_unobstructed_bounds,
    [624] = sandbox_unobstructed_area_service_subscribe,
    [625] = sandbox_unobstructed_area_service_unsubscribe,
};
