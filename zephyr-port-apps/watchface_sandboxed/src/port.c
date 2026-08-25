/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#include <zephyr/sys/printk.h>

#include "applib/applib_resource.h"
#include "applib/fonts/fonts.h"
#include "applib/graphics/framebuffer.h"
#include "applib/graphics/gcontext.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text_resources.h"
#include "applib/preferred_content_size.h"
#include "applib/tick_timer_service.h"
#include "applib/tick_timer_service_private.h"
#include "applib/ui/animation.h"
#include "applib/ui/layer.h"
#include "applib/ui/text_layer.h"
#include "applib/ui/window.h"
#include "applib/unobstructed_area_service.h"
#include "kernel/events.h"
#include "kernel/kernel_applib_state.h"
#include "kernel/pebble_tasks.h"
#include "pbl/drivers/rtc.h"
#include "pbl/services/event_service.h"
#include "pbl/util/attributes.h"
#include "pbl/util/crc32.h"
#include "process_state/app_state/app_state.h"
#include "resource/resource.h"
#include "sandbox.h"
#include "sliding_text_emery_resources.h"
#include "watchface_port.h"

#define EVENT_QUEUE_DEPTH 8
#define FONT_RESOURCE_GOTHAM_BOLD_50 1U
#define FONT_RESOURCE_GOTHAM_LIGHT_50 2U
#define SYSTEM_FONT_RESOURCE_GOTHIC_14 256U
#define PBPACK_MANIFEST_SIZE 12U
#define PBPACK_TABLE_ENTRY_SIZE 16U
#define PBPACK_APP_TABLE_ENTRIES 256U
#define PBPACK_CONTENT_OFFSET \
  (PBPACK_MANIFEST_SIZE + PBPACK_TABLE_ENTRY_SIZE * PBPACK_APP_TABLE_ENTRIES)
#if defined(WATCHFACE_ASCII_PREVIEW)
#define PREVIEW_COLS 50
#define PREVIEW_ROWS 28
#endif

typedef struct {
  TickTimerServiceState tick_state;
  TextRenderState text_render_state;
  Layer *layer_tree_stack[LAYER_TREE_STACK_SIZE];
  bool perimeter_debugging;
} WatchfaceAppState;

struct Animation {
  const AnimationImplementation *implementation;
  uint32_t duration_ms;
  bool scheduled;
};

static const uint8_t s_font_system_data[] __aligned(4) = {
#include "watchface_font_small.pbf.inc"
};

static FrameBuffer s_framebuffer;
static GContext s_context;
static FontInfo s_font_bold;
static FontInfo s_font_light;
static FontInfo s_font_system;
static WatchfaceAppState s_app_state;
static Layer *s_kernel_layer_tree_stack[LAYER_TREE_STACK_SIZE];
static Window *s_top_window;
static struct k_thread *s_kernel_thread;
static struct k_thread *s_app_thread;
#if !defined(PBL_WATCHFACE_IN_FW)
static EventServiceInfo *s_app_tick_client;
static EventServiceAddSubscriberCallback s_add_subscriber;
static EventServiceRemoveSubscriberCallback s_remove_subscriber;
#endif
static struct k_heap s_app_heap;

K_MSGQ_DEFINE(s_kernel_event_queue, sizeof(PebbleEvent), EVENT_QUEUE_DEPTH, sizeof(uint32_t));
K_MSGQ_DEFINE(s_app_event_queue, sizeof(PebbleEvent), EVENT_QUEUE_DEPTH, sizeof(uint32_t));

bool clock_is_24h_style(void);

#if !defined(PBL_WATCHFACE_IN_FW)
time_t kernel_wall_clock_get(void) {
  return (time_t)KERNEL_BUILD_EPOCH + (time_t)(k_uptime_get() / 1000);
}
#else
time_t kernel_wall_clock_get(void);
#endif

