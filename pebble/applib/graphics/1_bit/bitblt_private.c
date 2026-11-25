/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/kernel/assert.h>
#include <pbl/logging/logging.h>
#include <pbl/util/bitops.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/testing.h>

#include <applib/app_logging.h>
#include <applib/graphics/bitblt_private.h>
#include <applib/graphics/framebuffer.h>
#include <applib/graphics/graphics.h>
#include <applib/graphics/graphics_private.h>
#include <applib/graphics/gtypes.h>
#include <applib/graphics/raw_image.h>

#define MAX_SUPPORTED_PALETTE_ENTRIES 4

// stores transparent masks + color patterns for even+odd scanlines
typedef struct {
  // true, if color entry will be visible on 1bit, false otherwise
  bool transparent_mask[MAX_SUPPORTED_PALETTE_ENTRIES];
  // a 32bit value you can OR into the 1bit destination for each color entry
  uint32_t palette_pattern[MAX_SUPPORTED_PALETTE_ENTRIES];
} RowLookUp;

typedef RowLookUp TwoRowLookUp[2];

PBL_T_STATIC void prv_apply_tint_color(GColor *color, GColor tint_color) {
  // tint_color.a is always 0 or 3
  if (tint_color.a != 0) {
    tint_color.a = (*color).a;
    *color = tint_color;
  }
}

PBL_T_STATIC void prv_calc_two_row_look_ups(TwoRowLookUp *look_up, GCompOp compositing_mode,
                                            const GColor8 *palette, uint8_t num_entries,
                                            GColor tint_color) {
  for (unsigned int palette_index = 0; palette_index < num_entries; palette_index++) {
    GColor color = palette[palette_index];
    // gcolor_get_grayscale will convert any color with an alpha less than 2 to clear
    // alpha should be ignored in the case of GCompOpAssign so the alpha is set to 3
    if (compositing_mode == GCompOpAssign) {
      color.a = 3;
    } else if (compositing_mode == GCompOpTint) {
      prv_apply_tint_color(&color, tint_color);
    } else if (compositing_mode == GCompOpTintLuminance) {
      color = gcolor_tint_using_luminance_and_multiply_alpha(color, tint_color);
    }
    color = gcolor_get_grayscale(color);
    for (unsigned int row_number = 0; row_number < PBL_ARRAY_SIZE(*look_up); row_number++) {
      (*look_up)[row_number].palette_pattern[palette_index] =
          graphics_private_get_1bit_grayscale_pattern(color, row_number);
      (*look_up)[row_number].transparent_mask[palette_index] =
          gcolor_is_transparent(color) ? false : true;
    }
  }
}

void bitblt_bitmap_into_bitmap_tiled_palette_to_1bit(GBitmap *dest_bitmap,
                                                     const GBitmap *src_bitmap, GRect dest_rect,
                                                     GPoint src_origin_offset,
                                                     GCompOp compositing_mode, GColor tint_color) {
  if (!src_bitmap->palette) {
    return;
  }
  const int8_t dest_begin_x = (dest_rect.origin.x / 32);
  const uint32_t *const dest_block_x_begin = ((uint32_t *)dest_bitmap->addr) + dest_begin_x;
  const int dest_row_length_words = (dest_bitmap->row_size_bytes / 4);
  // The number of bits between the beginning of dest_block and
  // the beginning of the nearest 32-bit block:
  const uint8_t dest_shift_at_line_begin = (dest_rect.origin.x % 32);

  const int16_t src_begin_x = src_bitmap->bounds.origin.x;
  const int16_t src_begin_y = src_bitmap->bounds.origin.y;
  // The bounds size is relative to the bounds origin, but the offset is within the origin. This
  // means that the end coordinates may need to be adjusted.
  const int16_t src_end_x = grect_get_max_x(&src_bitmap->bounds);
  const int16_t src_end_y = grect_get_max_y(&src_bitmap->bounds);

  // how many 32-bit blocks do we need to bitblt on this row:
  const int16_t dest_end_x = grect_get_max_x(&dest_rect);
  const int16_t dest_y_end = grect_get_max_y(&dest_rect);
  const uint8_t num_dest_blocks_per_row =
      (dest_end_x / 32) + ((dest_end_x % 32) ? 1 : 0) - dest_begin_x;

  const GColor *palette = src_bitmap->palette;
  const uint8_t *src = src_bitmap->addr;
  const uint8_t src_bpp = gbitmap_get_bits_per_pixel(gbitmap_get_format(src_bitmap));
  const uint8_t src_palette_size = 1 << src_bpp;
  PBL_ASSERTN(src_palette_size <= MAX_SUPPORTED_PALETTE_ENTRIES);
  // The bitblt loops:
  int16_t src_y = src_begin_y + src_origin_offset.y;
  int16_t dest_y = dest_rect.origin.y;

  TwoRowLookUp look_ups;
  prv_calc_two_row_look_ups(&look_ups, compositing_mode, palette, src_palette_size, tint_color);

  while (dest_y < dest_y_end) {
    if (src_y >= src_end_y) {
      src_y = src_begin_y;
    }
    uint8_t dest_shift = dest_shift_at_line_begin;

    RowLookUp look_up = look_ups[dest_y % 2];

    int16_t src_x = src_begin_x + src_origin_offset.x;
    int16_t row_bits_left = dest_rect.size.w;
    uint32_t *dest_block = (uint32_t *)dest_block_x_begin + (dest_y * dest_row_length_words);

    const uint32_t *dest_block_end = dest_block + num_dest_blocks_per_row;

    while (dest_block != dest_block_end) {
      uint8_t dest_x = dest_shift;
      while (dest_x < 32 && row_bits_left > 0) {
        if (src_x >= src_end_x) { // Wrap horizontally
          src_x = src_begin_x;
        }
        uint8_t cindex = raw_image_get_value_for_bitdepth(src, src_x, src_y,
                                                          src_bitmap->row_size_bytes, src_bpp);
        uint32_t mask = 0;
        pbl_bitset32_update(&mask, dest_x, look_up.transparent_mask[cindex]);

        // This can be optimized by performing actions on the current word all at once
        // instead of iterating through each pixel
        switch (compositing_mode) {
          case GCompOpAssign:
          case GCompOpSet:
          case GCompOpTint:
          case GCompOpTintLuminance:
            *dest_block = (*dest_block & ~mask) | (look_up.palette_pattern[cindex] & mask);
            break;
          default:
            PBL_LOG_DBG("Only the assign, set and tint modes are allowed for palettized bitmaps");
            return;
        }
        dest_x++;
        row_bits_left--;
        src_x++;
      }
      dest_shift = 0;
      dest_block++;
    }
    dest_y++;
    src_y++;
  }
}

