/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "text_detail.h"

#ifdef CONFIG_TEXT_DETAIL

#include <string.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/syscall.h>
#include <pbl/services/compositor/compositor.h>

#include <applib/graphics/framebuffer.h>
#include <kernel/pebble_tasks.h>
#include <process_management/app_manager.h>
#include <process_state/app_state/app_state.h>

static_assert(PBL_DISPLAY_WIDTH <= 256, "records keep x in a byte");

// The records of a row are a list of chunks, each of one owner (the app's frame or the kernel's)
// and one ink. A header packs the next chunk of the row (index + 1, or 0), the owner, the ink and
// the count; each record is x and the coverage of the pixel's 2x2 glyph pixels: 2.25 bytes each.
#define CHUNK_CELLS   8
#define NUM_CHUNKS    (CONFIG_TEXT_DETAIL_CELLS / CHUNK_CELLS)
#define OWNER_KERNEL  1U
#define HDR_NEXT_MASK 0x07FFU
#define HDR_OWNER     (1U << 11)
#define HDR_WHITE     (1U << 12)
// The count less one: a chunk on a row always holds a record
#define HDR_COUNT(h) (((h) >> 13) + 1U)

static_assert(NUM_CHUNKS < HDR_NEXT_MASK, "chunk indices must fit the header");

typedef struct {
  uint16_t hdr;
  uint8_t x[CHUNK_CELLS];
  //! Coverage 0 to 3 of the 2x2 glyph pixels, two bits each, top left first
  uint8_t levels[CHUNK_CELLS];
} Chunk;

static Chunk s_chunks[NUM_CHUNKS];
static uint16_t s_rows[PBL_DISPLAY_HEIGHT];
static uint16_t s_free;
static bool s_initialized;
static TextDetailMode s_mode = CONFIG_TEXT_DETAIL_DEFAULT_MODE;
static uint32_t s_used;
static uint32_t s_peak;
static uint32_t s_dropped;
// An owner's frame that did not fit is shown without detail, rather than with part of it
static bool s_overflow[2];

static uint16_t prv_next(const Chunk *chunk) {
  return chunk->hdr & HDR_NEXT_MASK;
}

static uint8_t prv_owner(const Chunk *chunk) {
  return (chunk->hdr & HDR_OWNER) ? OWNER_KERNEL : 0;
}

static void prv_set_next(Chunk *chunk, uint16_t next) {
  chunk->hdr = (chunk->hdr & ~HDR_NEXT_MASK) | next;
}

static void prv_init_locked(void) {
  for (uint16_t i = 0; i < NUM_CHUNKS; i++) {
    s_chunks[i].hdr = (i + 1 < NUM_CHUNKS) ? i + 2 : 0;
  }
  s_free = 1;
  s_initialized = true;
}

static void prv_forget(uint8_t owner) {
  s_overflow[owner] = false;
  for (uint16_t y = 0; y < PBL_DISPLAY_HEIGHT; y++) {
    pbl_irq_lock();
    Chunk *prev = nullptr;
    uint16_t index = s_rows[y];
    while (index) {
      Chunk *chunk = &s_chunks[index - 1];
      const uint16_t next = prv_next(chunk);
      if (prv_owner(chunk) == owner) {
        s_used -= HDR_COUNT(chunk->hdr);
        if (prev) {
          prv_set_next(prev, next);
        } else {
          s_rows[y] = next;
        }
        chunk->hdr = s_free;
        s_free = index;
      } else {
        prev = chunk;
      }
      index = next;
    }
    pbl_irq_unlock();
  }
}

int text_detail_glyph_begin(const GBitmap *dest) {
  // Only text drawn straight into a frame the display shows has a place on it
  int owner;
  if (pebble_task_get_current() == PebbleTask_App) {
    if (dest->addr != app_state_get_framebuffer()->buffer) {
      return -1;
    }
    owner = 0;
  } else {
    if (dest->addr != compositor_get_framebuffer()->buffer) {
      return -1;
    }
    owner = OWNER_KERNEL;
  }
  pbl_irq_lock();
  if (!s_initialized) {
    prv_init_locked();
  }
  return owner;
}

void text_detail_glyph_end(int owner) {
  if (owner >= 0) {
    pbl_irq_unlock();
  }
}

void text_detail_add(int owner, int16_t x, int16_t y, uint8_t levels, bool white_ink) {
  // Recorded whatever the mode, so a change of mode takes only a new flush
  if (x < 0 || y < 0 || x >= PBL_DISPLAY_WIDTH || y >= PBL_DISPLAY_HEIGHT) {
    return;
  }
  const uint16_t flags = (owner ? HDR_OWNER : 0U) | (white_ink ? HDR_WHITE : 0U);
  Chunk *chunk = s_rows[y] ? &s_chunks[s_rows[y] - 1] : nullptr;
  if (!chunk || (chunk->hdr & (HDR_OWNER | HDR_WHITE)) != flags ||
      HDR_COUNT(chunk->hdr) == CHUNK_CELLS) {
    if (!s_free) {
      s_dropped++;
      s_overflow[owner] = true;
      return;
    }
    const uint16_t index = s_free;
    chunk = &s_chunks[index - 1];
    s_free = prv_next(chunk);
    chunk->hdr = s_rows[y] | flags;
    s_rows[y] = index;
    chunk->x[0] = (uint8_t)x;
    chunk->levels[0] = levels;
  } else {
    const uint8_t i = HDR_COUNT(chunk->hdr);
    chunk->x[i] = (uint8_t)x;
    chunk->levels[i] = levels;
    chunk->hdr += 1U << 13;
  }
  if (++s_used > s_peak) {
    s_peak = s_used;
  }
}

