/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "uc8279_x4.h"
#include "uc8279_x4_image_lut.h"

#include <errno.h>
#include <string.h>

#include <pbl/crc/crc.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/malloc.h>
#include <pbl/util/math.h>

#define PANEL_WIDTH  480U
#define PANEL_HEIGHT 800U
#define GATE_BYTES   (PANEL_HEIGHT / 8U)
#define TILE_GATES   64U

// The glass takes complete planes, in landscape scan order. Keep a strip of gates in RAM.
static uint8_t s_tile[TILE_GATES][GATE_BYTES];

// Keep only nonzero spans of each glyph row. The compositor's font cache is accessed on
// KernelMain once. The panel thread can then convert the snapshot in controller scan order.
#define SNAPSHOT_LIMIT (12U * 1024U)

static int prv_snapshot(const struct pbl_display_detail *detail, uint8_t **out, uint32_t *hash) {
  uint8_t *data = nullptr;
  unsigned used = 0, capacity = 0;
  uint8_t row[PANEL_WIDTH / 4U];
  for (unsigned y = 0; y < PANEL_HEIGHT; y++) {
    if (!detail->row(detail, y, row)) {
      continue;
    }
    for (unsigned x = 0; x < sizeof(row);) {
      if (!row[x]) {
        x++;
        continue;
      }
      unsigned first = x++;
      while (x < sizeof(row) && row[x]) {
        x++;
      }
      unsigned len = x - first;
      unsigned needed = used + 4U + len + 2U; // End marker after the last span.
      if (needed > capacity) {
        unsigned size = PBL_MIN(capacity ? 2U * capacity : 1024U, SNAPSHOT_LIMIT);
        uint8_t *next = needed <= size ? pbl_realloc(data, size) : nullptr;
        if (!next) {
          pbl_free(data);
          return -ENOMEM;
        }
        data = next;
        capacity = size;
      }
      data[used++] = (uint8_t)y;
      data[used++] = (uint8_t)(y >> 8);
      data[used++] = (uint8_t)first;
      data[used++] = (uint8_t)len;
      memcpy(data + used, row + first, len);
      used += len;
    }
  }
  if (data) {
    data[used] = 0xff;
    data[used + 1U] = 0xff;
    *hash = pbl_crc32(0, data, used);
  }
  *out = data;
  return 0;
}

static bool prv_snapshot_row(const uint8_t **cursor, unsigned y, uint8_t *levels) {
  const uint8_t *span = *cursor;
  bool any = false;
  while (((unsigned)span[0] | ((unsigned)span[1] << 8)) == y) {
    if (!any) {
      memset(levels, 0, PANEL_WIDTH / 4U);
      any = true;
    }
    memcpy(levels + span[2], span + 4, span[3]);
    span += 4U + span[3];
  }
  *cursor = span;
  return any;
}

static int prv_plane(const struct pbl_display_ssd1677 *epd, const struct pbl_display_buffer *buf,
                     const uint8_t *snapshot, bool msb) {
  const bool flip = epd->state->flip;
  if (!msb) {
    memset(epd->state->grey_tiles, 0, sizeof(epd->state->grey_tiles));
  }
  uint8_t levels[PANEL_WIDTH / 4U];
  int ret = epaper_write_command(epd, msb ? 0x13 : 0x10, nullptr, 0);
  // The controller addresses 600 gates. The first 120 precede the glass and stay white.
  memset(s_tile, 0xff, sizeof(s_tile));
  for (unsigned gate = 0; ret == 0 && gate < 120; gate += TILE_GATES) {
    ret = pbl_spi_write(&epd->spi, s_tile, PBL_MIN(TILE_GATES, 120U - gate) * GATE_BYTES);
  }

  for (unsigned gate = 0; ret == 0 && gate < PANEL_WIDTH; gate += TILE_GATES) {
    const unsigned count = PBL_MIN(TILE_GATES, PANEL_WIDTH - gate);
    memset(s_tile, 0, count * GATE_BYTES);
    const uint8_t *cursor = snapshot;
    for (unsigned y = 0; y < PANEL_HEIGHT; y++) {
      const unsigned column = flip ? y : PANEL_HEIGHT - 1U - y;
      const uint8_t mask = 0x80U >> (column % 8U);
      bool edge = snapshot ? prv_snapshot_row(&cursor, y, levels)
                           : buf->detail->row(buf->detail, y, levels);
      const uint8_t *row = buf->data + y * buf->pitch;
      // The tile starts white. Visit only inked bits in each aligned framebuffer byte.
      for (unsigned i = 0; i < count; i += 8U) {
        const unsigned x = flip ? PANEL_WIDTH - gate - i - 8U : gate + i;
        unsigned ink = (uint8_t)~row[x / 8U];
        while (ink) {
          const unsigned bit = (unsigned)__builtin_ctz(ink);
          ink &= ink - 1U;
          const unsigned pixel = x + bit;
          const unsigned level = edge ? (levels[pixel / 4U] >> (2U * (pixel % 4U))) & 3U : 0;
          if (!msb && (level == 1U || level == 2U)) {
            epd->state->grey_tiles[y / 32U] |= 1U << (pixel / 32U);
          }
          // DTM1 encodes light and DTM2 encodes dark. Both encode solid black.
          if (level == 0U || level == 3U || level == (msb ? 2U : 1U)) {
            s_tile[i + (flip ? 7U - bit : bit)][column / 8U] |= mask;
          }
        }
      }
    }
    ret = pbl_spi_write(&epd->spi, s_tile, count * GATE_BYTES);
  }
  return ret;
}

