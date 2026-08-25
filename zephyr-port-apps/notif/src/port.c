/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#include <zephyr/sys/printk.h>

#include "applib/fonts/fonts.h"
#include "applib/graphics/framebuffer.h"
#include "applib/graphics/gbitmap_png.h"
#include "applib/graphics/gcontext.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text_resources.h"
#include "applib/preferred_content_size.h"
#include "applib/ui/kino/kino_layer.h"
#include "kernel/pebble_tasks.h"
#include "notif_port.h"
#include "pbl/services/blob_db/pin_db.h"
#include "pbl/services/notifications/alerts_preferences_private.h"
#include "pbl/services/notifications/notification_image.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/timeline_resources.h"
#include "util/stringlist.h"
#include "process_state/app_state/app_state.h"
#include "resource/resource.h"
#include "resource/resource_ids.auto.h"
#include "shell/system_theme.h"
#include "system/passert.h"

#define FONT_GOTHIC_18_RESOURCE 1U
#define FONT_GOTHIC_24_BOLD_RESOURCE 2U
#define FONT_GOTHIC_28_RESOURCE 3U
#define FONT_GOTHIC_28_BOLD_RESOURCE 4U

static const uint8_t s_gothic_18[] __aligned(4) = {
#include "notif_gothic_18.pbf.inc"
};
static const uint8_t s_gothic_24_bold[] __aligned(4) = {
#include "notif_gothic_24_bold.pbf.inc"
};
static const uint8_t s_gothic_28[] __aligned(4) = {
#include "notif_gothic_28.pbf.inc"
};
static const uint8_t s_gothic_28_bold[] __aligned(4) = {
#include "notif_gothic_28_bold.pbf.inc"
};
static const uint8_t s_generic_notification_icon_png[] __aligned(4) = {
#include "notif_generic_icon.png.inc"
};

typedef struct {
  TextRenderState text_render_state;
  Layer *layer_tree_stack[LAYER_TREE_STACK_SIZE];
} NotifAppState;

typedef struct {
  KinoReel base;
} NotifIconReel;

static GContext *s_context;
static FrameBuffer *s_framebuffer;
static FontInfo s_font_gothic_18;
static FontInfo s_font_gothic_24_bold;
static FontInfo s_font_gothic_28;
static FontInfo s_font_gothic_28_bold;
static GBitmap s_generic_notification_icon;
static NotifAppState s_app_state;
static Layer *s_kernel_layer_tree_stack[LAYER_TREE_STACK_SIZE];

static const uint8_t *prv_resource_data(uint32_t resource_id, size_t *size_out) {
  const uint8_t *data = NULL;
  size_t size = 0;
  switch (resource_id) {
    case FONT_GOTHIC_18_RESOURCE:
      data = s_gothic_18;
      size = sizeof(s_gothic_18);
      break;
    case FONT_GOTHIC_24_BOLD_RESOURCE:
      data = s_gothic_24_bold;
      size = sizeof(s_gothic_24_bold);
      break;
    case FONT_GOTHIC_28_RESOURCE:
      data = s_gothic_28;
      size = sizeof(s_gothic_28);
      break;
    case FONT_GOTHIC_28_BOLD_RESOURCE:
      data = s_gothic_28_bold;
      size = sizeof(s_gothic_28_bold);
      break;
    default:
      break;
  }
  if (size_out) {
    *size_out = size;
  }
  return data;
}

void notif_port_init(GContext *context, FrameBuffer *framebuffer) {
  s_context = context;
  s_framebuffer = framebuffer;
  if (!gbitmap_init_with_png_data(&s_generic_notification_icon,
                                  s_generic_notification_icon_png,
                                  sizeof(s_generic_notification_icon_png))) {
    printk("NOTIF_ICON_FAIL\n");
    k_panic();
  }
}

void notif_port_fonts_init(void) {
  if (!text_resources_init_font(SYSTEM_APP, FONT_GOTHIC_18_RESOURCE, 0, &s_font_gothic_18) ||
      !text_resources_init_font(SYSTEM_APP, FONT_GOTHIC_24_BOLD_RESOURCE, 0,
                                &s_font_gothic_24_bold) ||
      !text_resources_init_font(SYSTEM_APP, FONT_GOTHIC_28_RESOURCE, 0, &s_font_gothic_28) ||
      !text_resources_init_font(SYSTEM_APP, FONT_GOTHIC_28_BOLD_RESOURCE, 0,
                                &s_font_gothic_28_bold)) {
    printk("NOTIF_FONT_FAIL\n");
    k_panic();
  }
}

