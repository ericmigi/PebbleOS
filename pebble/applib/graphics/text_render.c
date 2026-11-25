/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "text_render.h"

#include "framebuffer.h"
#include "gcontext.h"
#include "graphics_private.h"
#include "text_resources.h"

#include <string.h>

#include <pbl/kernel/assert.h>
#include <pbl/util/bitops.h>
#include <pbl/util/math.h>
#include <pbl/util/testing.h>

#include <process_state/app_state/app_state.h>
#include <syscall/syscall.h>

#ifdef CONFIG_TEXT_DETAIL
#include "text_detail.h"
#endif

typedef uint32_t GlyphBlock PBL_ALIGNED(1);

#if !defined(__clang__)
#pragma GCC optimize("O2")
#endif

static GRect get_glyph_rect(const GlyphData *glyph) {
  GRect r = {
    .size.w = glyph->header.width_px,
    .size.h = glyph->header.height_px,
    .origin.x = glyph->header.left_offset_px,
    .origin.y = glyph->header.top_offset_px
  };

  return r;
}

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 8
/// This function returns the x coordinate of where to write the contents of a given word (32-bits)
/// of data from the 1-bit frame buffer into the 8-bit framebuffer
/// @param dest_bitmap 8-bit destination frame buffer bitmap
/// @param block_addr source address in 1-bit frame buffer of where the word is being updated
///                   within a given row; assumed to be zero-based
/// @param y_offset row offset within the source 1-bit frame buffer
PBL_T_STATIC int32_t prv_convert_1bit_addr_to_8bit_x(GBitmap *dest_bitmap, uint32_t *block_addr,
                                                     int32_t y_offset) {
  // Each byte block_addr corresponds to 8 pixels (i.e. 4-bytes in the 8-bit frame buffer).
  // Thus multiply by 8 to get the word offset within the destination 8-bit frame buffer.
  // Also need to account for the fact that the 1-bit frame buffer has 16 bits of unused space
  // on each row (thus 16 bytes need to be subtracted from the destination address since there is
  // no padding on each row of the 8-bit frame buffer.
  const int32_t padding = (32 - (dest_bitmap->bounds.size.w % 32)) % 32;
  // Calculate the overall offset in the 8-bit bitmap
  const int32_t bitmap_offset_8bit = ((uintptr_t)block_addr * 8) - (padding * y_offset);
  // Calculate just the offset from the start of the target row in the 8-bit bitmap (i.e. "x")
  return bitmap_offset_8bit - (dest_bitmap->bounds.size.w * y_offset);
}
#endif

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 8
#define COLOR_GLYPH_CHUNK_SIZE 64

typedef struct {
  const FontResource *font_res;
  uint32_t offset;
  uint32_t end;
  uint8_t pos;
  uint8_t len;
  uint8_t buf[COLOR_GLYPH_CHUNK_SIZE];
} ColorGlyphReader;

typedef struct {
  ColorGlyphReader reader;
  ColorGlyphMode mode;
  uint8_t bpp;
  uint8_t byte;
  uint8_t bits_left;
  uint8_t run_left;
  uint8_t run_value;
} ColorGlyphDecoder;

static bool prv_read(ColorGlyphReader *r, uint8_t *out, size_t len) {
  const size_t n =
      sys_resource_load_range(r->font_res->app_num, r->font_res->resource_id, r->offset, out, len);
  r->offset += len;
  return n == len;
}

static bool prv_read_byte(ColorGlyphReader *r, uint8_t *byte) {
  if (r->pos == r->len) {
    if (r->offset >= r->end) {
      return false;
    }
    r->len = PBL_MIN(sizeof(r->buf), r->end - r->offset);
    r->pos = 0;
    if (!prv_read(r, r->buf, r->len)) {
      return false;
    }
  }
  *byte = r->buf[r->pos++];
  return true;
}