static int prv_activate(const struct pbl_display_ssd1677 *epd) {
  static const uint8_t psr[] = {0x37, 0x4d};
  int ret = 0;
  if (ret == 0 && !epd->state->powered) {
    ret = epaper_write_command(epd, 0x04, nullptr, 0);
    if (ret == 0) {
      pbl_sleep_ms(2);
      ret = PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 1, 1, 5000U);
    }
    epd->state->powered = ret == 0;
  }
  // Power-on reloads panel settings. Select the external waveform again.
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x00, psr, sizeof(psr));
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x12, nullptr, 0);
  }
  if (ret == 0) {
    ret = PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 0, 1, 100U);
  }
  if (ret == 0) {
    ret = PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 1, 1, 5000U);
  }
  return ret;
}

static int prv_text_window(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *s = epd->state;
  unsigned x0 = PANEL_WIDTH, y0 = PANEL_HEIGHT, x1 = 0, y1 = 0;
  for (unsigned row = 0; row < PANEL_HEIGHT / 32U; row++) {
    unsigned bits = s->grey_tiles[row];
    if (!bits) {
      continue;
    }
    x0 = PBL_MIN(x0, 32U * (unsigned)__builtin_ctz(bits));
    x1 = PBL_MAX(x1, 32U * (32U - (unsigned)__builtin_clz(bits)));
    y0 = PBL_MIN(y0, 32U * row);
    y1 = 32U * (row + 1U);
  }
  if (x0 == PANEL_WIDTH) {
    s->last_window_width = s->last_window_height = 0;
    return -ENOENT;
  }
  unsigned source0 = s->flip ? y0 : PANEL_HEIGHT - y1;
  unsigned source1 = (s->flip ? y1 : PANEL_HEIGHT - y0) - 1U;
  unsigned gate0 = 120U + (s->flip ? PANEL_WIDTH - x1 : x0);
  unsigned gate1 = 119U + (s->flip ? PANEL_WIDTH - x0 : x1);
  s->last_window_width = source1 - source0 + 1U;
  s->last_window_height = gate1 - gate0 + 1U;
  const uint8_t coords[] = {source0 >> 8, source0,    source1 >> 8, source1, gate0 >> 8,
                            gate0,        gate1 >> 8, gate1,        1};
  int ret = epaper_write_command(epd, 0x91, nullptr, 0);
  return ret ? ret : epaper_write_command(epd, 0x90, coords, sizeof(coords));
}

