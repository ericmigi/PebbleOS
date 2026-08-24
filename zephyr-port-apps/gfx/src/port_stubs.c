/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#include <zephyr/sys/printk.h>

#include "applib/graphics/gcontext.h"
#include "process_state/app_state/app_state.h"
#include "resource/resource.h"
#include "system/passert.h"

#define GFX_FONT_RESOURCE_ID 1u

static const uint8_t s_font_data[] __aligned(4) = {
#include "leco_60_numbers.pbf.inc"
};

static GContext *s_context;
static TextRenderState s_text_render_state;
static bool s_perimeter_debugging_enabled;

void gfx_port_set_context(GContext *context) {
  s_context = context;
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

void *kernel_zalloc(size_t size) {
  return k_calloc(1, size);
}

void kernel_free(void *ptr) {
  k_free(ptr);
}

bool process_manager_compiled_with_legacy2_sdk(void) {
  return false;
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
  return &s_text_render_state;
}

bool app_state_get_text_perimeter_debugging_enabled(void) {
  return s_perimeter_debugging_enabled;
}

void app_state_set_text_perimeter_debugging_enabled(bool enabled) {
  s_perimeter_debugging_enabled = enabled;
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

bool sys_resource_is_valid(ResAppNum app_num, uint32_t resource_id) {
  return app_num == SYSTEM_APP && resource_id == GFX_FONT_RESOURCE_ID;
}

size_t sys_resource_size(ResAppNum app_num, uint32_t resource_id) {
  return sys_resource_is_valid(app_num, resource_id) ? sizeof(s_font_data) : 0;
}

size_t sys_resource_load_range(ResAppNum app_num, uint32_t resource_id,
                               uint32_t start_bytes, uint8_t *buffer, size_t num_bytes) {
  const size_t resource_size = sys_resource_size(app_num, resource_id);
  if (start_bytes >= resource_size) {
    return 0;
  }
  const size_t available = resource_size - start_bytes;
  const size_t copy_size = MIN(num_bytes, available);
  memcpy(buffer, &s_font_data[start_bytes], copy_size);
  return copy_size;
}

uint32_t sys_resource_get_and_cache(ResAppNum app_num, uint32_t resource_id) {
  return sys_resource_is_valid(app_num, resource_id) ? resource_id : 0;
}

const uint8_t *sys_resource_read_only_bytes(ResAppNum app_num, uint32_t resource_id,
                                            size_t *num_bytes_out) {
  if (!sys_resource_is_valid(app_num, resource_id)) {
    return NULL;
  }
  if (num_bytes_out) {
    *num_bytes_out = sizeof(s_font_data);
  }
  return s_font_data;
}

bool sys_resource_bytes_are_readonly(void *bytes) {
  return bytes >= (void *)s_font_data && bytes < (void *)(s_font_data + sizeof(s_font_data));
}

ResAppNum sys_get_current_resource_num(void) {
  return SYSTEM_APP;
}

GFont sys_font_get_system_font(const char *font_key) {
  ARG_UNUSED(font_key);
  return NULL;
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

void *applib_resource_mmap_or_load(ResAppNum app_num, uint32_t resource_id,
                                   size_t offset, size_t length, bool use_aligned) {
  ARG_UNUSED(use_aligned);
  if (offset > sys_resource_size(app_num, resource_id) ||
      length > sys_resource_size(app_num, resource_id) - offset) {
    return NULL;
  }
  return (void *)&s_font_data[offset];
}

void applib_resource_munmap_or_free(void *bytes) {
  ARG_UNUSED(bytes);
}

NORETURN passert_failed(const char *filename, int line_number, const char *message, ...) {
  printk("GFX_ASSERT %s:%d %s\n", filename, line_number, message);
  k_panic();
  for (;;) {}
}

NORETURN passert_failed_no_message(const char *filename, int line_number) {
  printk("GFX_ASSERT %s:%d\n", filename, line_number);
  k_panic();
  for (;;) {}
}

NORETURN passert_failed_no_message_with_lr(const char *filename, int line_number, uint32_t lr) {
  printk("GFX_ASSERT %s:%d lr=%08x\n", filename, line_number, lr);
  k_panic();
  for (;;) {}
}

NORETURN wtf(void) {
  printk("GFX_ASSERT WTF\n");
  k_panic();
  for (;;) {}
}

NORETURN util_assertion_failed(const char *filename, int line) {
  printk("GFX_ASSERT %s:%d\n", filename, line);
  k_panic();
  for (;;) {}
}