static bool prv_decode_next(ColorGlyphDecoder *d, uint8_t *value) {
  if (d->mode == ColorGlyphModeRle) {
    if (d->run_left == 0) {
      uint8_t run;
      if (!prv_read_byte(&d->reader, &run)) {
        return false;
      }
      d->run_left = (run >> 4) + 1;
      d->run_value = run & 0x0F;
    }
    d->run_left--;
    *value = d->run_value;
    return true;
  }

  if (d->bits_left == 0) {
    if (!prv_read_byte(&d->reader, &d->byte)) {
      return false;
    }
    d->bits_left = 8;
  }
  d->bits_left -= d->bpp;
  *value = (d->byte >> d->bits_left) & ((1 << d->bpp) - 1);
  return true;
}

// Kept out of line so its decode buffers aren't on render_glyph's frame while the glyph loads,
// which legacy apps' 2 KiB stack can't afford
static PBL_NOINLINE void prv_render_color_glyph(GContext *ctx, const GlyphData *glyph,
                                                const GlyphLocation *location, const GRect *target,
                                                const GRect *clipped) {
  ColorGlyphDecoder d = {
    .reader = {
      .font_res = location->font_res,
      .offset = location->offset + sizeof(GlyphHeaderData)
    },
  };

  ColorGlyphHeader header;
  if (!prv_read(&d.reader, (uint8_t *)&header, sizeof(header)) ||
      header.palette_size > COLOR_GLYPH_MAX_PALETTE) {
    return;
  }
  d.mode = COLOR_GLYPH_MODE(header.encoding);
  d.bpp = COLOR_GLYPH_BPP(header.encoding);

  GColor palette[COLOR_GLYPH_MAX_PALETTE];
  if (d.mode == ColorGlyphModeTinted) {
    palette[0] = GColorClear;
    palette[1] = ctx->draw_state.text_color;
    if (ctx->draw_state.compositing_mode != GCompOpSet) {
      palette[1].a = 3;
    }
  } else if (!prv_read(&d.reader, (uint8_t *)palette, header.palette_size)) {
    return;
  }
  d.reader.end = d.reader.offset + header.data_len;

  GBitmap *dest_bitmap = graphics_context_get_bitmap(ctx);
  const bool direct = (d.mode != ColorGlyphModeTinted) && (d.bpp == 8);
  const uint8_t num_colors = (d.mode == ColorGlyphModeTinted) ? 2 : header.palette_size;
  const int16_t clip_max_x = grect_get_max_x(clipped);
  const int16_t clip_max_y = grect_get_max_y(clipped);

  for (int16_t y = 0; y < glyph->header.height_px; y++) {
    const int16_t dest_y = target->origin.y + y;
    if (dest_y >= clip_max_y) {
      break;
    }
    const bool row_visible = (dest_y >= clipped->origin.y);
    const GBitmapDataRowInfo row =
        row_visible ? gbitmap_get_data_row_info(dest_bitmap, dest_y) : (GBitmapDataRowInfo){};

    // Raw rows are byte-aligned
    d.bits_left = 0;

    for (int16_t x = 0; x < glyph->header.width_px; x++) {
      uint8_t value;
      if (!prv_decode_next(&d, &value)) {
        return;
      }

      const int16_t dest_x = target->origin.x + x;
      if (!row_visible || dest_x < clipped->origin.x || dest_x >= clip_max_x ||
          dest_x < row.min_x || dest_x > row.max_x) {
        continue;
      }

      GColor color;
      if (direct) {
        color.argb = value;
      } else if (value < num_colors) {
        color = palette[value];
      } else {
        continue;
      }

      uint8_t *pixel = &row.data[dest_x];
      if (color.a == 3) {
        *pixel = color.argb;
      } else if (color.a != 0) {
        *pixel = gcolor_alpha_blend(color, (GColor){.argb = *pixel}).argb;
      }
    }
  }
}
#endif

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 1
// Coverage of two horizontally adjacent glyph pixels, 0 to 3 each in 2 bits, left one first
static PBL_ALWAYS_INLINE unsigned prv_pixel_pair(const GlyphBlock *bits, bool aa2, uint32_t p) {
  if (aa2) {
    const uint32_t shift = 2 * (p % 16);
    uint32_t v = bits[p / 16] >> shift;
    if (shift == 30) {
      v |= bits[p / 16 + 1] << 2;
    }
    return v & 0x0FU;
  }
  const uint32_t shift = p % 32;
  uint32_t v = bits[p / 32] >> shift;
  if (shift == 31) {
    v |= bits[p / 32 + 1] << 1;
  }
  return ((v & 1U) * 0x3U) | (((v >> 1) & 1U) * 0xCU);
}