static uint32_t prv_read_u32(const uint8_t *bytes) {
  uint32_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

static const uint8_t *prv_resource_data(uint32_t resource_id, size_t *size_out) {
  const uint8_t *data = NULL;
  size_t size = 0;

  if (resource_id == SYSTEM_FONT_RESOURCE_GOTHIC_14) {
    data = s_font_system_data;
    size = sizeof(s_font_system_data);
  } else if (sliding_text_emery_resources_len >= PBPACK_CONTENT_OFFSET && resource_id > 0U &&
             resource_id <= prv_read_u32(sliding_text_emery_resources)) {
    const uint8_t *entry = sliding_text_emery_resources + PBPACK_MANIFEST_SIZE +
                           (resource_id - 1U) * PBPACK_TABLE_ENTRY_SIZE;
    const uint32_t entry_id = prv_read_u32(entry);
    const uint32_t offset = prv_read_u32(entry + 4U);
    const uint32_t length = prv_read_u32(entry + 8U);
    if (entry_id == resource_id &&
        offset <= sliding_text_emery_resources_len - PBPACK_CONTENT_OFFSET &&
        length <= sliding_text_emery_resources_len - PBPACK_CONTENT_OFFSET - offset) {
      data = sliding_text_emery_resources + PBPACK_CONTENT_OFFSET + offset;
      size = length;
    }
  }
  if (size_out) {
    *size_out = size;
  }
  return data;
}

static void prv_window_update_proc(Layer *layer, GContext *ctx) {
  Window *window = (Window *)layer;
  graphics_context_set_fill_color(ctx, window->background_color);
  graphics_fill_rect(ctx, &layer->bounds);
}

static void prv_render(void) {
  if (!s_top_window) {
    return;
  }
  layer_render_tree(&s_top_window->layer, &s_context);
  s_top_window->is_render_scheduled = false;
}

#if defined(WATCHFACE_ASCII_PREVIEW)
static void prv_print_preview(void) {
  printk("WATCHFACE_PREVIEW %dx%d\n", PREVIEW_COLS, PREVIEW_ROWS);
  for (int row = 0; row < PREVIEW_ROWS; ++row) {
    const int y0 = row * PBL_DISPLAY_HEIGHT / PREVIEW_ROWS;
    const int y1 = (row + 1) * PBL_DISPLAY_HEIGHT / PREVIEW_ROWS;
    printk("|");
    for (int col = 0; col < PREVIEW_COLS; ++col) {
      const int x0 = col * PBL_DISPLAY_WIDTH / PREVIEW_COLS;
      const int x1 = (col + 1) * PBL_DISPLAY_WIDTH / PREVIEW_COLS;
      bool lit = false;
      for (int y = y0; y < y1 && !lit; ++y) {
        const uint8_t *line = framebuffer_get_line(&s_framebuffer, y);
        for (int x = x0; x < x1; ++x) {
          if (line[x] == GColorWhite.argb) {
            lit = true;
            break;
          }
        }
      }
      printk("%c", lit ? '#' : ' ');
    }
    printk("|\n");
  }
}
#endif

static void prv_dump_frame(time_t timestamp) {
  struct tm now;
  gmtime_r(&timestamp, &now);
  prv_render();
  const size_t size = framebuffer_get_size_bytes(&s_framebuffer);
  const uint32_t crc = crc32(CRC32_INIT, s_framebuffer.buffer, size);
  printk("SANDBOX_TICK %02d:%02d\n", now.tm_hour, now.tm_min);
  printk("SANDBOX_FRAME 0x%08x\n", crc);
  watchface_port_push_frame();
#if defined(WATCHFACE_ASCII_PREVIEW)
  prv_print_preview();
#endif
}

void watchface_port_set_threads(struct k_thread *kernel_thread, struct k_thread *app_thread) {
  s_kernel_thread = kernel_thread;
  s_app_thread = app_thread;
}

void watchface_port_graphics_init(void) {
  const GSize size = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  framebuffer_init(&s_framebuffer, &size);
  graphics_context_init(&s_context, &s_framebuffer, GContextInitializationMode_App);
  k_heap_init(&s_app_heap, g_sandbox_app_arena.app_heap,
              sizeof(g_sandbox_app_arena.app_heap));

  if (!text_resources_init_font(SYSTEM_APP, FONT_RESOURCE_GOTHAM_BOLD_50, 0, &s_font_bold) ||
      !text_resources_init_font(SYSTEM_APP, FONT_RESOURCE_GOTHAM_LIGHT_50, 0, &s_font_light) ||
      !text_resources_init_font(SYSTEM_APP, SYSTEM_FONT_RESOURCE_GOTHIC_14, 0, &s_font_system)) {
    printk("WATCHFACE_FONT_FAIL\n");
    k_panic();
  }
}

void watchface_port_app_state_init(void) {
  memset(&s_app_state, 0, sizeof(s_app_state));
  tick_timer_service_state_init(&s_app_state.tick_state);
}

uint8_t *watchface_framebuffer_bytes(size_t *size_out, uint16_t *stride_out) {
  if (size_out) {
    *size_out = framebuffer_get_size_bytes(&s_framebuffer);
  }
  if (stride_out) {
    *stride_out = PBL_DISPLAY_WIDTH;
  }
  return s_framebuffer.buffer;
}

#if !defined(PBL_WATCHFACE_IN_FW)
void event_put(PebbleEvent *event) {
  if (k_msgq_put(&s_kernel_event_queue, event, K_MSEC(100)) != 0) {
    printk("WATCHFACE_EVENT_DROP kernel\n");
  }
}

bool watchface_port_take_kernel_event(PebbleEvent *event) {
  return k_msgq_get(&s_kernel_event_queue, event, K_FOREVER) == 0;
}

void watchface_port_dispatch_kernel_event(PebbleEvent *event) {
  if (event->type == PEBBLE_TICK_EVENT && s_app_tick_client &&
      k_msgq_put(&s_app_event_queue, event, K_MSEC(100)) != 0) {
    printk("WATCHFACE_EVENT_DROP app\n");
  }
}
#endif

bool watchface_port_wait_tick(struct tm *tick_time, TimeUnits *units_changed,
                              TickHandler *handler, time_t *timestamp) {
#if defined(PBL_WATCHFACE_IN_FW)
  static struct tm s_last_time;
  static bool s_first_tick = true;
  extern TickHandler g_watchface_fw_tick_handler;

  k_msleep(s_first_tick ? 1 : 1000);
  if (!g_watchface_fw_tick_handler) {
    return false;
  }

  *timestamp = kernel_wall_clock_get();
  gmtime_r(timestamp, tick_time);
  *units_changed = s_first_tick ? 0 : SECOND_UNIT;
  if (!s_first_tick && s_last_time.tm_min != tick_time->tm_min) {
    *units_changed |= MINUTE_UNIT;
  }
  if (!s_first_tick && s_last_time.tm_hour != tick_time->tm_hour) {
    *units_changed |= HOUR_UNIT;
  }
  s_last_time = *tick_time;
  s_first_tick = false;
  *handler = g_watchface_fw_tick_handler;
  return true;
#else
  TickTimerServiceState *state = &s_app_state.tick_state;

  for (;;) {
    PebbleEvent event;
    if (k_msgq_get(&s_app_event_queue, &event, K_FOREVER) != 0 ||
        event.type != PEBBLE_TICK_EVENT || !state->handler) {
      continue;
    }

    struct tm current;
    gmtime_r(&event.clock_tick.tick_time, &current);
    TimeUnits changed = 0;
    if (!state->first_tick) {
      if (state->last_time.tm_sec != current.tm_sec) {
        changed |= SECOND_UNIT;
      }
      if (state->last_time.tm_min != current.tm_min) {
        changed |= MINUTE_UNIT;
      }
      if (state->last_time.tm_hour != current.tm_hour) {
        changed |= HOUR_UNIT;
      }
      if (state->last_time.tm_mday != current.tm_mday) {
        changed |= DAY_UNIT;
      }
      if (state->last_time.tm_mon != current.tm_mon) {
        changed |= MONTH_UNIT;
      }
      if (state->last_time.tm_year != current.tm_year) {
        changed |= YEAR_UNIT;
      }
    }

    const bool is_24h = clock_is_24h_style();
    const bool dispatch = state->first_tick ||
                          (state->tick_units & changed) != 0U ||
                          state->last_is_24h != is_24h;
    state->last_time = current;
    state->last_is_24h = is_24h;
    state->first_tick = false;
    if (dispatch) {
      *tick_time = current;
      *units_changed = changed;
      *handler = state->handler;
      *timestamp = event.clock_tick.tick_time;
      return true;
    }
  }
#endif
}

void watchface_port_render_tick(time_t timestamp) {
  prv_dump_frame(timestamp);
}

#if !defined(PBL_WATCHFACE_IN_FW)
void event_service_init(PebbleEventType type, EventServiceAddSubscriberCallback add_subscriber,
                        EventServiceRemoveSubscriberCallback remove_subscriber) {
  if (type == PEBBLE_TICK_EVENT) {
    s_add_subscriber = add_subscriber;
    s_remove_subscriber = remove_subscriber;
  }
}

void event_service_client_subscribe(EventServiceInfo *service_info) {
  if (service_info->type != PEBBLE_TICK_EVENT) {
    return;
  }
  s_app_tick_client = service_info;
  if (s_add_subscriber) {
    s_add_subscriber(PebbleTask_App);
  }
}

void event_service_client_unsubscribe(EventServiceInfo *service_info) {
  if (s_app_tick_client != service_info) {
    return;
  }
  if (s_remove_subscriber) {
    s_remove_subscriber(PebbleTask_App);
  }
  s_app_tick_client = NULL;
}

void app_event_loop(void) {
  printk("WATCHFACE_UP\n");
  prv_render();

  while (true) {
    PebbleEvent event;
    if (k_msgq_get(&s_app_event_queue, &event, K_FOREVER) != 0) {
      continue;
    }
    if (event.type == PEBBLE_TICK_EVENT && s_app_tick_client) {
      s_app_tick_client->handler(&event, s_app_tick_client->context);
      prv_dump_frame(event.clock_tick.tick_time);
    }
  }
}
#endif

#if !defined(PBL_WATCHFACE_IN_FW)
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
  return true;
}

