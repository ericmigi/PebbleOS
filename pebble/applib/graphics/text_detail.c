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
// the count; each record is x and a 4-bit 2x2 pattern: 1.75 bytes per record.
#define CHUNK_CELLS   8
#define NUM_CHUNKS    (CONFIG_TEXT_DETAIL_CELLS / CHUNK_CELLS)
#define OWNER_KERNEL  1U
#define HDR_NEXT_MASK 0x03FFU
#define HDR_OWNER     (1U << 10)
#define HDR_WHITE     (1U << 11)
#define HDR_COUNT(h)  ((h) >> 12)

static_assert(NUM_CHUNKS < HDR_NEXT_MASK, "chunk indices must fit the header");

typedef struct {
  uint16_t hdr;
  uint8_t x[CHUNK_CELLS];
  //! 2x2 ink patterns, two per byte
  uint8_t patterns[CHUNK_CELLS / 2];
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

void text_detail_add(int owner, int16_t x, int16_t y, uint8_t pattern, bool white_ink) {
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
  }
  const uint8_t i = HDR_COUNT(chunk->hdr);
  chunk->x[i] = (uint8_t)x;
  if (i % 2) {
    chunk->patterns[i / 2] |= (uint8_t)(pattern << 4);
  } else {
    chunk->patterns[i / 2] = pattern;
  }
  chunk->hdr += 1U << 12;
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

// Pixels set in a 2x2 pattern
static const uint8_t s_count[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

// Panel levels of a 2x2 pattern in black ink: 3 where set, 0 elsewhere
static const uint8_t s_sub_levels[16] = {
  0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF,
};

static uint8_t prv_levels(uint8_t pattern, bool white, TextDetailMode mode) {
  if (mode == TextDetailModeGrey) {
    // Grey only lightens black ink on e-ink, so white text keeps its pixels
    if (white) {
      return 0;
    }
    const uint8_t count = s_count[pattern];
    return ((count >= 3) ? 3 : count) * 0x55U;
  }
  // White ink: set panel pixels white (0), the others black (3)
  return white ? (uint8_t)~s_sub_levels[pattern] : s_sub_levels[pattern];
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
      const uint8_t pattern = (chunk->patterns[i / 2] >> (4 * (i % 2))) & 0x0FU;
      const bool inked = s_count[pattern] >= 2;
      const bool white = (fb_row[x / 8] >> (x % 8)) & 1U;
      if (white != (inked == white_ink)) {
        continue;
      }
      const uint8_t levels = prv_levels(pattern, white_ink, mode);
      if (levels) {
        cells[x] = levels;
        any = true;
      }
    }
  }
  pbl_irq_unlock();
  return any;
}

bool text_detail_wants_grey(void) {
  if (s_mode != TextDetailModeGrey) {
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