// Total coverage of a pair of glyph pixels
static const uint8_t s_pair_sum[16] = {0, 1, 2, 3, 1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6};

// A glyph stored at twice its font's resolution: each framebuffer pixel covers 2x2 glyph pixels,
// with 1 or 2 bits of coverage each, and is inked when at least half covered. The pixels at the
// glyph's edges are recorded with the coverage of their four, for displays that can show them at
// panel resolution and in grey.
static PBL_NOINLINE void prv_render_scale2_glyph(GContext *ctx, const GlyphData *glyph,
                                                 const GlyphLocation *location,
                                                 const GRect cursor) {
  const int w2 = glyph->header.width_px;
  const int h2 = glyph->header.height_px;
  if (w2 == 0 || h2 == 0) {
    return;
  }
  // The bitmap's position, in glyph pixels
  const int x2 = 2 * cursor.origin.x + glyph->header.left_offset_px;
  const int y2 = 2 * (cursor.origin.y + location->baseline_adjust) + glyph->header.top_offset_px;
  // Arithmetic shifts round down for negative positions too
  const int cx0 = x2 >> 1;
  const int cy0 = y2 >> 1;
  GRect cells = GRect(cx0, cy0, ((x2 + w2 + 1) >> 1) - cx0, ((y2 + h2 + 1) >> 1) - cy0);
  grect_clip(&cells, &ctx->draw_state.clip_box);
  if (cells.size.w <= 0 || cells.size.h <= 0) {
    return;
  }

  GBitmap *dest = graphics_context_get_bitmap(ctx);
  const bool white = !gcolor_equal(ctx->draw_state.text_color, GColorBlack);
  const GlyphBlock *bits = (const GlyphBlock *)glyph->data;
  const bool aa2 = text_resources_glyph_is_aa2(location);
#ifdef CONFIG_TEXT_DETAIL
  const int owner = text_detail_glyph_begin(dest);
#endif
  for (int cy = cells.origin.y; cy < cells.origin.y + cells.size.h; cy++) {
    const GBitmapDataRowInfo row = gbitmap_get_data_row_info(dest, cy);
    // Pixel offsets of the cell row's two glyph rows, or -1 outside the glyph
    const int sy = 2 * cy - y2;
    const int base[2] = {
      (sy >= 0 && sy < h2) ? sy * w2 : -1, (sy + 1 >= 0 && sy + 1 < h2) ? (sy + 1) * w2 : -1
    };
    const int x_min = PBL_MAX(cells.origin.x, row.min_x);
    const int x_max = PBL_MIN(cells.origin.x + cells.size.w - 1, row.max_x);
    for (int cx = x_min; cx <= x_max; cx++) {
      const int sx = 2 * cx - x2;
      // Coverage, 0 to 3, of the four glyph pixels, two bits each: top left first
      uint8_t levels = 0;
      if (sx >= 0 && sx + 1 < w2 && base[0] >= 0 && base[1] >= 0) {
        levels = (uint8_t)(prv_pixel_pair(bits, aa2, (uint32_t)(base[0] + sx)) |
                           (prv_pixel_pair(bits, aa2, (uint32_t)(base[1] + sx)) << 4));
      } else {
        // At the glyph's edges: the pixels outside it are uncovered
        for (int i = 0; i < 4; i++) {
          const int x = sx + (i & 1);
          if (x < 0 || x >= w2 || base[i >> 1] < 0) {
            continue;
          }
          const uint32_t pixel = (uint32_t)(base[i >> 1] + x);
          const unsigned level = aa2 ? (bits[pixel / 16] >> (2 * (pixel % 16))) & 3U
                                     : ((bits[pixel / 32] >> (pixel % 32)) & 1U) * 3U;
          levels |= (uint8_t)(level << (2 * i));
        }
      }
      const unsigned sum = s_pair_sum[levels & 0x0FU] + s_pair_sum[levels >> 4];
      if (levels == 0) {
        continue;
      }
      // Inked when at least half covered
      if (sum >= 6) {
        if (white) {
          row.data[cx / 8] |= (uint8_t)(1U << (cx % 8));
        } else {
          row.data[cx / 8] &= (uint8_t)~(1U << (cx % 8));
        }
      }
#ifdef CONFIG_TEXT_DETAIL
      if (levels != 0xFF && owner >= 0) {
        text_detail_add(owner, cx, cy, levels, white);
      }
#endif
    }
  }
#ifdef CONFIG_TEXT_DETAIL
  text_detail_glyph_end(owner);
#endif
  graphics_context_mark_dirty_rect(ctx, cells);
}
#endif