PebbleTask pebble_task_get_current(void) {
  if (k_current_get() == s_app_thread) {
    return PebbleTask_App;
  }
  if (k_current_get() == s_kernel_thread) {
    return PebbleTask_KernelMain;
  }
  return PebbleTask_NewTimers;
}

TaskHandle_t pebble_task_get_handle_for_task(PebbleTask task) {
  if (task == PebbleTask_App) {
    return s_app_thread;
  }
  if (task == PebbleTask_KernelMain) {
    return s_kernel_thread;
  }
  return NULL;
}
#endif

#if !defined(PBL_WATCHFACE_IN_FW)
TickTimerServiceState *app_state_get_tick_timer_service_state(void) {
  return &s_app_state.tick_state;
}

TickTimerServiceState *worker_state_get_tick_timer_service_state(void) {
  return &s_app_state.tick_state;
}

TickTimerServiceState *kernel_applib_get_tick_timer_service_state(void) {
  static TickTimerServiceState state;
  return &state;
}
#endif

GContext *app_state_get_graphics_context(void) {
  return &s_context;
}

GContext *kernel_ui_get_graphics_context(void) {
  return &s_context;
}

GContext *graphics_context_get_current_context(void) {
  return &s_context;
}

TextRenderState *app_state_get_text_render_state(void) {
  return &s_app_state.text_render_state;
}