#ifdef CONFIG_FRAMEBUFFER_SCALE2
// 1-bit source pixels doubled per blit
#define SCALED2_CHUNK 64

// A palettized source into a framebuffer's stored pixels, each pixel as 2x2: as
// bitblt_bitmap_into_bitmap_tiled_palette_to_1bit(), a pixel at a time but writing four
static void prv_palette_into_scaled2(GBitmap *dest, const GBitmap *src_bitmap, GRect dest_rect,
                                     GPoint src_origin_offset, GCompOp compositing_mode,
                                     GColor tint_color) {
  if (!src_bitmap->palette ||
      (compositing_mode != GCompOpAssign && compositing_mode != GCompOpSet &&
       compositing_mode != GCompOpTint && compositing_mode != GCompOpTintLuminance)) {
    return;
  }
  const uint8_t bpp = gbitmap_get_bits_per_pixel(gbitmap_get_format(src_bitmap));
  const uint8_t entries = 1 << bpp;
  TwoRowLookUp look_ups;
  prv_calc_two_row_look_ups(&look_ups, compositing_mode, src_bitmap->palette, entries, tint_color);
  // The dither of each pixel's row, each bit twice, on both of its rows
  uint32_t patterns[2][MAX_SUPPORTED_PALETTE_ENTRIES];
  for (unsigned r = 0; r < 2; r++) {
    for (unsigned i = 0; i < entries; i++) {
      patterns[r][i] = graphics_private_double_bits16((uint16_t)look_ups[r].palette_pattern[i]);
    }
  }
  const GRect *b = &src_bitmap->bounds;
  const int words_per_row = dest->row_size_bytes / 4;
  for (int y = 0; y < dest_rect.size.h; y++) {
    const int dest_y = dest_rect.origin.y + y;
    const int src_y = b->origin.y + (src_origin_offset.y + y) % b->size.h;
    const uint32_t *pattern = patterns[dest_y % 2];
    uint32_t *row0 = (uint32_t *)dest->addr + 2 * dest_y * words_per_row;
    uint32_t *row1 = row0 + words_per_row;
    int src_x = b->origin.x + src_origin_offset.x % b->size.w;
    for (int x = 0; x < dest_rect.size.w; x++, src_x++) {
      if (src_x >= grect_get_max_x(b)) {
        src_x = b->origin.x;
      }
      const uint8_t ci = raw_image_get_value_for_bitdepth(src_bitmap->addr, src_x, src_y,
                                                          src_bitmap->row_size_bytes, bpp);
      if (!look_ups[0].transparent_mask[ci]) {
        continue;
      }
      const int nx = 2 * (dest_rect.origin.x + x);
      const uint32_t m = 3U << (nx % 32);
      row0[nx / 32] = (row0[nx / 32] & ~m) | (pattern[ci] & m);
      row1[nx / 32] = (row1[nx / 32] & ~m) | (pattern[ci] & m);
    }
  }
}