#ifdef CONFIG_TEXT_DETAIL
// Records the edges a glyph of a FEATURE_EDGES font carries after its bitmap, those in the clip
static PBL_NOINLINE void prv_record_edges(GContext *ctx, const GlyphData *glyph,
                                          const GRect *target) {
  GBitmap *dest = graphics_context_get_bitmap(ctx);
  const int owner = text_detail_glyph_begin(dest);
  if (owner < 0) {
    return;
  }
  const uint8_t *data = (const uint8_t *)glyph->data +
                        ((glyph->header.width_px * glyph->header.height_px + 31) / 32) * 4;
  const uint16_t count = (uint16_t)(data[0] | (data[1] << 8));
  const bool white = !gcolor_equal(ctx->draw_state.text_color, GColorBlack);
  text_detail_add_edges(owner, target->origin, &ctx->draw_state.clip_box, data + 2, count, white);
  text_detail_glyph_end(owner);
}
#endif

#ifdef CONFIG_FRAMEBUFFER_SCALE2
// Mask words per row of a glyph drawn at twice its size: any glyph width, up to 255 pixels
#define SCALED2_GLYPH_WORDS 16

// n (up to 8) bits of a glyph bitmap from bit p, LSB first
static PBL_ALWAYS_INLINE unsigned prv_glyph_bits(const uint8_t *bits, uint32_t p, unsigned n) {
  unsigned v = bits[p / 8];
  if ((p % 8) + n > 8) {
    v |= (unsigned)bits[p / 8 + 1] << 8;
  }
  return (v >> (p % 8)) & ((1U << n) - 1U);
}

// Writes the set bits of a mask row whose bit 0 is native column x0 into native row y, within
// native columns [cx0, cx1)
static void prv_write_scaled2_row(GBitmap *dest, int y, int x0, int cx0, int cx1,
                                  const uint32_t *mask, int words, bool white) {
  uint32_t *row = (uint32_t *)((uint8_t *)dest->addr + y * dest->row_size_bytes);
  for (int k = 0; k < words; k++) {
    uint32_t v = mask[k];
    const int bx = x0 + 32 * k;
    if (v == 0 || bx >= cx1 || bx + 32 <= cx0) {
      continue;
    }
    if (cx0 > bx) {
      v &= ~0U << (cx0 - bx);
    }
    if (cx1 - bx < 32) {
      v &= (1U << (cx1 - bx)) - 1U;
    }
    // Arithmetic floor for columns left of 0, whose bits are clipped off by now
    const int sh = ((bx % 32) + 32) % 32;
    const int wi = (bx - sh) / 32;
    const uint32_t lo = v << sh;
    const uint32_t hi = sh ? (v >> (32 - sh)) : 0U;
    if (wi >= 0) {
      row[wi] = white ? (row[wi] | lo) : (row[wi] & ~lo);
    }
    if (hi) {
      row[wi + 1] = white ? (row[wi + 1] | hi) : (row[wi + 1] & ~hi);
    }
  }
}