bool app_state_get_text_perimeter_debugging_enabled(void) {
  return s_app_state.perimeter_debugging;
}

void app_state_set_text_perimeter_debugging_enabled(bool enabled) {
  s_app_state.perimeter_debugging = enabled;
}

GBitmap *app_state_legacy2_get_2bit_framebuffer(void) {
  return NULL;
}

Heap *app_state_get_heap(void) {
  return NULL;
}

Layer **app_state_get_layer_tree_stack(void) {
  return s_app_state.layer_tree_stack;
}

Layer **kernel_applib_get_layer_tree_stack(void) {
  return s_kernel_layer_tree_stack;
}

bool process_manager_compiled_with_legacy2_sdk(void) {
  return false;
}

bool heap_is_allocated(Heap *heap, void *ptr) {
  ARG_UNUSED(heap);
  ARG_UNUSED(ptr);
  return false;
}

void *applib_malloc(size_t size) {
  return k_heap_alloc(&s_app_heap, size, K_NO_WAIT);
}

void *applib_zalloc(size_t size) {
  void *ptr = applib_malloc(size);
  if (ptr) {
    memset(ptr, 0, size);
  }
  return ptr;
}

void applib_free(void *ptr) {
  if (ptr) {
    k_heap_free(&s_app_heap, ptr);
  }
}

void *task_malloc(size_t size) {
  return applib_malloc(size);
}

