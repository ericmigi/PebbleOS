/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Grey levels of the system UI's text for framebuffers at twice the display's size
//! (CONFIG_FRAMEBUFFER_SCALE2): the text_detail.h interface at panel resolution.
//!
//! The framebuffer holds the text at panel resolution already, so only the grey pass needs
//! anything more. Rather than a record per edge pixel, each glyph drawn is kept: its font,
//! codepoint and place. When the display asks for a row of grey, the glyphs of its band of rows
//! are fetched again and their edge pixels of level 1 or 2 laid into a band buffer, those the
//! frame still shows black.

#include "text_detail.h"

#if defined(CONFIG_TEXT_DETAIL) && defined(CONFIG_FRAMEBUFFER_SCALE2)

#include <string.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/syscall.h>
#include <pbl/services/compositor/compositor.h>
#include <pbl/util/math.h>

#include <applib/graphics/framebuffer.h>
#include <applib/graphics/gcontext.h>
#include <applib/graphics/text_resources.h>
#include <kernel/pebble_tasks.h>
#include <kernel/ui/kernel_ui.h>
#include <process_management/app_manager.h>
#include <process_state/app_state/app_state.h>
#include <resource/resource.h>

#define OWNER_KERNEL 1U
#define ROWS         (PBL_DISPLAY_HEIGHT * 2)
#define ROW_BYTES    (PBL_DISPLAY_WIDTH * 2 / 4)
#define BAND_ROWS    16

typedef struct {
  const FontInfo *font;
  //! Codepoint in bits 0-20, the glyph's height in rows in 21-28, the owner in 31
  uint32_t code;
  //! The glyph bitmap's top left and the clip it was drawn in, in display pixels
  int16_t x;
  int16_t y;
  GRect clip;
} Glyph;

#define CODE_MASK  0x1FFFFFU
#define CODE_H(c)  (((c) >> 21) & 0xFFU)
#define CODE_OWNER (1U << 31)

static Glyph s_glyphs[CONFIG_TEXT_DETAIL_GLYPHS];
static uint16_t s_count;
static TextDetailMode s_mode = CONFIG_TEXT_DETAIL_DEFAULT_MODE;
static uint32_t s_peak;
static uint32_t s_dropped;
static bool s_overflow[2];
static bool s_test;
static bool s_grey_supported = true;

static uint8_t s_band[BAND_ROWS][ROW_BYTES];
static bool s_band_any[BAND_ROWS];
static int s_band_index = -1;

static void prv_forget(unsigned owner) {
  pbl_irq_lock();
  s_overflow[owner] = false;
  uint16_t kept = 0;
  for (uint16_t i = 0; i < s_count; i++) {
    if (((s_glyphs[i].code & CODE_OWNER) ? OWNER_KERNEL : 0U) != owner) {
      s_glyphs[kept++] = s_glyphs[i];
    }
  }
  s_count = kept;
  s_band_index = -1;
  pbl_irq_unlock();
}

static bool prv_grey_enabled(void) {
  return s_grey_supported && (s_mode == TextDetailModeGrey || s_mode == TextDetailModeSubpixelGrey);
}

bool text_detail_add_glyph(const GBitmap *dest, const FontInfo *font, uint32_t codepoint,
                           GPoint origin, uint8_t height, const GRect *clip) {
  // Only the system's fonts outlive the frame: an app's could be gone by the grey pass
  if (!prv_grey_enabled() || font->base.app_num != SYSTEM_APP) {
    return false;
  }
  unsigned owner;
  if (pebble_task_get_current() == PebbleTask_App) {
    const PebbleProcessMd *md = app_manager_get_current_app_md();
    if (!md || md->is_unprivileged || dest->addr != app_state_get_framebuffer()->buffer ||
        dest->bounds.size.w != PBL_DISPLAY_WIDTH || dest->bounds.size.h != PBL_DISPLAY_HEIGHT) {
      return false;
    }
    owner = 0;
  } else {
    if (dest->addr != compositor_get_framebuffer()->buffer) {
      return false;
    }
    owner = OWNER_KERNEL;
  }
  pbl_irq_lock();
  if (s_overflow[owner]) {
    pbl_irq_unlock();
    return false;
  }
  if (s_count == CONFIG_TEXT_DETAIL_GLYPHS) {
    // Further glyphs use monochrome edges. Already accepted glyphs still get their grey pass.
    s_overflow[owner] = true;
    s_dropped++;
    pbl_irq_unlock();
    return false;
  }
  s_glyphs[s_count++] = (Glyph){
    .font = font,
    .code = (codepoint & CODE_MASK) | ((uint32_t)height << 21) | (owner ? CODE_OWNER : 0U),
    .x = origin.x,
    .y = origin.y,
    .clip = *clip,
  };
  s_peak = PBL_MAX(s_peak, s_count);
  s_band_index = -1;
  pbl_irq_unlock();
  return true;
}

void text_detail_app_unload(void) {
  prv_forget(0);
}

void text_detail_kernel_frame_begin(bool app_hidden) {
  s_test = false;
  prv_forget(OWNER_KERNEL);
  if (app_hidden) {
    prv_forget(0);
  }
}

PBL_SYSCALL(void, sys_text_detail_app_frame_begin) {
  // The same callers as get the system UI's fonts: built-in apps drawn to the whole display
  const PebbleProcessMd *md = app_manager_get_current_app_md();
  GSize size;
  app_manager_get_framebuffer_size(&size);
  if (md && !md->is_unprivileged && size.w == PBL_DISPLAY_WIDTH && size.h == PBL_DISPLAY_HEIGHT) {
    s_test = false;
    prv_forget(0);
  }
}