// Draws a 1-bit glyph into a framebuffer at twice its size: each pixel as 2x2, except the edge
// pixels of a FEATURE_EDGES glyph, whose four are inked from their own coverage (at least half)
static PBL_NOINLINE void prv_render_glyph_scaled2(GContext *ctx, const GlyphData *glyph,
                                                  bool has_edges, const GRect *target,
                                                  const GRect *clipped) {
  const int w = glyph->header.width_px;
  const int h = glyph->header.height_px;
  if (w == 0 || h == 0 || w > SCALED2_GLYPH_WORDS * 16) {
    return;
  }
  GBitmap *dest = graphics_context_get_bitmap(ctx);
  const bool white = !gcolor_equal(ctx->draw_state.text_color, GColorBlack);
  const uint8_t *bits = (const uint8_t *)glyph->data;
  const uint8_t *edges = bits + ((w * h + 31) / 32) * 4;
  const unsigned count = has_edges ? (unsigned)(edges[0] | (edges[1] << 8)) : 0U;
  edges += 2;
  unsigned e = 0;
  const int words = (2 * w + 31) / 32;
  const int x0 = 2 * target->origin.x;
  const int cx0 = 2 * clipped->origin.x;
  const int cx1 = 2 * grect_get_max_x(clipped);
  for (int gy = 0; gy < h; gy++) {
    const int y = target->origin.y + gy;
    if (y >= grect_get_max_y(clipped)) {
      break;
    }
    if (y < clipped->origin.y) {
      while (e < count && edges[3 * e + 1] == gy) {
        e++;
      }
      continue;
    }
    uint32_t mask[2][SCALED2_GLYPH_WORDS];
    memset(mask[0], 0, words * sizeof(uint32_t));
    for (int gx = 0; gx < w; gx += 8) {
      const unsigned n = (unsigned)PBL_MIN(8, w - gx);
      const unsigned v = prv_glyph_bits(bits, (uint32_t)(gy * w + gx), n);
      if (v) {
        const uint32_t doubled = graphics_private_double_bits16((uint16_t)v);
        // gx is a multiple of 8: the 16 bits never straddle a word
        mask[0][gx / 16] |= doubled << (2 * gx % 32);
      }
    }
    memcpy(mask[1], mask[0], words * sizeof(uint32_t));
    for (; e < count && edges[3 * e + 1] == gy; e++) {
      const int ex = edges[3 * e];
      const unsigned levels = edges[3 * e + 2];
      if (ex >= w) {
        // Not in the glyph: the font is malformed
        continue;
      }
      for (int i = 0; i < 4; i++) {
        const int c = 2 * ex + (i & 1);
        const uint32_t bit = 1U << (c % 32);
        if (((levels >> (2 * i)) & 3U) >= 2U) {
          mask[i >> 1][c / 32] |= bit;
        } else {
          mask[i >> 1][c / 32] &= ~bit;
        }
      }
    }
    prv_write_scaled2_row(dest, 2 * y, x0, cx0, cx1, mask[0], words, white);
    prv_write_scaled2_row(dest, 2 * y + 1, x0, cx0, cx1, mask[1], words, white);
  }
}
#endif