// Blits into a framebuffer at twice its size: a source at the same scale pixel for pixel, any
// other source with each of its pixels as 2x2, through the blitters below
static void prv_bitblt_into_scaled2(GBitmap *dest_bitmap, const GBitmap *src_bitmap,
                                    GRect dest_rect, GPoint src_origin_offset,
                                    GCompOp compositing_mode, GColor8 tint_color) {
  GBitmap dest = gbitmap_scaled2_get_stored(dest_bitmap);
  const GRect rect2 = GRect(2 * dest_rect.origin.x, 2 * dest_rect.origin.y, 2 * dest_rect.size.w,
                            2 * dest_rect.size.h);
  if (gbitmap_is_scaled2(src_bitmap)) {
    const GBitmap src = gbitmap_scaled2_get_stored(src_bitmap);
    bitblt_bitmap_into_bitmap_tiled(&dest, &src, rect2,
                                    GPoint(2 * src_origin_offset.x, 2 * src_origin_offset.y),
                                    compositing_mode, tint_color);
    return;
  }
  const GBitmapFormat fmt = gbitmap_get_format(src_bitmap);
  if (fmt == GBitmapFormat1BitPalette || fmt == GBitmapFormat2BitPalette) {
    prv_palette_into_scaled2(&dest, src_bitmap, dest_rect, src_origin_offset, compositing_mode,
                             tint_color);
    return;
  }
  if (fmt != GBitmapFormat1Bit) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Only 1 and 2 bit palettized images can be displayed.");
    return;
  }
  const GRect *b = &src_bitmap->bounds;
  if (b->size.w <= 0 || b->size.h <= 0) {
    return;
  }
  // A 1-bit source: each chunk of a row doubled into a row of its own, blitted onto two
  uint32_t buf[SCALED2_CHUNK * 2 / 32];
  GBitmap chunk = *src_bitmap;
  chunk.addr = buf;
  chunk.row_size_bytes = sizeof(buf);
  chunk.info.is_bitmap_heap_allocated = false;
  for (int y = 0; y < dest_rect.size.h; y++) {
    const int src_y = b->origin.y + (src_origin_offset.y + y) % b->size.h;
    const uint8_t *src_row = (const uint8_t *)src_bitmap->addr + src_y * src_bitmap->row_size_bytes;
    for (int cx = 0; cx < dest_rect.size.w; cx += SCALED2_CHUNK) {
      const int n = PBL_MIN(SCALED2_CHUNK, dest_rect.size.w - cx);
      int src_x = b->origin.x + (src_origin_offset.x + cx) % b->size.w;
      for (int i = 0; i < n; i += 8) {
        // Up to 8 source pixels, LSB first, wrapping at the source's right edge
        unsigned bits = 0;
        for (int k = 0; k < 8 && i + k < n; k++, src_x++) {
          if (src_x >= grect_get_max_x(b)) {
            src_x = b->origin.x;
          }
          bits |= ((src_row[src_x / 8] >> (src_x % 8)) & 1U) << k;
        }
        ((uint16_t *)buf)[i / 8] = (uint16_t)graphics_private_double_bits16((uint16_t)bits);
      }
      chunk.bounds = GRect(0, 0, 2 * n, 1);
      for (int row = 0; row < 2; row++) {
        bitblt_bitmap_into_bitmap_tiled_1bit_to_1bit(
            &dest, &chunk, GRect(rect2.origin.x + 2 * cx, rect2.origin.y + 2 * y + row, 2 * n, 1),
            GPointZero, compositing_mode, tint_color);
      }
    }
  }
}
#endif

void bitblt_bitmap_into_bitmap_tiled(GBitmap *dest_bitmap, const GBitmap *src_bitmap,
                                     GRect dest_rect, GPoint src_origin_offset,
                                     GCompOp compositing_mode, GColor8 tint_color) {
  if (bitblt_compositing_mode_is_noop(compositing_mode, tint_color)) {
    return;
  }
#ifdef CONFIG_FRAMEBUFFER_SCALE2
  if (gbitmap_is_scaled2(dest_bitmap)) {
    prv_bitblt_into_scaled2(dest_bitmap, src_bitmap, dest_rect, src_origin_offset, compositing_mode,
                            tint_color);
    return;
  }
#endif

  GBitmapFormat src_fmt = gbitmap_get_format(src_bitmap);
  GBitmapFormat dest_fmt = gbitmap_get_format(dest_bitmap);
  if (dest_fmt != GBitmapFormat1Bit) {
    return;
  }

  switch (src_fmt) {
    case GBitmapFormat1Bit:
      bitblt_bitmap_into_bitmap_tiled_1bit_to_1bit(dest_bitmap, src_bitmap, dest_rect,
                                                   src_origin_offset, compositing_mode, tint_color);
      break;
    case GBitmapFormat1BitPalette:
    case GBitmapFormat2BitPalette:
      bitblt_bitmap_into_bitmap_tiled_palette_to_1bit(
          dest_bitmap, src_bitmap, dest_rect, src_origin_offset, compositing_mode, tint_color);
      break;
    default:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Only 1 and 2 bit palettized images can be displayed.");
      return;
  }
}