static int prv_waveform(const struct pbl_display_ssd1677 *epd, bool local_text) {
  static const uint8_t psr[] = {0x37, 0x4d};
  static const uint8_t lut_regs[] = {0x20, 0x21, 0x23, 0x22, 0x24};
  const uint8_t power[] = {
    0x17, s_image_packet[1], s_image_packet[2], s_image_packet[3], s_image_packet[4]
  };
  const uint8_t cdi = local_text ? 0xd7 : 0x97;
  int ret = epaper_write_command(epd, 0x00, psr, sizeof(psr));
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x30, s_image_packet, 1);
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x01, power, sizeof(power));
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x2a, s_image_packet + 6, 3);
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x82, s_image_packet + 5, 1);
  }
  for (unsigned i = 0; ret == 0 && i < sizeof(lut_regs); i++) {
    // Absolute grey selectors keep their factory trajectories. Equal plane bits represent
    // already-rendered solid ink or paper, which both use VCOM's neutral phase sequence.
    unsigned bank = local_text && (i == 1 || i == 4) ? 0 : i;
    const uint8_t *row = s_image_packet + 9 + bank * 49;
    uint8_t text[49] = {0};
    if (local_text) {
      // All retained selectors match VCOM after the second group. Keep both active
      // groups exactly and use the factory text bank's zero padding for the remainder.
      memcpy(text, row, 14);
      row = text;
    }
    ret = epaper_write_command(epd, lut_regs[i], row, 49);
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x50, &cdi, 1);
  }
  if (ret || !local_text) {
    return ret ? ret : prv_activate(epd);
  }
  ret = prv_text_window(epd);
  if (ret == -ENOENT) {
    return 0;
  }
  if (!ret) {
    ret = prv_activate(epd);
  }
  int leave = epaper_write_command(epd, 0x92, nullptr, 0);
  return ret ? ret : leave;
}

int uc8279_x4_prepare_grey(const struct pbl_display_ssd1677 *epd,
                           const struct pbl_display_buffer *buf, enum uc8279_grey_action *action) {
  *action = UC8279_GREY_SKIP;
  if (epd->state->panel_id[0][3] != 0x68) {
    return 0;
  }
  if (epd->disp.props.width != PANEL_WIDTH || epd->disp.props.height != PANEL_HEIGHT ||
      buf->pitch != PANEL_WIDTH / 8U || !epd->quarter || epd->scale != 1U) {
    return -ENOTSUP;
  }
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t start = (uint32_t)pbl_uptime_ms();
  uint32_t hash = 0;
  int ret = prv_snapshot(buf->detail, &state->next_grey_levels, &hash);
  if (ret == 0 && !state->next_grey_levels) {
    if (state->last_was_grey) {
      *action = UC8279_GREY_CLEAR;
    }
    return 0;
  }
  if (ret == 0 && !state->need_half && state->last_was_grey && state->grey_hash_valid &&
      state->grey_hash == hash) {
    pbl_free(state->next_grey_levels);
    state->next_grey_levels = nullptr;
    return 0;
  }
  state->grey_hash_valid = ret == 0;
  state->grey_hash = hash;
#ifdef CONFIG_DISPLAY_UC8279_LOCAL_TEXT
  // A new selector map can precede any change to the B/W base. Restore the previous
  // edges before applying the new map so removed grey pixels return to their base.
  if ((ret == 0 || ret == -ENOMEM) && state->last_was_grey) {
    int clear = uc8279_x4_leave_grey(epd, buf);
    if (clear != 0) {
      pbl_free(state->next_grey_levels);
      state->next_grey_levels = nullptr;
      return clear;
    }
  }
#endif
  if (ret == -ENOMEM) {
    // A dense frame still gets all four levels. Encode directly before releasing the compositor.
    state->need_half = true;
    ret = prv_plane(epd, buf, nullptr, false);
    if (ret == 0) {
      ret = prv_plane(epd, buf, nullptr, true);
    }
  }
  state->last_write_ms = (uint32_t)pbl_uptime_ms() - start;
  if (ret == 0) {
    *action = UC8279_GREY_DRAW;
  }
  return ret;
}

int uc8279_x4_finish_grey(const struct pbl_display_ssd1677 *epd,
                          const struct pbl_display_buffer *buf) {
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t start = (uint32_t)pbl_uptime_ms();
  state->need_half = true;
  uint8_t *snapshot = state->next_grey_levels;
  int ret = 0;
  if (snapshot) {
    ret = prv_plane(epd, buf, snapshot, false);
    if (ret == 0) {
      ret = prv_plane(epd, buf, snapshot, true);
    }
    pbl_free(snapshot);
    state->next_grey_levels = nullptr;
    state->last_write_ms += (uint32_t)pbl_uptime_ms() - start;
  }
  start = (uint32_t)pbl_uptime_ms();
  bool drove = false;
  if (ret == 0) {
    state->last_window_width = PANEL_HEIGHT;
    state->last_window_height = PANEL_WIDTH;
#ifdef CONFIG_DISPLAY_UC8279_LOCAL_TEXT
    ret = prv_waveform(epd, true);
#else
    ret = prv_waveform(epd, false);
#endif
    drove = ret == 0 && state->last_window_width != 0;
    if (drove) {
      state->last_drive = UC8279_DRIVE_GREY;
      state->unsettled_pulses = 0;
      state->completed_drives++;
    }
  }
  state->last_refresh_ms = (uint32_t)pbl_uptime_ms() - start;
  start = (uint32_t)pbl_uptime_ms();
  if (ret == 0) {
    ret = epaper_write_frame(epd, 0x10, buf->data);
  }
  if (ret == 0) {
    ret = epaper_write_frame(epd, 0x13, buf->data);
  }
  state->last_post_ms = (uint32_t)pbl_uptime_ms() - start;
  state->need_half = ret != 0;
  state->last_was_grey = ret == 0 && drove;
  state->idle_since_ms = (uint32_t)pbl_uptime_ms();
  if (state->powered && epd->power_off_ms != 0U) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle, PBL_MSEC(epd->power_off_ms));
  }
  return ret;
}