// PRO TIP: if you have to modify this function, expect to waste the rest of your day on it
void render_glyph(GContext *const ctx, const uint32_t codepoint, FontInfo *const font,
                  const GRect cursor) {
  if (codepoint_is_special(codepoint)) {
    TextRenderState *state = app_state_get_text_render_state();
    if (state->special_codepoint_handler_cb) {
      state->special_codepoint_handler_cb(ctx, codepoint, cursor,
                                          state->special_codepoint_handler_context);
    }
    return;
  }

  GlyphLocation location;
  const GlyphData *glyph = text_resources_get_glyph(&ctx->font_cache, codepoint, font, &location);

  PBL_ASSERTN(glyph);
  // Bitfiddle the metrics data:
  GRect glyph_metrics = get_glyph_rect(glyph);
  // Sit a substituted glyph on the primary font's baseline. Must happen before the target and
  // clipping rects are derived from it.
  glyph_metrics.origin.y += location.baseline_adjust;

  // Calculate the box that we intend to draw to the screen, in screen coordinates
  GRect glyph_target = {
    .origin =
        {.x = cursor.origin.x + glyph_metrics.origin.x,
         .y = cursor.origin.y + glyph_metrics.origin.y},
    .size = {.w = glyph_metrics.size.w, .h = glyph_metrics.size.h}
  };

  // The destination bitmap's x-coordinate and row advance. Used in the loop below.
  GBitmap *dest_bitmap = graphics_context_get_bitmap(ctx);
  const int32_t x = (int32_t)((int16_t)cursor.origin.x + (int16_t)glyph_metrics.origin.x);

  // Now clip that box against the screen/other UI elements. This rect will be the rect that we
  // actually fill with bits on the screen.
  GRect clipped_glyph_target = glyph_target;
  grect_clip(&clipped_glyph_target, &ctx->draw_state.clip_box);

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 8
  if (text_resources_glyph_is_color(&location)) {
    if (clipped_glyph_target.size.h > 0 && clipped_glyph_target.size.w > 0) {
      prv_render_color_glyph(ctx, glyph, &location, &glyph_target, &clipped_glyph_target);
      graphics_context_mark_dirty_rect(ctx, clipped_glyph_target);
    }
    return;
  }
#endif

#ifdef CONFIG_FRAMEBUFFER_SCALE2
  if (gbitmap_is_scaled2(dest_bitmap)) {
    if (!text_resources_glyph_is_scale2(&location) && clipped_glyph_target.size.w > 0 &&
        clipped_glyph_target.size.h > 0) {
      prv_render_glyph_scaled2(ctx, glyph, text_resources_glyph_has_edges(&location), &glyph_target,
                               &clipped_glyph_target);
      graphics_context_mark_dirty_rect(ctx, clipped_glyph_target);
    }
    return;
  }
#endif

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 1
  if (text_resources_glyph_is_scale2(&location)) {
    prv_render_scale2_glyph(ctx, glyph, &location, cursor);
    return;
  }
#endif

#ifdef CONFIG_TEXT_DETAIL
  // An empty glyph (a space) has no body to carry edges
  if (text_resources_glyph_has_edges(&location) && glyph_target.size.w && glyph_target.size.h) {
    prv_record_edges(ctx, glyph, &glyph_target);
  }
#endif

  // The number of bits to be clipped off the edges
  const int left_clip = clipped_glyph_target.origin.x - glyph_target.origin.x;
  const int right_clip =
      PBL_MIN(glyph_target.size.w,
              PBL_MAX(0, glyph_target.size.w - clipped_glyph_target.size.w - left_clip));

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 8
  // Set base address to 0 for 8-bit as this will be later translated to the destination bitmap
  // address - so do all calculations so everything is offset from 0
  uint32_t *base_addr = 0;
#else
  uint32_t *base_addr = ((uint32_t *)dest_bitmap->addr);
#endif

  const uint32_t *const dest_block_x_begin =
      base_addr + (left_clip ? PBL_MAX(0, (((x + left_clip + 31) / 32) - 1)) : (x / 32));

  if (clipped_glyph_target.size.h == 0 || clipped_glyph_target.size.w == 0) {
    return;
  }

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 8
  // NOTE: Since all calculations are based on 1-bit calculation - use the row size from
  // the 1-bit frame buffer
  const int row_size_bytes =
      4 * ((dest_bitmap->bounds.size.w / 32) + ((dest_bitmap->bounds.size.w % 32) ? 1 : 0));
#else
  const int row_size_bytes = dest_bitmap->row_size_bytes;
#endif // CONFIG_SCREEN_COLOR_DEPTH_BITS == 8

  // Number of blocks (i.e. 32-bit chunks)
  const int dest_row_length = row_size_bytes / 4;

  // The number of bits between the beginning of dest_block and glyph_block.
  // If x is negative we need to be fancy to get the rounded down remainder. This
  // is the number of bits to the right of the next 32-bit boundary to the left.
  // For example, if x is -5 we want this shift to be 27, since -32 (the nearest
  // boundary) + 27 = -5
  const uint8_t dest_shift_at_line_begin = (x >= 0) ? x % 32 : (x - ((x / 32) * 32));

  uint8_t dest_shift = dest_shift_at_line_begin;

  // The glyph bitmap starts the block after the metrics data:
  const GlyphBlock *glyph_block = glyph->data;

  // Set up the first piece of source glyph bitmap:
  int8_t glyph_block_bits_left = 32;
  uint32_t src = *glyph_block;

  // Use bit-rotate to align to shift the bitmap to align with the destination.
  // The advantage of rotate vs. bitwise shift is that we can use
  // the bits that wrapped around for the next dest_block
  src = pbl_rotl32(src, dest_shift);
  int8_t src_rotated = dest_shift;
  // how many 32-bit blocks do we need to bitblt on each row. If we're not word aligned we'll need
  // to modify an extra partial word, as we'll have an incomplete word on either side of the line
  // segment we're modifying. For 1-bit, each pixel goes into one bit in dest bitmap - so 32 pixels
  // per block
  const uint8_t num_dest_blocks_per_row =
      (clipped_glyph_target.size.w / 32) + (((dest_shift + left_clip) % 32) ? 1 : 0);

  // Handle clipping at the top of the character. We need to skip a number of bits in our source
  // data.
  const unsigned int bits_to_skip =
      glyph_metrics.size.w * (clipped_glyph_target.origin.y - glyph_target.origin.y);
  if (bits_to_skip) {
    glyph_block += bits_to_skip / 32;
    src = *glyph_block;

    // Simulate the rotate that happens at the bottom of the bitblt loop so our source value is set
    // up just as if we actually rendered those first few lines.
    src = pbl_rotl32(src, (dest_shift_at_line_begin +
                           ((0 - ((uint8_t)glyph_metrics.size.w)) % 32) *
                               (clipped_glyph_target.origin.y - glyph_target.origin.y)) %
                              32);
    src_rotated =
        (dest_shift_at_line_begin + ((0 - ((uint8_t)glyph_metrics.size.w)) % 32) *
                                        (clipped_glyph_target.origin.y - glyph_target.origin.y)) %
        32;
    glyph_block_bits_left -= bits_to_skip % 32;
  }

  for (int dest_y = clipped_glyph_target.origin.y;
       dest_y != clipped_glyph_target.origin.y + clipped_glyph_target.size.h; ++dest_y) {
    dest_shift = dest_shift_at_line_begin;

    // Number of bits to render on this line.
    uint8_t glyph_line_bits_left = clipped_glyph_target.size.w;

    uint32_t *dest_block = (uint32_t *)dest_block_x_begin + (dest_y * dest_row_length);
    const uint32_t *dest_block_end = dest_block + num_dest_blocks_per_row + 1;

    if (left_clip) {
      const int left_clip_shift = left_clip % 32;
      const int clipped_blocks = left_clip / 32;

      dest_shift = (dest_shift + left_clip_shift) % 32;
      glyph_block_bits_left -= left_clip_shift;

      glyph_block += clipped_blocks;

      if (glyph_block_bits_left <= 0) {
        src = *(++glyph_block);
        glyph_block_bits_left += 32;
        // Need to account for the dest_shift when loading up the new glyph block
        src = pbl_rotl32(src, glyph_block_bits_left + dest_shift);
        src_rotated = glyph_block_bits_left + dest_shift;
      }

      dest_block += clipped_blocks;
    }

    while (dest_block != dest_block_end && glyph_line_bits_left) {
      PBL_ASSERT(dest_block < dest_block_end, "DB=<%p> DBE=<%p>", dest_block, dest_block_end);
      PBL_ASSERTN(dest_block >= (uint32_t *)base_addr);
      PBL_ASSERTN(dest_block <
                  (uint32_t *)base_addr +
                      row_size_bytes * (dest_bitmap->bounds.origin.y + dest_bitmap->bounds.size.h));

      // bitblt part of glyph_block:
      const uint8_t number_of_bits =
          PBL_MIN(32 - dest_shift, PBL_MIN(glyph_line_bits_left, glyph_block_bits_left));
      // number_of_bits may be 32: shifting a 32-bit 1 that far is undefined.
      const uint32_t mask = (uint32_t)((((uint64_t)1 << number_of_bits) - 1) << dest_shift);

#if CONFIG_SCREEN_COLOR_DEPTH_BITS == 8
      // dest_block points to the block if the dest image was a 1-bit buffer
      // translate this to an x coordinate in the 8-bit buffer
      const int32_t block_start_x =
          prv_convert_1bit_addr_to_8bit_x(dest_bitmap, dest_block, dest_y);
      const GBitmapDataRowInfo data_row = gbitmap_get_data_row_info(dest_bitmap, dest_y);
      // Only enter the loop if the current block is within the valid data row range
      if (block_start_x + 31 >= data_row.min_x && block_start_x <= data_row.max_x) {
        uint8_t *dest_addr = data_row.data + block_start_x;

        // For each bit in block, write that bit to the dest_bitmap
        for (unsigned int bitindex = 0; bitindex < 32; bitindex++) {
          const int32_t current_x = block_start_x + bitindex;
          // Stop iteration early if we have reached the end of the data row
          if (current_x > data_row.max_x) {
            break;
          }
          // Skip over pixels outside of the bitmap data's x coordinate range
          if (current_x < data_row.min_x) {
            continue;
          }
          // Find position in dest_bitmap that corresponds to the bit index
          // Write to that position if mask for that bit is 1
          if ((mask & src) & (1u << bitindex)) {
            GColor dest_color;
            if (ctx->draw_state.compositing_mode == GCompOpSet) {
              // Blend (i.e. for transparency) if GCompOpSet
              dest_color = gcolor_alpha_blend(ctx->draw_state.text_color,
                                              (GColor){.argb = dest_addr[bitindex]});
            } else {
              dest_color = ctx->draw_state.text_color;
              dest_color.a = 3;
            }
            dest_addr[bitindex] = dest_color.argb;
          }
        }
      }
#else
      if (gcolor_equal(ctx->draw_state.text_color, GColorBlack)) {
        *(dest_block) &= ~(mask & src);
      } else {
        *(dest_block) |= mask & src;
      }
#endif

      dest_shift = (dest_shift + number_of_bits) % 32;
      glyph_block_bits_left -= number_of_bits;
      glyph_line_bits_left -= number_of_bits;

      if (glyph_block_bits_left <= 0) {
        // We ran out of bits in the current glyph block. Get the next glyph blob:
        src = *(++glyph_block);
        glyph_block_bits_left += 32;
        src = pbl_rotl32(src, dest_shift);
        src_rotated = dest_shift;
        // Continue with this dest_block if there is still space left:
        if (dest_shift) {
          continue;
        }
      }

      ++dest_block;
    }

    dest_shift += right_clip % 32;

    // emulate having drawn the right clip
    if (glyph_block_bits_left <= right_clip) {
      int jump_words = (right_clip - glyph_block_bits_left) / 32 + 1;
      glyph_block += jump_words;
      src = *glyph_block;
      src = pbl_rotl32(src, src_rotated);
      glyph_block_bits_left += 32 * jump_words;
    }
    glyph_block_bits_left -= right_clip;

    // Rotate the bits into the right position for the next row:
    dest_shift = dest_shift_at_line_begin - dest_shift;
    src = pbl_rotl32(src, dest_shift % 32);
    src_rotated = (src_rotated + dest_shift) % 32;
  }

  graphics_context_mark_dirty_rect(ctx, clipped_glyph_target);
}

void text_render_set_special_codepoint_cb(SpecialCodepointHandlerCb handler, void *context) {
  TextRenderState *state = app_state_get_text_render_state();
  state->special_codepoint_handler_cb = handler;
  state->special_codepoint_handler_context = context;
}
