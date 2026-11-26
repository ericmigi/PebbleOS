/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "uc8279_x4_cursor.h"

#include <string.h>

#include <pbl/util/math.h>

#define WIDTH  480U
#define HEIGHT 800U
#define PITCH  (WIDTH / 8U)

// Repeated rows compress well in doubled menus. Encode the XOR from the preceding row
// as alternating literal and zero runs, up to 128 bytes per run.
struct writer {
  uint8_t *data;
  unsigned capacity, size, count, header;
  bool zero;
  uint8_t row[PITCH];
};

struct reader {
  const uint8_t *data;
  unsigned count;
  bool zero;
  uint8_t row[PITCH];
};

static uint8_t s_base[12288], s_masks[2][12288];
static bool s_base_valid, s_mask_valid;
static unsigned s_current;

static void prv_put(struct writer *w, unsigned col, uint8_t v) {
  uint8_t delta = w->row[col] ^ v;
  w->row[col] = v;
  if (!w->count || w->count == 128U || w->zero != (delta == 0)) {
    w->header = w->size++;
    w->count = 0;
    w->zero = delta == 0;
  }
  w->count++;
  if (w->data && w->header < w->capacity) {
    w->data[w->header] = (w->count - 1U) | (w->zero ? 0x80U : 0);
  }
  if (delta) {
    if (w->data && w->size < w->capacity) {
      w->data[w->size] = delta;
    }
    w->size++;
  }
}

static uint8_t prv_get(struct reader *r, unsigned col) {
  if (!r->count) {
    uint8_t cmd = *r->data++;
    r->count = (cmd & 127U) + 1U;
    r->zero = (cmd & 128U) != 0;
  }
  r->count--;
  if (!r->zero) {
    r->row[col] ^= *r->data++;
  }
  return r->row[col];
}

void uc8279_cursor_reset(void) {
  s_mask_valid = false;
}

void uc8279_cursor_remember(const struct pbl_display_buffer *buf, bool keep) {
  s_base_valid = false;
  if (!keep || buf->pitch != PITCH) {
    uc8279_cursor_reset();
    return;
  }
  struct writer out = {.data = s_base, .capacity = sizeof(s_base)};
  for (unsigned y = 0; y < HEIGHT && out.size <= out.capacity; y++) {
    for (unsigned col = 0; col < PITCH; col++) {
      prv_put(&out, col, buf->data[y * PITCH + col]);
    }
  }
  s_base_valid = out.size <= out.capacity;
}

bool uc8279_cursor_preview(const struct pbl_display_buffer *buf) {
  if (!s_base_valid || buf->pitch != PITCH) {
    return false;
  }
  struct reader prev = {.data = s_base}, mask = {.data = s_masks[s_current]};
  struct writer next = {.data = s_masks[1U - s_current], .capacity = sizeof(s_masks[0])};
  struct writer cost = {0};
  for (unsigned y = 0; y < HEIGHT; y++) {
    for (unsigned col = 0; col < PITCH; col++) {
      uint8_t target = buf->data[y * PITCH + col];
      uint8_t changed = prv_get(&prev, col) ^ target;
      if (s_mask_valid) {
        changed |= prv_get(&mask, col);
      }
      prv_put(&next, col, changed);
      prv_put(&cost, col, target);
    }
    if (next.size > next.capacity || cost.size > sizeof(s_base)) {
      return false;
    }
  }
  s_current = 1U - s_current;
  s_mask_valid = true;
  return true;
}

int uc8279_cursor_seed(const struct pbl_display_ssd1677 *epd,
                       const struct pbl_display_buffer *buf) {
  struct reader prev = {.data = s_base}, mask = {.data = s_masks[s_current]};
  const bool flip = epd->state->flip;
  // One byte across every visible gate, in landscape controller order.
  static uint8_t strip[WIDTH];
  int ret = 0;
  for (unsigned y = 0; y < HEIGHT && !ret; y += 8U) {
    bool any = false;
    memset(strip, 0, sizeof(strip));
    for (unsigned bit = 0; bit < 8; bit++) {
      for (unsigned col = 0; col < PITCH; col++) {
        uint8_t touched = prv_get(&mask, col);
        uint8_t old = prv_get(&prev, col);
        uint8_t target = buf->data[(y + bit) * PITCH + col];
        uint8_t seed = (old & ~touched) | (~target & touched);
        any |= touched != 0;
        for (unsigned x = 0; x < 8; ++x) {
          unsigned gate = col * 8 + x;
          strip[flip ? WIDTH - gate - 1U : gate] |= ((seed >> x) & 1U) << (flip ? 7U - bit : bit);
        }
      }
    }
    if (any) {
      unsigned x0 = flip ? y : HEIGHT - y - 8U, x1 = x0 + 7U;
      const uint8_t coords[] = {x0 >> 8, x0, x1 >> 8, x1, 0, 120, 2, 0x57, 1};
      ret = epaper_write_command(epd, 0x91, nullptr, 0);
      if (!ret) {
        ret = epaper_write_command(epd, 0x90, coords, sizeof(coords));
      }
      if (!ret) {
        ret = epaper_write_command(epd, 0x10, strip, sizeof(strip));
      }
      if (!ret) {
        ret = epaper_write_command(epd, 0x92, nullptr, 0);
      }
    }
  }
  if (ret) {
    (void)epaper_write_command(epd, 0x92, nullptr, 0);
  }
  uc8279_cursor_reset();
  return ret;
}