// The factory settle bank drives the previous page to the new monochrome destination.
int uc8279_x4_leave_grey(const struct pbl_display_ssd1677 *epd,
                         const struct pbl_display_buffer *buf) {
  static const uint8_t window[] = {0, 0, 3, 0x1f, 0, 120, 2, 0x57, 1};
  static const uint8_t psr[] = {0x37, 0x4d};
  static const uint8_t setup[][2] =
      {{0x03, 0x20}, {0xe1, 0x02}, {0x50, 0xd7}, {0xe0, 0x02}, {0xe5, 0x5a}};
  struct pbl_display_ssd1677_state *s = epd->state;
  uint32_t start = (uint32_t)pbl_uptime_ms();
  // OLD still holds the previous monochrome base, including the paper around each glyph.
  int ret = epaper_write_frame(epd, 0x13, buf->data);
  s->last_write_ms = (uint32_t)pbl_uptime_ms() - start;
  start = (uint32_t)pbl_uptime_ms();
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x91, nullptr, 0);
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x90, window, sizeof(window));
  }
  if (ret == 0) {
    ret = epaper_write_command(epd, 0x00, psr, sizeof(psr));
  }
  for (unsigned i = 0; ret == 0 && i < sizeof(setup) / sizeof(setup[0]); i++) {
    ret = epaper_write_command(epd, setup[i][0], &setup[i][1], 1);
  }
  for (unsigned i = 0; ret == 0 && i < 5U; i++) {
    ret = epaper_write_command(epd, s_settle_lut[i][0], s_settle_lut[i] + 1, 42);
  }
  if (ret == 0) {
    ret = prv_activate(epd);
    if (ret == 0) {
      s->last_drive = UC8279_DRIVE_TRANSITION;
      s->completed_drives++;
    }
  }
  int leave = epaper_write_command(epd, 0x92, nullptr, 0);
  if (ret == 0) {
    ret = leave;
  }
  s->last_refresh_ms = (uint32_t)pbl_uptime_ms() - start;
  start = (uint32_t)pbl_uptime_ms();
  if (ret == 0) {
    ret = epaper_write_frame(epd, 0x10, buf->data);
  }
  s->last_post_ms = (uint32_t)pbl_uptime_ms() - start;
  s->last_was_grey = false;
  s->need_half = ret != 0;
  s->updates++;
  s->idle_since_ms = (uint32_t)pbl_uptime_ms();
  if (s->powered && epd->power_off_ms != 0U) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &s->idle, PBL_MSEC(epd->power_off_ms));
  }
  return ret;
}

#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>
#include <pbl/util/misc.h>