uint8_t *notif_port_framebuffer_bytes(size_t *size_out, uint16_t *stride_out) {
  if (size_out) {
    *size_out = framebuffer_get_size_bytes(s_framebuffer);
  }
  if (stride_out) {
    *stride_out = PBL_DISPLAY_WIDTH;
  }
  return s_framebuffer->buffer;
}

void *applib_malloc(size_t size) {
  return k_malloc(size);
}

void *applib_zalloc(size_t size) {
  return k_calloc(1, size);
}

void applib_free(void *ptr) {
  k_free(ptr);
}

void *task_malloc(size_t size) {
  return applib_malloc(size);
}

void *task_malloc_check(size_t size) {
  void *ptr = task_malloc(size);
  PBL_ASSERTN(ptr);
  return ptr;
}

// Timeline deserialize only calls this for all-day/floating items; notification
// cards are neither, so an identity pass-through is correct here.
time_t time_local_to_utc(time_t local_time) {
  return local_time;
}

void *task_zalloc(size_t size) {
  return applib_zalloc(size);
}

void *task_zalloc_check(size_t size) {
  void *ptr = task_zalloc(size);
  PBL_ASSERTN(ptr);
  return ptr;
}

void task_free(void *ptr) {
  applib_free(ptr);
}

void *kernel_zalloc(size_t size) {
  return k_calloc(1, size);
}

void *kernel_zalloc_check(size_t size) {
  void *ptr = kernel_zalloc(size);
  PBL_ASSERTN(ptr);
  return ptr;
}

void *kernel_realloc(void *ptr, size_t size) {
  return k_realloc(ptr, size);
}

void kernel_free(void *ptr) {
  k_free(ptr);
}

GContext *app_state_get_graphics_context(void) {
  return s_context;
}

GContext *kernel_ui_get_graphics_context(void) {
  return s_context;
}

GContext *graphics_context_get_current_context(void) {
  return s_context;
}

TextRenderState *app_state_get_text_render_state(void) {
  return &s_app_state.text_render_state;
}

bool app_state_get_text_perimeter_debugging_enabled(void) {
  return false;
}

void app_state_set_text_perimeter_debugging_enabled(bool enabled) {
  ARG_UNUSED(enabled);
}

GBitmap *app_state_legacy2_get_2bit_framebuffer(void) {
  return NULL;
}

Heap *app_state_get_heap(void) {
  return NULL;
}

bool heap_is_allocated(Heap *heap, void *ptr) {
  ARG_UNUSED(heap);
  ARG_UNUSED(ptr);
  return false;
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

PebbleTask pebble_task_get_current(void) {
  return PebbleTask_KernelMain;
}

void window_schedule_render(struct Window *window) {
  ARG_UNUSED(window);
}

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
  return app_num == SYSTEM_APP ? prv_resource_data(resource_id, num_bytes_out) : NULL;
}

