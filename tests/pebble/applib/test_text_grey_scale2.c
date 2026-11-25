/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <applib/graphics/framebuffer.h>
#include <applib/graphics/gcontext.h>
#include <applib/graphics/text_detail.h>
#include <applib/graphics/text_render.h>
#include <applib/graphics/text_resources.h>
#include <clar.h>
#include <kernel/pebble_tasks.h>
#include <process_management/app_manager.h>
#include <resource/resource.h>

static FrameBuffer s_fb;
static GBitmap s_bitmap;
static GContext s_ctx;
static bool s_edges;
static PebbleTask s_task;
static FontInfo s_font;
// Each of the four panel pixels has coverage, in order, of 1, 2, 0 and 3.
static const struct PBL_PACKED {
  GlyphHeaderData header;
  uint8_t bitmap[4];
  uint8_t edge_count[2];
  uint8_t edge[3];
} s_glyph = {{.width_px = 1, .height_px = 1}, {1}, {1, 0}, {0, 0, 0xc9}};

PebbleTask pebble_task_get_current(void) {
  return s_task;
}

const PebbleProcessMd *app_manager_get_current_app_md(void) {
  static const PebbleProcessMd app = {.is_unprivileged = true};
  return &app;
}

uint32_t *framebuffer_get_line(FrameBuffer *fb, uint16_t row) {
  return &fb->buffer[row * FRAMEBUFFER_WORDS_PER_ROW];
}

FrameBuffer *compositor_get_framebuffer(void) {
  return &s_fb;
}

GBitmap *graphics_context_get_bitmap(GContext *ctx) {
  return &s_bitmap;
}

GContext *kernel_ui_get_graphics_context(void) {
  return &s_ctx;
}

void graphics_context_mark_dirty_rect(GContext *ctx, GRect rect) {
}

const GlyphData *text_resources_get_glyph(FontCache *cache, Codepoint code, FontInfo *font,
                                          GlyphLocation *location) {
  *location = (GlyphLocation){};
  return (const GlyphData *)&s_glyph;
}

bool text_resources_glyph_has_edges(const GlyphLocation *location) {
  return s_edges;
}

bool text_resources_glyph_is_aa2(const GlyphLocation *location) {
  return false;
}

bool text_resources_glyph_is_scale2(const GlyphLocation *location) {
  return false;
}

void test_text_grey_scale2__initialize(void) {
  memset(s_fb.buffer, 0xff, sizeof(s_fb.buffer));
  s_bitmap = (GBitmap){
    .addr = s_fb.buffer,
    .row_size_bytes = FRAMEBUFFER_BYTES_PER_ROW,
    .bounds = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT),
    .info.is_scaled2 = true,
  };
  s_ctx.draw_state.clip_box = s_bitmap.bounds;
  s_ctx.draw_state.text_color = GColorBlack;
  s_font.base.app_num = SYSTEM_APP;
  s_edges = true;
  s_task = PebbleTask_KernelMain;
  text_detail_set_grey_supported(true);
  text_detail_set_mode(TextDetailModeSubpixelGrey);
  text_detail_kernel_frame_begin(true);
}

void test_text_grey_scale2__unsupported_panel_uses_monochrome_edges(void) {
  text_detail_set_grey_supported(false);
  render_glyph(&s_ctx, 'A', &s_font, GRectZero);
  cl_assert_equal_i(1, s_fb.buffer[0] & 3);
  cl_assert_equal_i(1, s_fb.buffer[FRAMEBUFFER_WORDS_PER_ROW] & 3);
  cl_assert(!text_detail_wants_grey());
}

void test_text_grey_scale2__custom_font_thresholds_its_own_edges(void) {
  s_font.base.app_num = SYSTEM_APP + 1;
  render_glyph(&s_ctx, 'A', &s_font, GRectZero);
  cl_assert_equal_i(1, s_fb.buffer[0] & 3);
  cl_assert_equal_i(1, s_fb.buffer[FRAMEBUFFER_WORDS_PER_ROW] & 3);
  cl_assert(!text_detail_wants_grey());
}

void test_text_grey_scale2__unprivileged_app_keeps_thresholded_system_font(void) {
  s_task = PebbleTask_App;
  render_glyph(&s_ctx, 'A', &s_font, GRectZero);
  cl_assert_equal_i(1, s_fb.buffer[0] & 3);
  cl_assert_equal_i(1, s_fb.buffer[FRAMEBUFFER_WORDS_PER_ROW] & 3);
  cl_assert(!text_detail_wants_grey());
}

void test_text_grey_scale2__accepted_glyph_still_refines_when_the_record_pool_fills(void) {
  render_glyph(&s_ctx, 'A', &s_font, GRectZero);
  render_glyph(&s_ctx, 'B', &s_font, GRect(1, 0, 1, 1));
  cl_assert_equal_i(4, s_fb.buffer[0] & 15);
  cl_assert_equal_i(5, s_fb.buffer[FRAMEBUFFER_WORDS_PER_ROW] & 15);
  uint8_t cells[PBL_DISPLAY_WIDTH * 2 / 4];
  cl_assert(text_detail_row(&(TextDetailFrame){.grey = true}, 0, (uint8_t *)s_fb.buffer, cells));
  cl_assert_equal_i(9, cells[0]);
}

void test_text_grey_scale2__ordinary_one_bit_font_keeps_its_pixels(void) {
  s_edges = false;
  render_glyph(&s_ctx, 'A', &s_font, GRectZero);
  cl_assert_equal_i(0, s_fb.buffer[0] & 3);
  cl_assert_equal_i(0, s_fb.buffer[FRAMEBUFFER_WORDS_PER_ROW] & 3);
  cl_assert(!text_detail_wants_grey());
}