void text_detail_kernel_frame_begin(bool app_hidden) {
  prv_forget(OWNER_KERNEL);
  if (app_hidden) {
    // Its records come back when it draws again on coming back into view
    prv_forget(0);
  }
}

PBL_SYSCALL(void, sys_text_detail_app_frame_begin) {
  // The same callers as get the system UI's fonts: built-in apps drawn to the whole display
  const PebbleProcessMd *md = app_manager_get_current_app_md();
  GSize size;
  app_manager_get_framebuffer_size(&size);
  if (md && !md->is_unprivileged && size.w == PBL_DISPLAY_WIDTH && size.h == PBL_DISPLAY_HEIGHT) {
    prv_forget(0);
  }
}

// Total coverage of a pixel's four glyph pixels, 0 to 12
static unsigned prv_sum(uint8_t levels) {
  return (levels & 3U) + ((levels >> 2) & 3U) + ((levels >> 4) & 3U) + (levels >> 6);
}

// Each glyph pixel black or white, as a black and white panel shows it: inked when at least half
static uint8_t prv_black_white(uint8_t levels, bool white) {
  uint8_t out = 0;
  for (unsigned i = 0; i < 4; i++) {
    const bool ink = ((levels >> (2U * i)) & 3U) >= 2U;
    out |= (uint8_t)(((ink != white) ? 3U : 0U) << (2U * i));
  }
  return out;
}

static uint8_t prv_panel_levels(uint8_t levels, bool white, TextDetailMode mode, bool grey) {
  switch (mode) {
    case TextDetailModeGrey:
      // One level for the whole pixel; grey only lightens black ink on e-ink
      if (!grey || white) {
        return 0;
      }
      return (uint8_t)(((prv_sum(levels) + 2U) / 4U) * 0x55U);
    case TextDetailModeSubpixelGrey:
      if (grey && !white) {
        return levels;
      }
      return prv_black_white(levels, white);
    case TextDetailModeSubpixel:
      return prv_black_white(levels, white);
    default:
      return 0;
  }
}

bool text_detail_row(const TextDetailFrame *frame, uint16_t y, const uint8_t *fb_row,
                     uint8_t *cells) {
  const TextDetailMode mode = s_mode;
  if (y >= PBL_DISPLAY_HEIGHT || !s_rows[y] || mode == TextDetailModeMono ||
      (mode == TextDetailModeGrey && !frame->grey)) {
    return false;
  }
  memset(cells, 0, PBL_DISPLAY_WIDTH);
  const bool in_modal_rows =
      (y >= frame->modal.origin.y) && (y < frame->modal.origin.y + frame->modal.size.h);

  bool any = false;
  pbl_irq_lock();
  for (uint16_t index = s_rows[y]; index; index = prv_next(&s_chunks[index - 1])) {
    const Chunk *chunk = &s_chunks[index - 1];
    const uint8_t owner = prv_owner(chunk);
    if ((owner != OWNER_KERNEL && !frame->app) || s_overflow[owner]) {
      continue;
    }
    const bool white_ink = (chunk->hdr & HDR_WHITE) != 0U;
    for (uint8_t i = 0; i < HDR_COUNT(chunk->hdr); i++) {
      const uint8_t x = chunk->x[i];
      if (owner != OWNER_KERNEL && in_modal_rows && x >= frame->modal.origin.x &&
          x < frame->modal.origin.x + frame->modal.size.w) {
        continue;
      }
      // Skip pixels drawn over since: the frame must hold what the text left there
      const uint8_t levels = chunk->levels[i];
      const bool inked = prv_sum(levels) >= 6U;
      const bool white = (fb_row[x / 8] >> (x % 8)) & 1U;
      if (white != (inked == white_ink)) {
        continue;
      }
      const uint8_t panel = prv_panel_levels(levels, white_ink, mode, frame->grey);
      if (panel) {
        cells[x] = panel;
        any = true;
      }
    }
  }
  pbl_irq_unlock();
  return any;
}

bool text_detail_wants_grey(void) {
  if (s_mode != TextDetailModeGrey && s_mode != TextDetailModeSubpixelGrey) {
    return false;
  }
  for (uint16_t y = 0; y < PBL_DISPLAY_HEIGHT; y++) {
    if (s_rows[y]) {
      return true;
    }
  }
  return false;
}

TextDetailMode text_detail_get_mode(void) {
  return s_mode;
}

void text_detail_set_mode(TextDetailMode mode) {
  if (mode < TextDetailModeCount) {
    s_mode = mode;
  }
}

void text_detail_get_stats(uint32_t *used, uint32_t *peak, uint32_t *dropped) {
  *used = s_used;
  *peak = s_peak;
  *dropped = s_dropped;
}

#endif // CONFIG_TEXT_DETAIL