void task_free(void *ptr) {
  applib_free(ptr);
}

#if !defined(PBL_WATCHFACE_IN_FW)
void *kernel_zalloc(size_t size) {
  return k_calloc(1, size);
}

void kernel_free(void *ptr) {
  k_free(ptr);
}
#endif

bool sys_resource_is_valid(ResAppNum app_num, uint32_t resource_id) {
  return app_num == SYSTEM_APP && prv_resource_data(resource_id, NULL) != NULL;
}

size_t sys_resource_size(ResAppNum app_num, uint32_t resource_id) {
  size_t size = 0;
  if (app_num == SYSTEM_APP) {
    prv_resource_data(resource_id, &size);
  }
  return size;
}

size_t sys_resource_load_range(ResAppNum app_num, uint32_t resource_id, uint32_t start_bytes,
                               uint8_t *buffer, size_t num_bytes) {
  size_t size;
  const uint8_t *data = app_num == SYSTEM_APP ? prv_resource_data(resource_id, &size) : NULL;
  if (!data || start_bytes >= size) {
    return 0;
  }
  const size_t copy_size = MIN(num_bytes, size - start_bytes);
  memcpy(buffer, data + start_bytes, copy_size);
  return copy_size;
}

uint32_t sys_resource_get_and_cache(ResAppNum app_num, uint32_t resource_id) {
  return sys_resource_is_valid(app_num, resource_id) ? resource_id : 0;
}

const uint8_t *sys_resource_read_only_bytes(ResAppNum app_num, uint32_t resource_id,
                                            size_t *num_bytes_out) {
  if (app_num != SYSTEM_APP) {
    return NULL;
  }
  return prv_resource_data(resource_id, num_bytes_out);
}

bool sys_resource_bytes_are_readonly(void *bytes) {
  const uintptr_t address = (uintptr_t)bytes;
  return (address >= (uintptr_t)sliding_text_emery_resources &&
          address < (uintptr_t)(sliding_text_emery_resources + sliding_text_emery_resources_len)) ||
         (address >= (uintptr_t)s_font_system_data &&
          address < (uintptr_t)(s_font_system_data + sizeof(s_font_system_data)));
}

ResAppNum sys_get_current_resource_num(void) {
  return SYSTEM_APP;
}

ResourceCallbackHandle resource_watch(ResAppNum app_num, uint32_t resource_id,
                                      ResourceChangedCallback callback, void *data) {
  ARG_UNUSED(app_num);
  ARG_UNUSED(resource_id);
  ARG_UNUSED(callback);
  ARG_UNUSED(data);
  return NULL;
}

bool applib_resource_track_mmapped(const void *bytes) {
  ARG_UNUSED(bytes);
  return false;
}

bool applib_resource_is_mmapped(const void *bytes) {
  ARG_UNUSED(bytes);
  return false;
}

bool applib_resource_munmap(const void *bytes) {
  ARG_UNUSED(bytes);
  return false;
}

void *applib_resource_mmap_or_load(ResAppNum app_num, uint32_t resource_id, size_t offset,
                                   size_t length, bool use_aligned) {
  ARG_UNUSED(use_aligned);
  size_t size;
  const uint8_t *data = app_num == SYSTEM_APP ? prv_resource_data(resource_id, &size) : NULL;
  if (!data || offset > size || length > size - offset) {
    return NULL;
  }
  return (void *)(data + offset);
}

void applib_resource_munmap_or_free(void *bytes) {
  ARG_UNUSED(bytes);
}

ResHandle applib_resource_get_handle(uint32_t resource_id) {
  return sys_resource_is_valid(SYSTEM_APP, resource_id) ? (ResHandle)(uintptr_t)resource_id : NULL;
}

GFont fonts_get_system_font(const char *font_key) {
  ARG_UNUSED(font_key);
  return &s_font_system;
}

GFont sys_font_get_system_font(const char *font_key) {
  return fonts_get_system_font(font_key);
}

GFont fonts_load_custom_font(ResHandle handle) {
  switch ((uintptr_t)handle) {
    case FONT_RESOURCE_GOTHAM_BOLD_50:
      return &s_font_bold;
    case FONT_RESOURCE_GOTHAM_LIGHT_50:
      return &s_font_light;
    default:
      return &s_font_system;
  }
}