static int prv_stream_image(const struct pbl_display_ssd1677 *epd, uc8279_image_read_fn read,
                            void *ctx) {
  struct pbl_display_ssd1677_state *s = epd->state;
  if (pbl_display_update_in_progress(&epd->disp)) {
    return -EBUSY;
  }
  uint32_t start = (uint32_t)pbl_uptime_ms();
  int ret = 0;
  for (unsigned plane = 0; plane < 2 && ret == 0; plane++) {
    ret = epaper_write_command(epd, plane ? 0x13 : 0x10, nullptr, 0);
    memset(s_tile, 0xff, sizeof(s_tile));
    for (unsigned g = 0; ret == 0 && g < 120; g += TILE_GATES) {
      ret = pbl_spi_write(&epd->spi, s_tile, PBL_MIN(TILE_GATES, 120U - g) * GATE_BYTES);
    }
    for (unsigned g = 0; ret == 0 && g < PANEL_WIDTH; g += TILE_GATES) {
      unsigned n = PBL_MIN(TILE_GATES, PANEL_WIDTH - g);
      ret = read(ctx, plane, g, &s_tile[0][0], n);
      if (ret == 0) {
        ret = pbl_spi_write(&epd->spi, s_tile, n * GATE_BYTES);
      }
    }
  }
  s->last_write_ms = (uint32_t)pbl_uptime_ms() - start;
  start = (uint32_t)pbl_uptime_ms();
  if (ret == 0) {
    ret = prv_waveform(epd, false);
    if (ret == 0) {
      s->last_drive = UC8279_DRIVE_GREY;
      s->unsettled_pulses = 0;
      s->completed_drives++;
    }
  }
  s->last_refresh_ms = (uint32_t)pbl_uptime_ms() - start;
  s->last_post_ms = 0;
  s->last_window_width = PANEL_HEIGHT;
  s->last_window_height = PANEL_WIDTH;
  // Page data replaced the compositor's baseline, including on a partial write failure.
  s->need_half = true;
  s->last_was_grey = false;
  s->idle_since_ms = (uint32_t)pbl_uptime_ms();
  if (s->powered && epd->power_off_ms != 0U) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &s->idle, PBL_MSEC(epd->power_off_ms));
  }
  return ret;
}

int pbl_display_uc8279_image(uc8279_image_read_fn read, void *ctx) {
  const struct pbl_device *dev;
  PBL_DEVICE_CLASS_FOREACH(&pbl_display_class, dev) {
    const struct pbl_display *disp = PBL_CONTAINER_OF(dev, const struct pbl_display, dev);
    if (!pbl_device_is_ready(dev) || disp->ops != &pbl_display_ssd1677_ops) {
      continue;
    }
    const struct pbl_display_ssd1677 *epd =
        PBL_CONTAINER_OF(disp, const struct pbl_display_ssd1677, disp);
    if (!epd->state->uc8279 || epd->state->panel_id[0][3] != 0x68) {
      return -ENOTSUP;
    }
    pbl_mutex_lock(&epd->state->lock, PBL_FOREVER);
    int ret = prv_stream_image(epd, read, ctx);
    pbl_mutex_unlock(&epd->state->lock);
    return ret;
  }
  return -ENODEV;
}

// A 16x16 Bayer tile has 256 ranks. Mix two adjacent native tones for each input value.
static unsigned prv_rank(unsigned x, unsigned y) {
  unsigned rank = 0;
  for (unsigned bit = 0; bit < 4; bit++) {
    rank = (rank << 2) | (((x ^ y) & 1U) << 1) | (y & 1U);
    x >>= 1;
    y >>= 1;
  }
  return rank;
}

static int prv_ramp(void *ctx, unsigned plane, unsigned gate, uint8_t *data, unsigned gates) {
  memset(data, 0, gates * GATE_BYTES);
  for (unsigned i = 0; i < gates; i++) {
    const unsigned x = PANEL_WIDTH - 1U - gate - i;
    for (unsigned y = 0; y < PANEL_HEIGHT; y++) {
      unsigned ink;
      if (y < 160U) {
        ink = x / 120U; // Four native swatches.
      } else {
        // 256 patches, each an integral 16x16 tile; keep a white margin.
        unsigned value =
            x >= 112 && x < 368 && y >= 208 && y < 720 ? ((y - 208) / 32) * 16 + (x - 112) / 16 : 0;
        unsigned target = (value * 768 + 127) / 255;
        ink = target / 256 + (prv_rank(x, y) < target % 256);
      }
      if ((ink >> plane) & 1U) {
        data[i * GATE_BYTES + y / 8] |= 0x80U >> (y % 8U);
      }
    }
  }
  return 0;
}

static int prv_cmd_ramp(const struct pbl_shell *sh, size_t argc, char **argv) {
  int ret = pbl_display_uc8279_image(prv_ramp, nullptr);
  if (ret != 0) {
    pbl_shell_error(sh, "Grayscale image failed (%d)", ret);
  }
  return ret;
}
PBL_SHELL_SUBCMD_ADD(sub_epd, ramp, nullptr, "Show four native tones and 256 spatial mixtures",
                     prv_cmd_ramp, 1, 0);
#endif