bool sys_resource_bytes_are_readonly(void *bytes) {
  ARG_UNUSED(bytes);
  return true;
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

GFont fonts_get_system_font(const char *font_key) {
  if (!strcmp(font_key, FONT_KEY_GOTHIC_24_BOLD)) {
    return &s_font_gothic_24_bold;
  }
  if (!strcmp(font_key, FONT_KEY_GOTHIC_28_BOLD)) {
    return &s_font_gothic_28_bold;
  }
  if (!strcmp(font_key, FONT_KEY_GOTHIC_28)) {
    return &s_font_gothic_28;
  }
  return &s_font_gothic_18;
}

GFont sys_font_get_system_font(const char *font_key) {
  return fonts_get_system_font(font_key);
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

const char *system_theme_get_font_key(TextStyleFont font) {
  switch (font) {
    case TextStyleFont_Header:
      return FONT_KEY_GOTHIC_24_BOLD;
    case TextStyleFont_Title:
      return FONT_KEY_GOTHIC_28_BOLD;
    case TextStyleFont_Body:
    case TextStyleFont_Subtitle:
      return FONT_KEY_GOTHIC_28;
    case TextStyleFont_Caption:
    case TextStyleFont_Footer:
    default:
      return FONT_KEY_GOTHIC_18;
  }
}

const char *system_theme_get_font_key_for_size(PreferredContentSize size, TextStyleFont font) {
  ARG_UNUSED(size);
  return system_theme_get_font_key(font);
}

PreferredContentSize system_theme_get_content_size(void) {
  return PreferredContentSizeLarge;
}

PreferredContentSize system_theme_get_default_content_size_for_runtime_platform(void) {
  return PreferredContentSizeLarge;
}

PreferredContentSize system_theme_convert_host_content_size_to_runtime_platform(
    PreferredContentSize size) {
  return size;
}

PreferredContentSize preferred_content_size(void) {
  return PreferredContentSizeLarge;
}

// attribute_find / attribute_get_* now come from the real attribute.c that the
// live-notification deserialize path pulls in; the former port stubs would
// collide with them.

size_t string_list_count(StringList *list) {
  ARG_UNUSED(list);
  return 0;
}

char *string_list_get_at(StringList *list, size_t index) {
  ARG_UNUSED(list);
  ARG_UNUSED(index);
  return NULL;
}

const char *i18n_get(const char *string, const void *owner) {
  ARG_UNUSED(owner);
  return string;
}

void i18n_free(const char *string, const void *owner) {
  ARG_UNUSED(string);
  ARG_UNUSED(owner);
}

void i18n_free_all(const void *owner) {
  ARG_UNUSED(owner);
}

char *string_strip_leading_whitespace(char *str) {
  while (*str == ' ') {
    ++str;
  }
  return str;
}

void clock_get_since_time(char *buffer, int buf_size, time_t timestamp) {
  ARG_UNUSED(timestamp);
  snprintf(buffer, buf_size, "Now");
}

void clock_get_until_time(char *buffer, int buf_size, time_t timestamp, int max_relative_hrs) {
  ARG_UNUSED(timestamp);
  ARG_UNUSED(max_relative_hrs);
  snprintf(buffer, buf_size, "Now");
}

status_t pin_db_get(const TimelineItemId *id, TimelineItem *pin) {
  ARG_UNUSED(id);
  ARG_UNUSED(pin);
  return -1;
}

// timeline_item_free_allocated_buffer now comes from the real item.c.

bool alerts_preferences_get_notification_alternative_design(void) {
  return false;
}

NotificationStatusBarStyle alerts_preferences_get_notification_status_bar_style(void) {
  return NotificationStatusBarStyle_Default;
}

const GBitmap *notification_image_lock(const Uuid *item_id) {
  ARG_UNUSED(item_id);
  return NULL;
}

void notification_image_unlock(void) {}

bool notification_image_is_pending(const Uuid *item_id) {
  ARG_UNUSED(item_id);
  return false;
}

bool timeline_resources_is_system(TimelineResourceId timeline_id) {
  return (timeline_id & SYSTEM_RESOURCE_FLAG) != 0;
}

void timeline_resources_get_id(const TimelineResourceInfo *timeline_res, TimelineResourceSize size,
                               AppResourceInfo *res_info_out) {
  ARG_UNUSED(timeline_res);
  ARG_UNUSED(size);
  *res_info_out = (AppResourceInfo) {
      .res_app_num = SYSTEM_APP,
      .res_id = RESOURCE_ID_NOTIFICATION_GENERIC_TINY,
  };
}

static void prv_icon_update_proc(Layer *layer, GContext *ctx) {
  GRect destination = s_generic_notification_icon.bounds;
  grect_align(&destination, &layer->bounds, GAlignCenter, false);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, &s_generic_notification_icon, &destination);
}

KinoReel *kino_reel_create_with_resource_system(ResAppNum app_num, uint32_t resource_id) {
  ARG_UNUSED(app_num);
  ARG_UNUSED(resource_id);
  return task_zalloc(sizeof(NotifIconReel));
}

void kino_reel_destroy(KinoReel *reel) {
  task_free(reel);
}

GSize kino_reel_get_size(KinoReel *reel) {
  return reel ? GSize(25, 25) : GSizeZero;
}

void kino_layer_init(KinoLayer *kino_layer, const GRect *frame) {
  *kino_layer = (KinoLayer) {};
  layer_init(&kino_layer->layer, frame);
  layer_set_update_proc(&kino_layer->layer, prv_icon_update_proc);
  kino_layer->alignment = GAlignCenter;
}

void kino_layer_deinit(KinoLayer *kino_layer) {
  if (kino_layer->player.reel && kino_layer->player.owns_reel) {
    kino_reel_destroy(kino_layer->player.reel);
  }
  layer_deinit(&kino_layer->layer);
}

KinoLayer *kino_layer_create(GRect frame) {
  KinoLayer *layer = task_zalloc(sizeof(*layer));
  if (layer) {
    kino_layer_init(layer, &frame);
  }
  return layer;
}

void kino_layer_destroy(KinoLayer *kino_layer) {
  if (kino_layer) {
    kino_layer_deinit(kino_layer);
    task_free(kino_layer);
  }
}

Layer *kino_layer_get_layer(KinoLayer *kino_layer) {
  return kino_layer ? &kino_layer->layer : NULL;
}

void kino_layer_set_reel(KinoLayer *kino_layer, KinoReel *reel, bool take_ownership) {
  kino_layer->player.reel = reel;
  kino_layer->player.owns_reel = take_ownership;
}

void kino_layer_set_reel_with_resource_system(KinoLayer *kino_layer, ResAppNum app_num,
                                              uint32_t resource_id, bool invert) {
  ARG_UNUSED(invert);
  kino_layer_set_reel(kino_layer, kino_reel_create_with_resource_system(app_num, resource_id),
                      true);
}

void kino_layer_set_alignment(KinoLayer *kino_layer, GAlign alignment) {
  kino_layer->alignment = alignment;
}

GAlign kino_layer_get_alignment(KinoLayer *kino_layer) {
  return kino_layer->alignment;
}

void kino_layer_play(KinoLayer *kino_layer) {
  ARG_UNUSED(kino_layer);
}

LayoutLayer *generic_layout_create(const LayoutLayerConfig *config) {
  ARG_UNUSED(config);
  return NULL;
}

bool generic_layout_verify(bool existing_attributes[]) {
  ARG_UNUSED(existing_attributes);
  return false;
}

LayoutLayer *calendar_layout_create(const LayoutLayerConfig *config) {
  ARG_UNUSED(config);
  return NULL;
}

bool calendar_layout_verify(bool existing_attributes[]) {
  ARG_UNUSED(existing_attributes);
  return false;
}

LayoutLayer *alarm_layout_create(const LayoutLayerConfig *config) {
  ARG_UNUSED(config);
  return NULL;
}

bool alarm_layout_verify(bool existing_attributes[]) {
  ARG_UNUSED(existing_attributes);
  return false;
}

LayoutLayer *health_layout_create(const LayoutLayerConfig *config) {
  ARG_UNUSED(config);
  return NULL;
}

bool health_layout_verify(bool existing_attributes[]) {
  ARG_UNUSED(existing_attributes);
  return false;
}

LayoutLayer *sports_layout_create(const LayoutLayerConfig *config) {
  ARG_UNUSED(config);
  return NULL;
}

bool sports_layout_verify(bool existing_attributes[]) {
  ARG_UNUSED(existing_attributes);
  return false;
}

LayoutLayer *weather_layout_create(const LayoutLayerConfig *config) {
  ARG_UNUSED(config);
  return NULL;
}

bool weather_layout_verify(bool existing_attributes[]) {
  ARG_UNUSED(existing_attributes);
  return false;
}

NORETURN passert_failed(const char *filename, int line_number, const char *message, ...) {
  printk("NOTIF_ASSERT %s:%d %s\n", filename, line_number, message ? message : "");
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN passert_failed_no_message(const char *filename, int line_number) {
  printk("NOTIF_ASSERT %s:%d\n", filename, line_number);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN passert_failed_no_message_with_lr(const char *filename, int line_number, uint32_t lr) {
  printk("NOTIF_ASSERT %s:%d lr=%08x\n", filename, line_number, lr);
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN wtf(void) {
  printk("NOTIF_ASSERT WTF\n");
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN util_assertion_failed(const char *filename, int line) {
  printk("NOTIF_ASSERT %s:%d\n", filename, line);
  k_panic();
  CODE_UNREACHABLE;
}