void fonts_unload_custom_font(GFont font) {
  ARG_UNUSED(font);
}

void sys_font_reload_font(FontInfo *font_info) {
  ARG_UNUSED(font_info);
}

FontInfo *fonts_get_system_emoji_font_for_size(unsigned int font_height) {
  ARG_UNUSED(font_height);
  return NULL;
}

uint8_t fonts_get_font_height(GFont font) {
  return font ? font->max_height : 0;
}

int16_t fonts_get_font_cap_offset(GFont font) {
  return font ? (int16_t)((int16_t)font->max_height * 22 / 100) : 0;
}

PreferredContentSize system_theme_get_default_content_size_for_runtime_platform(void) {
  return PreferredContentSizeLarge;
}

Window *window_create(void) {
  Window *window = applib_zalloc(sizeof(*window));
  if (!window) {
    return NULL;
  }
  const GRect frame = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  layer_init(&window->layer, &frame);
  window->layer.window = window;
  window->layer.update_proc = prv_window_update_proc;
  window->background_color = GColorWhite;
  window->is_fullscreen = true;
  return window;
}

void window_destroy(Window *window) {
  if (!window) {
    return;
  }
  layer_remove_child_layers(&window->layer);
  if (s_top_window == window) {
    s_top_window = NULL;
  }
  applib_free(window);
}

Layer *window_get_root_layer(const Window *window) {
  return window ? (Layer *)&window->layer : NULL;
}

void window_set_background_color(Window *window, GColor background_color) {
  if (!window) {
    return;
  }
  window->background_color = background_color;
  layer_mark_dirty(&window->layer);
}

void window_schedule_render(Window *window) {
  if (window) {
    window->is_render_scheduled = true;
  }
}

// Set by the in-fw privileged system-app launcher (fw_system_app_launch) around
// a system app's main(). When set, a pushed window's load/appear handlers run
// synchronously here, matching shipping app_window_stack_push semantics that a
// system app relies on (e.g. tictoc paints from its tick handler right after the
// push, before the layer its load handler creates would otherwise exist).
// A sandboxed PBW leaves this false: its handlers point into sandbox memory and
// must never be invoked from kernel context.
bool g_fw_privileged_window;

void app_window_stack_push(Window *window, bool animated) {
  ARG_UNUSED(animated);
  s_top_window = window;
  window->on_screen = true;
  if (g_fw_privileged_window && !window->is_loaded) {
    window->is_loaded = true;
    if (window->window_handlers.load) {
      window->window_handlers.load(window);
    }
    if (window->window_handlers.appear) {
      window->window_handlers.appear(window);
    }
  }
  window_schedule_render(window);
}

Window *app_window_stack_get_top_window(void) {
  return s_top_window;
}

GRect watchface_layer_get_unobstructed_bounds_by_value(const Layer *layer) {
  return layer ? layer->bounds : GRectZero;
}

void app_unobstructed_area_service_subscribe(UnobstructedAreaHandlers handlers, void *context) {
  ARG_UNUSED(handlers);
  ARG_UNUSED(context);
}

void app_unobstructed_area_service_unsubscribe(void) {}

Animation *animation_create(void) {
  return applib_zalloc(sizeof(struct Animation));
}

bool animation_set_duration(Animation *animation, uint32_t duration_ms) {
  if (!animation) {
    return false;
  }
  animation->duration_ms = duration_ms;
  return true;
}

bool animation_set_implementation(Animation *animation,
                                  const AnimationImplementation *implementation) {
  if (!animation || !implementation) {
    return false;
  }
  animation->implementation = implementation;
  return true;
}

bool animation_schedule(Animation *animation) {
  if (!animation || !animation->implementation || !animation->implementation->update) {
    return false;
  }
  animation->scheduled = true;
  if (animation->implementation->setup) {
    animation->implementation->setup(animation);
  }
  animation->implementation->update(animation, ANIMATION_NORMALIZED_MAX);
  animation->scheduled = false;
  return true;
}

bool animation_unschedule(Animation *animation) {
  if (!animation) {
    return false;
  }
  const bool was_scheduled = animation->scheduled;
  animation->scheduled = false;
  if (was_scheduled && animation->implementation && animation->implementation->teardown) {
    animation->implementation->teardown(animation);
  }
  return was_scheduled;
}