static void prv_set(int row, int x, unsigned level) {
  if (row < 0 || row >= BAND_ROWS || x < 0 || x >= PBL_DISPLAY_WIDTH * 2) {
    return;
  }
  s_band[row][x / 4] |= (uint8_t)(level << (2 * (x % 4)));
  s_band_any[row] = true;
}

// The test pattern of text_detail_test(), for rows of band y0
static void prv_test_band(int y0) {
  for (int r = 0; r < BAND_ROWS; r++) {
    const int y = y0 + r;
    // Two bands of light grey bars, two of dark, from row 96; columns 48-431
    if (y >= 96 && y < 160) {
      const unsigned level = (y < 128) ? 1U : 2U;
      for (int x = 48; x < 432; x++) {
        prv_set(r, x, ((x / 16) % 2) ? level : 0U);
      }
    }
    // Single pixels: light at (100, 200), dark at (102, 200), a light-dark pair at (300, 201)
    if (y == 200) {
      prv_set(r, 100, 1);
      prv_set(r, 102, 2);
    }
    if (y == 201) {
      prv_set(r, 300, 1);
      prv_set(r, 301, 2);
    }
  }
}

static bool prv_black(const FrameBuffer *fb, int x, int y) {
  const uint8_t *row = (const uint8_t *)framebuffer_get_line((FrameBuffer *)fb, (uint16_t)y);
  return ((row[x / 8] >> (x % 8)) & 1U) == 0;
}

static void prv_fill_band(const TextDetailFrame *frame, int band) {
  memset(s_band, 0, sizeof(s_band));
  memset(s_band_any, 0, sizeof(s_band_any));
  const int y0 = band * BAND_ROWS;
  if (s_test) {
    prv_test_band(y0);
    return;
  }
  FrameBuffer *fb = compositor_get_framebuffer();
  FontCache *cache = &kernel_ui_get_graphics_context()->font_cache;
  for (uint16_t i = 0;; i++) {
    // The app may draw its next frame meanwhile: take each record whole
    pbl_irq_lock();
    if (i >= s_count) {
      pbl_irq_unlock();
      break;
    }
    const Glyph g = s_glyphs[i];
    pbl_irq_unlock();
    const bool kernel = (g.code & CODE_OWNER) != 0U;
    if (!kernel && !frame->app) {
      continue;
    }
    // Rows 2y to 2(y + h) at panel resolution
    if (2 * g.y >= y0 + BAND_ROWS || 2 * (g.y + (int)CODE_H(g.code)) <= y0) {
      continue;
    }
    GlyphLocation location;
    const GlyphData *glyph =
        text_resources_get_glyph(cache, g.code & CODE_MASK, (FontInfo *)g.font, &location);
    if (!glyph || !text_resources_glyph_has_edges(&location)) {
      continue;
    }
    const int w = glyph->header.width_px;
    const int h = glyph->header.height_px;
    if (w == 0 || h == 0) {
      continue;
    }
    const uint8_t *edges = (const uint8_t *)glyph->data + ((w * h + 31) / 32) * 4;
    const unsigned count = edges[0] | (edges[1] << 8);
    edges += 2;
    for (unsigned e = 0; e < count; e++) {
      const int cx = g.x + edges[3 * e];
      const int cy = g.y + edges[3 * e + 1];
      const unsigned levels = edges[3 * e + 2];
      if (edges[3 * e] >= w || !grect_contains_point(&g.clip, &GPoint(cx, cy))) {
        continue;
      }
      // An app's text under the kernel's windows is theirs now
      if (!kernel && grect_contains_point(&frame->modal, &GPoint(cx, cy))) {
        continue;
      }
      for (unsigned s = 0; s < 4; s++) {
        const unsigned level = (levels >> (2 * s)) & 3U;
        const int y = 2 * cy + (int)(s >> 1);
        const int x = 2 * cx + (int)(s & 1U);
        // Only pixels the frame still shows black, as the text left them
        if ((level == 1U || level == 2U) && x >= 0 && x < PBL_DISPLAY_WIDTH * 2 && y >= 0 &&
            y < ROWS && y >= y0 && y < y0 + BAND_ROWS && prv_black(fb, x, y)) {
          prv_set(y - y0, x, level);
        }
      }
    }
  }
}

bool text_detail_row(const TextDetailFrame *frame, uint16_t y, const uint8_t *fb_row,
                     uint8_t *cells) {
  if (!frame->grey || y >= ROWS || (!prv_grey_enabled() && !s_test)) {
    return false;
  }
  const int band = y / BAND_ROWS;
  if (band != s_band_index) {
    prv_fill_band(frame, band);
    s_band_index = band;
  }
  if (!s_band_any[y % BAND_ROWS]) {
    return false;
  }
  memcpy(cells, s_band[y % BAND_ROWS], ROW_BYTES);
  return true;
}

bool text_detail_wants_grey(void) {
  // A grey pass follows: its bands are of the frame now shown
  s_band_index = -1;
  return s_grey_supported && (s_test || (prv_grey_enabled() && s_count > 0));
}

void text_detail_test(void) {
  s_test = true;
  s_band_index = -1;
}

TextDetailMode text_detail_get_mode(void) {
  return s_mode;
}

void text_detail_set_grey_supported(bool supported) {
  s_grey_supported = supported;
}

void text_detail_set_mode(TextDetailMode mode) {
  if (mode < TextDetailModeCount) {
    s_test = false;
    s_mode = mode;
  }
}

void text_detail_get_stats(uint32_t *used, uint32_t *peak, uint32_t *dropped) {
  *used = s_count;
  *peak = s_peak;
  *dropped = s_dropped;
}

#endif