int32_t watchface_port_time(int32_t *tloc) {
  const int32_t result = (int32_t)kernel_wall_clock_get();
  if (tloc) {
    *tloc = result;
  }
  return result;
}

struct tm *watchface_port_localtime(const int32_t *timep) {
  struct tm *result = &g_sandbox_app_arena.localtime_result;
  const time_t value = timep ? (time_t)*timep : kernel_wall_clock_get();
  return gmtime_r(&value, result);
}

#if !defined(PBL_WATCHFACE_IN_FW)
static const char *prv_small_number(unsigned value) {
  static const char *const numbers[] = {
      "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
      "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
      "seventeen", "eighteen", "nineteen"};
  return value < ARRAY_SIZE(numbers) ? numbers[value] : "";
}

static void prv_fallback_tick(struct tm *tick_time, TimeUnits units_changed) {
  ARG_UNUSED(units_changed);
  static char hour_text[20];
  static char minute_tens[20];
  static char minute_ones[20];
  static TextLayer *layers[3];
  static bool initialized;
  static const char *const tens[] = {"", "", "twenty", "thirty", "forty", "fifty"};

  if (!initialized) {
    Window *window = window_create();
    window_set_background_color(window, GColorBlack);
    for (int i = 0; i < 3; ++i) {
      layers[i] = text_layer_create(GRect(0, 18 + i * 64, PBL_DISPLAY_WIDTH, 58));
      text_layer_set_background_color(layers[i], GColorClear);
      text_layer_set_text_color(layers[i], GColorWhite);
      text_layer_set_font(layers[i], i == 0 ? &s_font_bold : &s_font_light);
      layer_add_child(window_get_root_layer(window), text_layer_get_layer(layers[i]));
    }
    app_window_stack_push(window, false);
    initialized = true;
  }

  unsigned hour = tick_time->tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(hour_text, sizeof(hour_text), "%s", prv_small_number(hour));
  snprintf(minute_tens, sizeof(minute_tens), "%s",
           tick_time->tm_min < 20 ? prv_small_number(tick_time->tm_min)
                                 : tens[tick_time->tm_min / 10]);
  snprintf(minute_ones, sizeof(minute_ones), "%s",
           tick_time->tm_min >= 20 ? prv_small_number(tick_time->tm_min % 10) : "");
  text_layer_set_text(layers[0], hour_text);
  text_layer_set_text(layers[1], minute_tens);
  text_layer_set_text(layers[2], minute_ones);
}

void watchface_start_fallback(void) {
  tick_timer_service_subscribe(MINUTE_UNIT, prv_fallback_tick);
  app_event_loop();
}

NORETURN passert_failed(const char *filename, int line_number, const char *message, ...) {
  printk("WATCHFACE_ASSERT %s:%d %s\n", filename, line_number, message ? message : "");
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN passert_failed_no_message(const char *filename, int line_number) {
  printk("WATCHFACE_ASSERT %s:%d\n", filename, line_number);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN passert_failed_no_message_with_lr(const char *filename, int line_number, uint32_t lr) {
  printk("WATCHFACE_ASSERT %s:%d lr=%08x\n", filename, line_number, lr);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN wtf_with_context(const char *filename, int line_number) {
  const PebbleTask task = pebble_task_get_current();
  printk("WATCHFACE_ASSERT WTF %s:%d task=%u current=%p app=%p kernel=%p\n",
         filename, line_number, (unsigned int)task, k_current_get(),
         s_app_thread, s_kernel_thread);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN wtf(void) {
  wtf_with_context("<unknown>", 0);
}

NORETURN util_assertion_failed(const char *filename, int line) {
  printk("WATCHFACE_ASSERT %s:%d\n", filename, line);
  k_panic();
  CODE_UNREACHABLE;
}
#endif

#if defined(PBL_WATCHFACE_IN_FW)
TickHandler g_watchface_fw_tick_handler;

void watchface_port_tick_subscribe(TimeUnits tick_units, TickHandler handler) {
  ARG_UNUSED(tick_units);
  g_watchface_fw_tick_handler = handler;
}

void watchface_port_tick_unsubscribe(void) {
  g_watchface_fw_tick_handler = NULL;
}
#endif
