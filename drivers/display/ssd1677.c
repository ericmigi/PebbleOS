/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "geometry.h"

#include <errno.h>
#include <string.h>

#include <pbl/crc/crc.h>
#include <pbl/devicetree.h>
#include <pbl/drivers/display/ssd1677.h>
#ifdef CONFIG_DISPLAY_UC8279_X4
#include "uc8279_x4.h"
#endif
#include <pbl/kernel/assert.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/section.h>
#include <pbl/kernel/time.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>

#define CMD_DRIVER_OUTPUT  0x01U
#define CMD_GATE_VOLTAGE   0x03U
#define CMD_SOURCE_VOLTAGE 0x04U
#define CMD_BOOSTER        0x0CU
#define CMD_DATA_ENTRY     0x11U
#define CMD_SW_RESET       0x12U
#define CMD_TEMP_SENSOR    0x18U
#define CMD_TEMP_WRITE     0x1AU
#define CMD_ACTIVATE       0x20U
#define CMD_UPDATE_CTRL1   0x21U
#define CMD_UPDATE_CTRL2   0x22U
#define CMD_WRITE_RAM_BW   0x24U
#define CMD_WRITE_RAM_RED  0x26U
#define CMD_VCOM           0x2CU
#define CMD_WRITE_LUT      0x32U
#define CMD_BORDER         0x3CU
#define CMD_RAM_X_RANGE    0x44U
#define CMD_RAM_Y_RANGE    0x45U
#define CMD_AUTO_WRITE_BW  0x46U
#define CMD_AUTO_WRITE_RED 0x47U
#define CMD_RAM_X_COUNTER  0x4EU
#define CMD_RAM_Y_COUNTER  0x4FU

// Interlaced gate scan
#define DRIVER_OUTPUT_SCAN 0x02U
// X increments, Y decrements: the panel's gates run bottom to top
#define DATA_ENTRY_XINC_YDEC 0x01U
#define TEMP_SENSOR_INTERNAL 0x80U
// Fill the whole plane with white
#define AUTO_WRITE_WHITE 0xF7U
// Border waveforms, at init and for refreshes; most other projects use 0x01 for both
#define BORDER_INIT      0x80U
#define BORDER_REFRESH   0xC0U
#define CTRL1_NORMAL     0x00U
#define CTRL1_BYPASS_RED 0x40U
#define HALF_TEMP        0x5AU
#define SEQ_HALF         0xD7U
#define SEQ_FAST         0xFCU
#define SEQ_POWER_OFF    0x03U
// The display mode 2 waveform in the LUT register, without loading one; turning the clock and
// analog on first, if they are off, takes 80 ms more
#define SEQ_CUSTOM_LUT   0x0CU
#define SEQ_CLOCK_ANALOG 0xC0U
#define SEQ_ANALOG_OFF   0x03U

#define RESET_MS 10U
// Reset, RAM fill and power off: 84 ms measured for the power on
#define BUSY_TIMEOUT_MS 1000U
// Measured: half 1.7 s, fast 0.2-0.5 s; longer in the cold
#define REFRESH_TIMEOUT_MS 5000U
// Waiting for an in-flight refresh and its post-refresh re-sync to finish
#define WAIT_IDLE_TIMEOUT_MS (REFRESH_TIMEOUT_MS + BUSY_TIMEOUT_MS)

enum refresh_mode {
  REFRESH_HALF,
  REFRESH_FAST,
  REFRESH_TOPUP,
  REFRESH_GREY,
};

static const uint8_t s_booster[] = {0xAEU, 0xC7U, 0xC3U, 0xC0U, 0x80U};

/*
 * The panel's own fast waveform, as the OTP holds it (read back on an X4 by marigold-os, and its
 * length matches the 416 ms 22=1C takes here): one group of phases of 3, 16, 1 and 1 frames at
 * 50 Hz. The pixel classes are (old << 1) | new, 1 white: black to black gets 3 frames to black,
 * a change 20 frames to its colour, white to white 1 frame to white. Phase B, the main drive, is
 * fast-frames long instead: black to white and white to black shorten together and stay
 * balanced. The voltages are that waveform's, which a LUT written through 0x32 must set itself.
 */
#define LUT_LEN 105U
static const uint8_t s_lut_vs[] = {0x40U, 0xA8U, 0x54U, 0x08U};
static const uint8_t s_lut_phases[] = {0x03U, 0x10U, 0x01U, 0x01U};
// Differential: a change is driven to its colour, other pixels not at all
static const uint8_t s_diff_vs[] = {0x00U, 0xA8U, 0x54U, 0x00U};
#define GATE_VOLTAGE 0x17U
#define VCOM         0x10U

static const struct pbl_display_ssd1677 *prv_epd(const struct pbl_display *disp) {
  return PBL_CONTAINER_OF(disp, const struct pbl_display_ssd1677, disp);
}

static struct display_geometry prv_geometry(const struct pbl_display_ssd1677 *epd) {
  return (struct display_geometry){
    .width = epd->disp.props.width,
    .height = epd->disp.props.height,
    .panel_width = epd->panel_width,
    .panel_height = epd->panel_height,
    .scale = epd->scale,
    .quarter = epd->quarter,
  };
}

static int prv_cmd(const struct pbl_display_ssd1677 *epd, uint8_t cmd, const uint8_t *data,
                   size_t len) {
  int ret;

  (void)pbl_gpio_set(&epd->dc, false);
  ret = pbl_spi_write(&epd->spi, &cmd, 1U);
  (void)pbl_gpio_set(&epd->dc, true);
  if (ret != 0 || len == 0U) {
    return ret;
  }

  return pbl_spi_write(&epd->spi, data, len);
}

static int prv_cmd1(const struct pbl_display_ssd1677 *epd, uint8_t cmd, uint8_t data) {
  return prv_cmd(epd, cmd, &data, 1U);
}

static int prv_wait_busy(const struct pbl_display_ssd1677 *epd) {
  return PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 0, 1U, BUSY_TIMEOUT_MS);
}

// RAM window: bytes x0..x1 of controller rows y0..y1, written top to bottom
static int prv_ram_window(const struct pbl_display_ssd1677 *epd,
                          const struct pbl_display_ssd1677_window *w) {
  struct display_geometry g = prv_geometry(epd);
  uint16_t last = display_geometry_scan_height(&g) - 1U;
  // The gates run bottom to top: controller row y is RAM row last - y
  uint16_t ys = last - w->y0;
  uint16_t ye = last - w->y1;
  uint16_t xs = w->x0 * 8U;
  uint16_t xe = w->x1 * 8U + 7U;
  const uint8_t xr[] = {xs & 0xFFU, xs >> 8, xe & 0xFFU, xe >> 8};
  const uint8_t yr[] = {ys & 0xFFU, ys >> 8, ye & 0xFFU, ye >> 8};
  const uint8_t xc[] = {xs & 0xFFU, xs >> 8};
  const uint8_t yc[] = {ys & 0xFFU, ys >> 8};
  int ret;

  ret = prv_cmd1(epd, CMD_DATA_ENTRY, DATA_ENTRY_XINC_YDEC);
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_X_RANGE, xr, sizeof(xr));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_Y_RANGE, yr, sizeof(yr));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_X_COUNTER, xc, sizeof(xc));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_Y_COUNTER, yc, sizeof(yc));
  }

  return ret;
}

static struct pbl_display_ssd1677_window prv_full_window(const struct pbl_display_ssd1677 *epd) {
  struct display_geometry g = prv_geometry(epd);

  return (struct pbl_display_ssd1677_window){
    .x0 = 0U,
    .x1 = display_geometry_scan_width(&g) / 8U - 1U,
    .y0 = 0U,
    .y1 = display_geometry_scan_height(&g) - 1U,
  };
}

/*
 * The RAM bytes that framebuffer rows y0..y1 cover, scaled and turned. Only the frame's own pixels:
 * the white border around it is never written after init. Mounted a quarter turn the rows are a
 * band of controller columns, else of controller rows.
 */
static struct pbl_display_ssd1677_window prv_window(const struct pbl_display_ssd1677 *epd,
                                                    uint16_t y0, uint16_t y1) {
  struct display_geometry g = prv_geometry(epd);
  bool flip = epd->state->flip;
  struct pbl_display_ssd1677_window w = {UINT16_MAX, 0U, UINT16_MAX, 0U};

  for (uint16_t cx = 0U; cx < display_geometry_scan_width(&g); cx++) {
    int32_t along = epd->along[cx];

    if (along >= 0 && (!g.quarter || (along >= y0 && along <= y1))) {
      w.x0 = PBL_MIN(w.x0, cx / 8U);
      w.x1 = PBL_MAX(w.x1, cx / 8U);
    }
  }
  for (uint16_t cy = 0U; cy < display_geometry_scan_height(&g); cy++) {
    int32_t across = display_geometry_across(&g, flip, cy);

    if (across >= 0 && (g.quarter || (across >= y0 && across <= y1))) {
      w.y0 = PBL_MIN(w.y0, cy);
      w.y1 = PBL_MAX(w.y1, cy);
    }
  }

  return w;
}

/*
 * What a plane gets for a pixel of detail, by its level (0 white to 3 black), 1 for white. The B/W
 * image draws every grey black: the grey pass only lightens. The old plane of an update makes the
 * grey pixels of the rows it changes white, so the new image drives them. The grey pass's planes
 * hold the B/W image, with class 1 (old 0, new 1) for light grey and 2 for dark grey: the classes
 * of equal planes are undriven.
 */
enum plane_map {
  MAP_BW,
  MAP_OLD,
  MAP_GREY_OLD,
  MAP_GREY_NEW,
};

static const uint8_t s_map_levels[4][4] = {
  [MAP_BW] = {1U, 0U, 0U, 0U},
  [MAP_OLD] = {1U, 1U, 1U, 0U},
  [MAP_GREY_OLD] = {1U, 0U, 1U, 0U},
  [MAP_GREY_NEW] = {1U, 1U, 0U, 0U},
};

// What a plane write draws: a frame, or 0 without, with the pixels of xor inverted; and the detail
// kept, mapped by map, MAP_OLD only in rows force_lo..force_hi, the grey maps only in rows the grey
// pass has not drawn
struct plane_src {
  const uint8_t *frame;
  const uint8_t *xor;
  const uint8_t *xor_rows;
  bool invert;
  // Without a frame copy, a grey update: the levels of rows levels_y0 on, 2 bits a pixel, for the
  // old plane or the new one
  const uint8_t *levels;
  uint16_t levels_y0;
  bool levels_new;
  enum plane_map map;
  bool cells;
  uint16_t force_lo;
  uint16_t force_hi;
};

static bool prv_row_bit(const uint8_t *bits, uint32_t y) {
  return (bits[y / 8U] >> (y % 8U)) & 1U;
}

static uint32_t prv_bit(const struct pbl_display_ssd1677 *epd, const uint8_t *frame, uint32_t along,
                        uint32_t across) {
  uint32_t pitch = (epd->disp.props.width + 7U) / 8U;

  if (epd->quarter) {
    return (frame[along * pitch + across / 8U] >> (across % 8U)) & 1U;
  }

  return (frame[across * pitch + along / 8U] >> (along % 8U)) & 1U;
}

static bool prv_has_grey(uint8_t levels) {
  for (uint32_t i = 0U; i < 4U; i++) {
    uint32_t l = (levels >> (2U * i)) & 3U;

    if (l == 1U || l == 2U) {
      return true;
    }
  }

  return false;
}

// One cell's panel pixels on controller row cy into its bytes from x0
static void prv_apply_cell(const struct pbl_display_ssd1677 *epd, const struct plane_src *src,
                           const struct pbl_display_ssd1677_cell *c, uint32_t y, uint32_t cy,
                           uint32_t x0, uint32_t len, uint8_t *out) {
  struct display_geometry g = prv_geometry(epd);
  bool grey_row = prv_row_bit(epd->grey_rows, y);
  enum plane_map map = src->map;

  if (map == MAP_OLD && !(grey_row && y >= src->force_lo && y <= src->force_hi)) {
    map = MAP_BW;
  }
  if ((map == MAP_GREY_OLD || map == MAP_GREY_NEW) && grey_row) {
    map = MAP_BW;
  }

  uint32_t inv = 0U;

  if (src->xor != nullptr) {
    inv = epd->quarter ? prv_bit(epd, src->xor, y, c->x) : prv_bit(epd, src->xor, c->x, y);
  }

  for (uint32_t i = 0U; i < 4U; i++) {
    uint32_t cx;
    uint32_t row;

    display_geometry_scan_pos(&g, epd->state->flip, c->x, y, i % 2U, i / 2U, &cx, &row);
    if (row != cy || cx < x0 * 8U || cx >= (x0 + len) * 8U) {
      continue;
    }

    uint8_t bit = (uint8_t)(0x80U >> (cx % 8U));
    uint8_t *byte = &out[cx / 8U - x0];

    if (s_map_levels[map][(c->levels >> (2U * i)) & 3U] ^ inv) {
      *byte |= bit;
    } else {
      *byte &= (uint8_t)~bit;
    }
  }
}

/*
 * The detail on controller row cy, framebuffer row or column across. Mounted a quarter turn it is a
 * column of the framebuffer, and each framebuffer row lo..hi keeps a cursor into its cells, which
 * the rows of one write meet in order. Returns whether any was drawn.
 */
static bool prv_map_cells(const struct pbl_display_ssd1677 *epd, const struct plane_src *src,
                          bool asc, uint32_t across, uint32_t cy, uint32_t x0, uint32_t len,
                          uint32_t lo, uint32_t hi, uint8_t *out) {
  const struct pbl_display_ssd1677_cell *cells = epd->cells;
  const uint16_t *start = epd->cell_start;
  bool drawn = false;

  if (!epd->quarter) {
    for (uint32_t k = start[across]; k < start[across + 1U]; k++) {
      if (cells[k].x >= lo && cells[k].x <= hi) {
        prv_apply_cell(epd, src, &cells[k], across, cy, x0, len, out);
        drawn = true;
      }
    }
    return drawn;
  }

  for (uint32_t y = lo; y <= hi; y++) {
    uint32_t k = epd->cursor[y];

    if (start[y] == start[y + 1U]) {
      continue;
    }
    if (asc) {
      while (k < start[y + 1U] && cells[k].x < across) {
        k++;
      }
      if (k < start[y + 1U] && cells[k].x == across) {
        prv_apply_cell(epd, src, &cells[k], y, cy, x0, len, out);
        drawn = true;
      }
    } else {
      // One past the cell looked at
      while (k > start[y] && cells[k - 1U].x > across) {
        k--;
      }
      if (k > start[y] && cells[k - 1U].x == across) {
        prv_apply_cell(epd, src, &cells[k - 1U], y, cy, x0, len, out);
        drawn = true;
      }
    }
    epd->cursor[y] = (uint16_t)k;
  }

  return drawn;
}

// One controller row's bytes x0..x0+len-1 from a frame; across is its framebuffer row, or column
// mounted a quarter turn, -1 on the border. The frame's pixels lo..hi along the row are unpacked
// into line first, one byte each: a scale-up reads each of them several times
static void prv_map_row(const struct pbl_display_ssd1677 *epd, const struct plane_src *src,
                        int32_t across, uint32_t x0, uint32_t len, uint32_t lo, uint32_t hi,
                        uint8_t *out) {
  uint8_t *line = epd->line;
  uint32_t edge = s_map_levels[src->map][0];

  if (across < 0) {
    memset(out, edge ? 0xFF : 0x00, len);
    return;
  }

  for (uint32_t a = lo; a <= hi; a++) {
    uint32_t v = (src->frame != nullptr) ? prv_bit(epd, src->frame, a, (uint32_t)across) : 0U;

    if (src->xor != nullptr) {
      v ^= prv_bit(epd, src->xor, a, (uint32_t)across);
    }
    if (src->xor_rows != nullptr) {
      v ^= prv_row_bit(src->xor_rows, epd->quarter ? a : (uint32_t)across);
    }
    line[a] = (uint8_t)(v ^ (src->invert ? 1U : 0U));
  }

  // RAM bytes are most significant bit first, the framebuffer's least; 1 is white. The border is
  // white: line[-1] is not read, a negative coordinate picks the edge instead
  const int16_t *along = &epd->along[x0 * 8U];

  for (uint32_t i = 0U; i < len; i++, along += 8) {
#define PIX(b) ((along[b] < 0) ? edge : (uint32_t)line[along[b]])
    out[i] = (uint8_t)((PIX(0) << 7) | (PIX(1) << 6) | (PIX(2) << 5) | (PIX(3) << 4) |
                       (PIX(4) << 3) | (PIX(5) << 2) | (PIX(6) << 1) | PIX(7));
#undef PIX
  }
}

/*
 * prv_map_row for a panel at the framebuffer's own size mounted a quarter turn, the common case at
 * 480x800: each RAM byte is 8 consecutive framebuffer rows of one column, gathered with a pointer
 * step instead of unpacking the column into line first. Rows in xor_rows are inverted.
 */
static PBL_SECTION_RAM void prv_map_row_quarter1(const struct pbl_display_ssd1677 *epd,
                                                 const struct plane_src *src, uint32_t across,
                                                 uint32_t x0, uint32_t len, uint8_t *out) {
  uint32_t pitch = (epd->disp.props.width + 7U) / 8U;
  uint32_t shift = across % 8U;
  const int16_t *along = &epd->along[x0 * 8U];

  for (uint32_t i = 0U; i < len; i++, along += 8) {
    // The column's rows for this byte, first to last bit, run up or down the framebuffer
    int32_t step = (along[1] > along[0]) ? (int32_t)pitch : -(int32_t)pitch;
    const uint8_t *p = &src->frame[(uint32_t)along[0] * pitch + across / 8U];
    uint32_t v = 0U;

    for (uint32_t b = 0U; b < 8U; b++, p += step) {
      v = (v << 1) | ((*p >> shift) & 1U);
    }
    if (src->xor_rows != nullptr) {
      for (uint32_t b = 0U; b < 8U; b++) {
        uint32_t a = (uint32_t)along[b];

        v ^= ((uint32_t)(src->xor_rows[a / 8U] >> (a % 8U)) & 1U) << (7U - b);
      }
    }
    if (src->levels != nullptr) {
      uint32_t lpitch = (epd->disp.props.width + 3U) / 4U;

      for (uint32_t b = 0U; b < 8U; b++) {
        uint32_t r = (uint32_t)along[b] - src->levels_y0;
        uint32_t lvl = (r < SSD1677_GREY_BAND)
                           ? (src->levels[r * lpitch + across / 4U] >> (2U * (across % 4U))) & 3U
                           : 0U;
        uint32_t bit = 1U << (7U - b);

        // Light grey is class 1 (old 0, new 1), dark grey class 2 (old 1, new 0)
        if (lvl == 1U) {
          v = src->levels_new ? (v | bit) : (v & ~bit);
        } else if (lvl == 2U) {
          v = src->levels_new ? (v & ~bit) : (v | bit);
        }
      }
    }
    out[i] = (uint8_t)(src->invert ? ~v : v);
  }
}

/*
 * Eight controller rows at once, for prv_map_row_quarter1 without grey or rows to invert: the
 * columns across[0..7] of one framebuffer byte. Each RAM byte position gathers one byte of each of
 * its 8 framebuffer rows and transposes the 8x8 bits (Hacker's Delight, transpose8): one load per
 * row for all 8 controller rows instead of 8 loads for each.
 */
static PBL_SECTION_RAM void prv_map_block_quarter1(const struct pbl_display_ssd1677 *epd,
                                                   const struct plane_src *src,
                                                   const int32_t across[8], uint32_t x0,
                                                   uint32_t len, uint8_t *out) {
  uint32_t pitch = (epd->disp.props.width + 7U) / 8U;
  const uint8_t *col = &src->frame[(uint32_t)across[0] / 8U];
  const int16_t *along = &epd->along[x0 * 8U];
  uint32_t inv = src->invert ? 0xFFU : 0U;

  for (uint32_t i = 0U; i < len; i++, along += 8) {
    uint32_t x = ((uint32_t)col[(uint32_t)along[0] * pitch] << 24) |
                 ((uint32_t)col[(uint32_t)along[1] * pitch] << 16) |
                 ((uint32_t)col[(uint32_t)along[2] * pitch] << 8) |
                 (uint32_t)col[(uint32_t)along[3] * pitch];
    uint32_t y = ((uint32_t)col[(uint32_t)along[4] * pitch] << 24) |
                 ((uint32_t)col[(uint32_t)along[5] * pitch] << 16) |
                 ((uint32_t)col[(uint32_t)along[6] * pitch] << 8) |
                 (uint32_t)col[(uint32_t)along[7] * pitch];
    uint32_t t;

    t = (x ^ (x >> 7)) & 0x00AA00AAU;
    x = x ^ t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00AA00AAU;
    y = y ^ t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCCU;
    x = x ^ t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000CCCCU;
    y = y ^ t ^ (t << 14);
    t = (x & 0xF0F0F0F0U) | ((y >> 4) & 0x0F0F0F0FU);
    y = ((x << 4) & 0xF0F0F0F0U) | (y & 0x0F0F0F0FU);
    x = t;

    // Column k of the result, most significant first, is bit 7 - k of the framebuffer byte
    const uint8_t cols[8] = {
      (uint8_t)(x >> 24), (uint8_t)(x >> 16), (uint8_t)(x >> 8), (uint8_t)x,
      (uint8_t)(y >> 24), (uint8_t)(y >> 16), (uint8_t)(y >> 8), (uint8_t)y,
    };

    for (uint32_t j = 0U; j < 8U; j++) {
      out[j * len + i] = (uint8_t)(cols[7U - (uint32_t)across[j] % 8U] ^ inv);
    }
  }
}

// A window of a frame, scaled and turned, to a RAM plane; rows a scale-up repeats are mapped once,
// unless detail tells them apart, and several go out per transfer
static int prv_write_rows(const struct pbl_display_ssd1677 *epd,
                          const struct pbl_display_ssd1677_window *w, const struct plane_src *src) {
  struct display_geometry g = prv_geometry(epd);
  bool flip = epd->state->flip;
  bool cells = src->cells && epd->cells != nullptr && epd->cell_start[epd->disp.props.height] != 0U;
  uint32_t len = w->x1 - w->x0 + 1U;
  uint32_t cap = display_geometry_scan_width(&g) / 8U * SSD1677_ROWS_PER_WRITE;
  uint32_t fill = 0U;
  int32_t prev = INT32_MIN;
  bool prev_cells = false;
  int ret = 0;

  // The frame's pixels the window's columns show
  uint32_t lo = UINT32_MAX;
  uint32_t hi = 0U;

  for (uint32_t cx = w->x0 * 8U; cx < (w->x1 + 1U) * 8U; cx++) {
    if (epd->along[cx] >= 0) {
      lo = PBL_MIN(lo, (uint32_t)epd->along[cx]);
      hi = PBL_MAX(hi, (uint32_t)epd->along[cx]);
    }
  }

  // The controller rows meet the framebuffer's columns in order, backwards flipped
  bool asc = !flip;
  // Every RAM bit a framebuffer pixel: no border, no scaling
  bool quarter1 = epd->quarter && epd->scale == 1U && !cells && src->frame != nullptr &&
                  src->xor == nullptr && lo <= hi && hi - lo + 1U == len * 8U;

  if (cells && epd->quarter) {
    for (uint32_t y = lo; y <= hi && lo <= hi; y++) {
      epd->cursor[y] = asc ? epd->cell_start[y] : epd->cell_start[y + 1U];
    }
  }

  for (uint32_t cy = w->y0; ret == 0 && cy <= w->y1; cy++) {
    int32_t across = display_geometry_across(&g, flip, cy);
    uint8_t *row = &epd->row[fill];

    // Eight rows that are the columns of one framebuffer byte fill a transfer at once
    if (quarter1 && src->levels == nullptr && src->xor_rows == nullptr && fill == 0U &&
        cy + 7U <= w->y1 && 8U * len <= cap) {
      int32_t cols[8];
      bool block = true;

      for (uint32_t j = 0U; j < 8U; j++) {
        cols[j] = display_geometry_across(&g, flip, cy + j);
        block = block && cols[j] >= 0 && cols[j] / 8 == cols[0] / 8;
      }
      if (block) {
        prv_map_block_quarter1(epd, src, cols, w->x0, len, row);
        ret = pbl_spi_write(&epd->spi, epd->row, 8U * len);
        prev = INT32_MIN;
        cy += 7U;
        continue;
      }
    }

    if (fill != 0U && across == prev && !prev_cells) {
      memcpy(row, row - len, len);
    } else if (quarter1 && across >= 0) {
      prv_map_row_quarter1(epd, src, (uint32_t)across, w->x0, len, row);
    } else {
      prv_map_row(epd, src, (lo <= hi) ? across : -1, w->x0, len, lo, hi, row);
      prev_cells = cells && lo <= hi && across >= 0 &&
                   prv_map_cells(epd, src, asc, (uint32_t)across, cy, w->x0, len, lo, hi, row);
    }
    prev = across;
    fill += len;

    if (fill + len > cap || cy == w->y1) {
      ret = pbl_spi_write(&epd->spi, epd->row, fill);
      fill = 0U;
    }
  }

  return ret;
}

static int prv_write_plane(const struct pbl_display_ssd1677 *epd, uint8_t ram,
                           const struct pbl_display_ssd1677_window *w,
                           const struct plane_src *src) {
  if (w->x0 > w->x1 || w->y0 > w->y1) {
    return 0;
  }
  int ret = prv_ram_window(epd, w);
  if (ret == 0) {
    ret = prv_cmd(epd, ram, nullptr, 0);
  }
  return ret != 0 ? ret : prv_write_rows(epd, w, src);
}

#ifdef CONFIG_DISPLAY_UC8279_X4
bool pbl_display_ssd1677_text_grey_supported(const struct pbl_display *disp) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  return !epd->state->uc8279 || epd->state->panel_id[0][3] == 0x68;
}

bool pbl_display_uc8279_motion_settle(const struct pbl_display *disp, bool arm) {
  if (disp->ops != &pbl_display_ssd1677_ops) {
    return false;
  }
  struct pbl_display_ssd1677_state *state = prv_epd(disp)->state;
  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  bool pending = state->uc8279 && state->unsettled_pulses != 0U &&
                 !state->next_frame.continuous;
  if (pending && arm) {
    state->need_half = true;
  }
  pbl_mutex_unlock(&state->lock);
  return pending;
}

uint32_t pbl_display_uc8279_completed_drives(const struct pbl_display *disp) {
  return prv_epd(disp)->state->completed_drives;
}

int epaper_write_command(const struct pbl_display_ssd1677 *epd, uint8_t command,
                         const uint8_t *data, size_t len) {
  return prv_cmd(epd, command, data, len);
}

int epaper_write_frame(const struct pbl_display_ssd1677 *epd, uint8_t command,
                       const uint8_t *frame) {
  const struct pbl_display_ssd1677_window w = prv_full_window(epd);
  const struct plane_src src = {.frame = frame, .invert = frame == nullptr};
  // The controller's first 120 gates are outside the visible panel.
  const uint32_t chunk = 8U * (w.x1 + 1U);
  int ret = prv_cmd(epd, command, nullptr, 0);
  memset(epd->row, 0xff, chunk);
  for (unsigned y = 0; ret == 0 && y < 120; y += 8) {
    ret = pbl_spi_write(&epd->spi, epd->row, chunk);
  }
  if (frame == nullptr) {
    for (unsigned y = 0; ret == 0 && y < epd->panel_width; y += 8) {
      ret = pbl_spi_write(&epd->spi, epd->row, chunk);
    }
    return ret;
  }
  return ret != 0 ? ret : prv_write_rows(epd, &w, &src);
}

int epaper_write_window(const struct pbl_display_ssd1677 *epd, uint8_t command,
                        const uint8_t *frame, const struct pbl_display_ssd1677_window *window) {
  int ret = prv_cmd(epd, command, nullptr, 0);
  return ret != 0 ? ret : prv_write_rows(epd, window, &(struct plane_src){.frame = frame});
}
#endif

static bool prv_has_idle_work(const struct pbl_display_ssd1677 *epd) {
  return epd->touched != nullptr || epd->touched_rows != nullptr || epd->full_on_idle ||
         epd->power_off_ms != 0U;
}

// Ends the refresh in flight once, from the BUSY interrupt or the timeout
static void prv_end(const struct pbl_display *disp, int status) {
  struct pbl_display_ssd1677_state *state = prv_epd(disp)->state;
  bool refreshing;

  pbl_irq_lock();
  refreshing = state->refreshing;
  state->refreshing = false;
  pbl_irq_unlock();

  if (!refreshing) {
    return;
  }

  (void)pbl_intc_disable(&state->busy_irq);
  pbl_timer_stop(&state->timer);
  if (status != 0) {
    state->need_half = true;
  }
  state->idle_since_ms = (uint32_t)pbl_uptime_ms();
  state->last_refresh_ms = (uint16_t)(state->idle_since_ms - state->refresh_start_ms);
#ifdef CONFIG_SHELL
  if (status == 0 && !state->own) {
    if (state->completed_updates++ == 0U) {
      state->first_update_ms = state->idle_since_ms;
    }
    state->last_update_ms = state->idle_since_ms;
  }
#endif
  // Without a frame copy the planes are re-synced first, from the framebuffer
  if (prv_epd(disp)->shadow == nullptr) {
    state->post_status = status;
    (void)pbl_work_submit_boosted(&pbl_sys_workq, &state->post);
    return;
  }
  if (prv_has_idle_work(prv_epd(disp))) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle,
                                      PBL_MSEC(prv_epd(disp)->idle_ms));
  }

  if (!state->own) {
    pbl_display_complete(disp, status);
  }
}

static void prv_busy_isr(void *arg) {
  prv_end(arg, 0);
}

static void prv_timeout(struct pbl_timer *timer) {
  struct pbl_display_ssd1677_state *state =
      PBL_CONTAINER_OF(timer, struct pbl_display_ssd1677_state, timer);

  prv_end(state->disp, -ETIMEDOUT);
}

static int prv_controller_init(const struct pbl_display_ssd1677 *epd) {
  struct display_geometry g = prv_geometry(epd);
  uint16_t gates = display_geometry_scan_height(&g) - 1U;
  const uint8_t output[] = {gates & 0xFFU, gates >> 8, DRIVER_OUTPUT_SCAN};
  int ret;

  // High, low and high again, 10 ms each, as the stock firmware does
  pbl_sleep_ms(RESET_MS);
  (void)pbl_gpio_set(&epd->reset, true);
  pbl_sleep_ms(RESET_MS);
  (void)pbl_gpio_set(&epd->reset, false);
  pbl_sleep_ms(RESET_MS);

  ret = prv_cmd(epd, CMD_SW_RESET, nullptr, 0U);
  if (ret != 0) {
    return ret;
  }
  // BUSY may not be up yet at once
  pbl_sleep_ms(RESET_MS);
  ret = prv_wait_busy(epd);

  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_TEMP_SENSOR, TEMP_SENSOR_INTERNAL);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_BOOSTER, s_booster, sizeof(s_booster));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_DRIVER_OUTPUT, output, sizeof(output));
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_BORDER, BORDER_INIT);
  }
  if (ret == 0) {
    struct pbl_display_ssd1677_window all = prv_full_window(epd);

    ret = prv_ram_window(epd, &all);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_AUTO_WRITE_BW, AUTO_WRITE_WHITE);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_AUTO_WRITE_RED, AUTO_WRITE_WHITE);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }

  return ret;
}

static void prv_idle_work(struct pbl_work *work);
static void prv_post(struct pbl_work *work);

static int prv_init(const struct pbl_display *disp) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t edge;
  int ret;

  if (pbl_device_init(&epd->spi.bus->dev) != 0 || pbl_device_init(&epd->dc.port->dev) != 0 ||
      pbl_device_init(&epd->reset.port->dev) != 0 || pbl_device_init(&epd->busy.port->dev) != 0) {
    return -ENODEV;
  }

  ret = pbl_spi_setup(&epd->spi);
  if (ret == 0) {
    ret = pbl_gpio_configure(&epd->dc, PBL_GPIO_OUTPUT_ACTIVE);
  }
  if (ret == 0) {
    ret = pbl_gpio_configure(&epd->reset, PBL_GPIO_OUTPUT_INACTIVE);
  }
  if (ret == 0) {
    ret = pbl_gpio_configure(&epd->busy, PBL_GPIO_INPUT);
  }
  if (ret != 0) {
    return ret;
  }

  state->disp = disp;
  state->need_half = true;
  pbl_mutex_init(&state->lock);
  pbl_delayed_work_init(&state->idle, prv_idle_work);

#ifdef CONFIG_DISPLAY_UC8279_X4
  ret = uc8279_x4_probe(epd);
  if (ret != 0) {
    return ret;
  }
  if (state->uc8279) {
    return uc8279_x4_init(epd);
  }
#endif

  // BUSY drops when a refresh ends
  edge = ((epd->busy.flags & PBL_GPIO_ACTIVE_LOW) != 0U) ? PBL_IRQ_TYPE_EDGE_RISING
                                                         : PBL_IRQ_TYPE_EDGE_FALLING;
  ret = pbl_gpio_to_irq(&epd->busy, edge, &state->busy_irq);
  if (ret != 0) {
    return ret;
  }
  if (pbl_device_init(&state->busy_irq.intc->dev) != 0) {
    return -ENODEV;
  }

  state->refreshing = false;
  state->own = false;
  state->dirty_valid = false;
  pbl_work_init(&state->post, prv_post);
  state->post_pending = false;
  state->resync_valid = false;
  // The panel's own fast waveform, with its main phase cut, or that phase alone, on changes only
  if (epd->fast_differential) {
    memcpy(state->wave.vs, s_diff_vs, sizeof(state->wave.vs));
    memset(state->wave.phases, 0, sizeof(state->wave.phases));
  } else {
    memcpy(state->wave.vs, s_lut_vs, sizeof(state->wave.vs));
    memcpy(state->wave.phases, s_lut_phases, sizeof(state->wave.phases));
  }
  state->wave.phases[1] = epd->fast_frames;
  state->wave.frame_rate = epd->fast_frame_rate;
  memcpy(state->wave.source, epd->source, sizeof(state->wave.source));
  state->wave.topup_frames = epd->topup_frames;
  if (epd->fast_differential && epd->fast_frame_rate == 0x22U && epd->topup_frames != 0U &&
      epd->full_on_idle) {
    state->motion_frames = CONFIG_DISPLAY_SSD1677_MOTION_FRAMES;
  }
  // Four extra frames on a large change (~138 ms total) clear white-to-black and black-to-white
  // app-switch ghosts in a single drive without needing a second idle pass
  state->wave.large_frames = 4U;
  state->powered = false;
  state->stale_valid = false;
  state->updates = 0U;
  pbl_timer_init(&state->timer, prv_timeout);

  ret = pbl_intc_request(&state->busy_irq, prv_busy_isr, (void *)disp);
  if (ret != 0) {
    return ret;
  }

  // Unknown until the first update: the frame is white, as the RAM
  if (epd->shadow != nullptr) {
    memset(epd->shadow, 0xFF, (disp->props.width + 7U) / 8U * disp->props.height);
  }

  return prv_controller_init(epd);
}

static int prv_enable(const struct pbl_display *disp) {
  return 0;
}

static int prv_disable(const struct pbl_display *disp) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  int ret;

  (void)pbl_delayed_work_cancel(&epd->state->idle);
  (void)pbl_mutex_lock(&epd->state->lock, PBL_FOREVER);
  if (epd->state->refreshing || epd->state->post_pending) {
    pbl_mutex_unlock(&epd->state->lock);
    return -EBUSY;
  }

#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    if (pbl_display_update_in_progress(disp)) {
      pbl_mutex_unlock(&epd->state->lock);
      return -EBUSY;
    }
    ret = uc8279_x4_power_off(epd);
    epd->state->need_half = true;
    pbl_mutex_unlock(&epd->state->lock);
    return ret;
  }
#endif
  ret = prv_cmd1(epd, CMD_BORDER, BORDER_INIT);
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_UPDATE_CTRL2, SEQ_POWER_OFF);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_ACTIVATE, nullptr, 0U);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }

  epd->state->frame = nullptr;
  epd->state->need_half = true;
  epd->state->powered = false;
  pbl_mutex_unlock(&epd->state->lock);

  return ret;
}

static int prv_set_rotated(const struct pbl_display *disp, bool rotated) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct display_geometry g = prv_geometry(epd);

#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279 && pbl_display_update_in_progress(disp)) {
    return -EBUSY;
  }
#endif

  // The whole panel moves: the shadow no longer says what it shows
  if (epd->state->flip != rotated) {
    epd->state->need_half = true;
    epd->state->stale_valid = false;
  }
  epd->state->flip = rotated;
  for (uint32_t cx = 0U; cx < display_geometry_scan_width(&g); cx++) {
    epd->along[cx] = (int16_t)display_geometry_along(&g, rotated, cx);
  }

  return 0;
}

static enum refresh_mode prv_mode(const struct pbl_display_ssd1677 *epd, bool small) {
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t now = (uint32_t)pbl_uptime_ms();

  state->short_drive = state->motion_frames != 0U && small && state->powered &&
                       !state->need_half && now - state->last_user_start_ms <= 150U;
  state->last_user_start_ms = now;
  if (state->need_half) {
    state->need_half = false;
    state->updates = 0U;
    return REFRESH_HALF;
  }

  if (epd->full_interval != 0U && ++state->updates >= epd->full_interval) {
    if (state->updates == epd->full_interval) {
      state->clean_due_ms = now;
    }
    // Continuous small motion can defer a due clean for four seconds, capped at 120 extra drives.
    uint32_t extra = state->short_drive && now - state->clean_due_ms < 4000U
                         ? 120U
                         : epd->full_interval;
    if (!epd->full_on_idle || state->updates >= epd->full_interval + extra) {
      state->updates = 0U;
      return REFRESH_HALF;
    }
  }
  return REFRESH_FAST;
}

// One group of phases and a voltage pattern per pixel class, with the fast waveform's frame rate
// and voltages, to the controller
static int prv_load_lut(const struct pbl_display_ssd1677 *epd, const uint8_t phases[4],
                        const uint8_t vs[4]) {
  const struct pbl_display_ssd1677_wave *wave = &epd->state->wave;
  uint8_t lut[LUT_LEN] = {0};
  int ret;

  for (uint32_t i = 0U; i < 4U; i++) {
    lut[i * 10U] = vs[i];
  }
  memcpy(&lut[50], phases, 4U);
  lut[99] = 0x01U;
  memset(&lut[100], wave->frame_rate, 5U);

  ret = prv_cmd(epd, CMD_WRITE_LUT, lut, sizeof(lut));
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_GATE_VOLTAGE, GATE_VOLTAGE);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_SOURCE_VOLTAGE, wave->source, sizeof(wave->source));
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_VCOM, VCOM);
  }

  return ret;
}

/*
 * The X4's own 4-level overlay, LUT and voltages as its stock firmware has them (via FreeInk,
 * MIT): over a B/W refresh that drew every grey pixel black, class 2 (old 1, new 0) gets 9 frames
 * towards white, light grey, class 3 (old 1, new 1) 6 frames, dark grey; class 0 is undriven.
 * Three groups of four 1-frame phases, at frame rate codes 8 and F, about 60 ms.
 */
static const uint8_t s_grey_vs[4][3] = {
  {0x00U, 0x00U, 0x00U},
  {0x54U, 0x54U, 0x40U},
  {0xAAU, 0xA0U, 0xA8U},
  {0xA2U, 0x22U, 0x20U},
};
#define GREY_FRAME_RATE 0x8FU
#define GREY_VCOM       0x30U
#define VS_VSL          0x2U

/*
 * 0 light frames: the stock table; else that many phases of 1 frame towards white for light grey,
 * dark ones for dark grey, from the first on, four per group, then tail undriven ones. Each
 * undriven phase after a drive darkens it again much more than its length suggests, so the
 * drives start together. Measured on the X4 (camera luminance white/light/dark/black, -1.2 V):
 * stock 151/70/27/4; 12, 8 and a tail of 1: 152/81/35/4; 12, 9, tail 2: 152/72/41/4.
 */
static uint8_t s_grey_light = 12U;
static uint8_t s_grey_dark = 8U;
static uint8_t s_grey_frame_rate = GREY_FRAME_RATE;
static uint8_t s_grey_tail = 1U;

// Light grey on pixel class light, dark grey on the next
static int prv_load_grey(const struct pbl_display_ssd1677 *epd, uint8_t vcom, uint32_t light) {
  uint8_t lut[LUT_LEN] = {0};
  int ret;

  if (s_grey_light == 0U) {
    for (uint32_t c = 0U; c < 4U; c++) {
      uint32_t from = c + 2U - light;

      for (uint32_t g = 0U; g < 3U && from < 4U && (light == 2U || from >= 2U); g++) {
        lut[c * 10U + g] = s_grey_vs[from][g];
      }
    }
    for (uint32_t g = 0U; g < 3U; g++) {
      memset(&lut[50U + g * 5U], 1, 4U);
    }
  } else {
    uint32_t total = PBL_MIN(PBL_MAX(s_grey_light, s_grey_dark) + s_grey_tail, 40U);

    for (uint32_t ph = 0U; ph < total; ph++) {
      uint32_t g = ph / 4U;
      uint32_t shift = (3U - ph % 4U) * 2U;

      lut[50U + g * 5U + ph % 4U] = 1U;
      if (ph < s_grey_light) {
        lut[light * 10U + g] |= (uint8_t)(VS_VSL << shift);
      }
      if (ph < s_grey_dark) {
        lut[(light + 1U) * 10U + g] |= (uint8_t)(VS_VSL << shift);
      }
    }
  }
  memset(&lut[100], s_grey_frame_rate, 5U);

  ret = prv_cmd(epd, CMD_WRITE_LUT, lut, sizeof(lut));
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_GATE_VOLTAGE, GATE_VOLTAGE);
  }
  if (ret == 0) {
    ret =
        prv_cmd(epd, CMD_SOURCE_VOLTAGE, epd->state->wave.source, sizeof(epd->state->wave.source));
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_VCOM, vcom);
  }

  return ret;
}

// A fast refresh drives changes boost frames more than the fast waveform's main phase
static int prv_refresh(const struct pbl_display *disp, enum refresh_mode mode, bool own,
                       uint8_t boost) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  uint8_t ctrl1 = (mode == REFRESH_HALF) ? CTRL1_BYPASS_RED : CTRL1_NORMAL;
  uint8_t seq = SEQ_HALF;
  int ret;

  ret = prv_cmd1(epd, CMD_UPDATE_CTRL1, ctrl1);
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_BORDER, BORDER_REFRESH);
  }
  if (ret == 0 && mode == REFRESH_HALF) {
    state->topup_extra = 0U;
    ret = prv_cmd1(epd, CMD_TEMP_WRITE, HALF_TEMP);
  }
  // A half refresh loads the OTP waveforms over the LUT, so it goes in before every fast one
  if (ret == 0 && mode == REFRESH_FAST) {
    seq = SEQ_FAST;
    if (state->wave.phases[1] != 0U) {
      uint8_t phases[4];

      memcpy(phases, state->wave.phases, sizeof(phases));
      if (state->short_drive && boost == 0U && state->motion_frames < phases[1]) {
        uint8_t extra = phases[1] - state->motion_frames;
        state->topup_extra = PBL_MAX(state->topup_extra, extra);
        phases[1] = state->motion_frames;
      }
      phases[1] = (uint8_t)PBL_MIN(phases[1] + boost, UINT8_MAX);
      seq = state->powered ? SEQ_CUSTOM_LUT : (SEQ_CLOCK_ANALOG | SEQ_CUSTOM_LUT);
      ret = prv_load_lut(epd, phases, state->wave.vs);
    }
  }
  if (ret == 0 && mode == REFRESH_TOPUP) {
    uint8_t frames = (uint8_t)PBL_MIN(state->wave.topup_frames + state->topup_extra, UINT8_MAX);
    const uint8_t phases[4] = {0U, frames, 0U, 0U};
    state->topup_extra = 0U;

    seq = state->powered ? SEQ_CUSTOM_LUT : (SEQ_CLOCK_ANALOG | SEQ_CUSTOM_LUT);
    ret = prv_load_lut(epd, phases, s_diff_vs);
  }
  if (ret == 0 && mode == REFRESH_GREY) {
    seq = state->powered ? SEQ_CUSTOM_LUT : (SEQ_CLOCK_ANALOG | SEQ_CUSTOM_LUT);
    ret = prv_load_grey(epd, GREY_VCOM, 1U);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_UPDATE_CTRL2, seq);
  }
  if (ret != 0) {
    return ret;
  }
  // The half refresh turns them off at its end, the others leave them on
  state->powered = (mode != REFRESH_HALF);
  state->own = own;

  // The interrupt is armed before the refresh starts, so its end cannot be missed; the deadline
  // runs from the activation, however late that comes
  state->post_pending = epd->shadow == nullptr;
  state->refresh_start_ms = (uint32_t)pbl_uptime_ms();
  state->refreshing = true;
  ret = pbl_intc_enable(&state->busy_irq);
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_ACTIVATE, nullptr, 0U);
  }

  bool refreshing;

  pbl_irq_lock();
  if (ret == 0) {
    // Not once the interrupt ended it: a stale deadline would end the next refresh
    if (state->refreshing) {
      pbl_timer_start(&state->timer, PBL_MSEC(REFRESH_TIMEOUT_MS));
    }
    pbl_irq_unlock();
    return 0;
  }
  refreshing = state->refreshing;
  state->refreshing = false;
  // Not started: no re-sync follows
  if (refreshing) {
    state->post_pending = false;
  }
  pbl_irq_unlock();
  (void)pbl_intc_disable(&state->busy_irq);

  // A refresh the activation started anyway has ended through the interrupt, which completed it
  return refreshing ? ret : 0;
}

static uint32_t prv_frame_size(const struct pbl_display_ssd1677 *epd) {
  return (epd->disp.props.width + 7U) / 8U * epd->disp.props.height;
}

static void prv_union(struct pbl_display_ssd1677_window *a,
                      const struct pbl_display_ssd1677_window *b) {
  a->x0 = PBL_MIN(a->x0, b->x0);
  a->x1 = PBL_MAX(a->x1, b->x1);
  a->y0 = PBL_MIN(a->y0, b->y0);
  a->y1 = PBL_MAX(a->y1, b->y1);
}

// Both planes start over with the shadow; nothing is left to top up
static int prv_write_half(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *state = epd->state;
  struct pbl_display_ssd1677_window w = prv_window(epd, 0U, epd->disp.props.height - 1U);
  int ret;

  const struct plane_src bw = {.frame = epd->shadow, .cells = true};

  state->stale_valid = false;
  ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, &bw);
  // The half refresh is not blind to the old plane, whatever 0x21 says: without the new image
  // there too, what it turns black greys out at the next fast refresh
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, &bw);
  }
  // It draws no grey
  if (epd->cells != nullptr) {
    memset(epd->grey_rows, 0, (epd->disp.props.height + 7U) / 8U);
  }
  state->dirty_valid = false;
  if (epd->touched != nullptr) {
    memset(epd->touched, 0, prv_frame_size(epd));
  }

  return ret;
}

/*
 * A top up drives the pixels changed by fast refreshes since the last one, those changed back
 * too, towards their colour: short drives leave both the new colour and the one a change passed
 * through grey. Over those rows, and where the planes may differ, the new plane gets the frame
 * shown and the old one the frame with those pixels inverted, which makes each of them a change
 * and leaves the others undriven.
 */
static int prv_topup(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *state = epd->state;
  const struct plane_src bw = {.frame = epd->shadow, .cells = true};
  const struct plane_src inv = {.frame = epd->shadow, .xor = epd->touched, .cells = true};
  struct pbl_display_ssd1677_window u = state->dirty;
  int ret;

  if (state->stale_valid) {
    prv_union(&u, &state->stale);
  }
  ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &u, &bw);
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &u, &inv);
  }
  memset(epd->touched, 0, prv_frame_size(epd));
  if (ret == 0) {
    ret = prv_refresh(state->disp, REFRESH_TOPUP, true, 0U);
  }
  if (ret != 0) {
    state->need_half = true;
    return ret;
  }

  state->stale = state->dirty;
  state->stale_valid = true;
  state->dirty_valid = false;

  return 0;
}

static int prv_power_off(const struct pbl_display_ssd1677 *epd) {
  int ret;

  ret = prv_cmd1(epd, CMD_UPDATE_CTRL2, SEQ_ANALOG_OFF);
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_ACTIVATE, nullptr, 0U);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }
  epd->state->powered = false;

  return ret;
}

// The analog supplies go off once no refresh ran for long enough
static void prv_idle_power(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *state = epd->state;

  if (epd->power_off_ms == 0U || !state->powered) {
    return;
  }

  uint32_t idle = (uint32_t)pbl_uptime_ms() - state->idle_since_ms;

  if (idle >= epd->power_off_ms) {
    if (prv_power_off(epd) != 0) {
      state->need_half = true;
    }
  } else {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle,
                                      PBL_MSEC(epd->power_off_ms - idle));
  }
}

/*
 * Without a frame copy. The planes hold the frame shown everywhere between refreshes: after each,
 * both are written again from the framebuffer over what it drove, before its completion, while
 * the compositor leaves the framebuffer alone. A fast update then writes only the new plane over
 * its rows; a CRC per row tells them. The top up drives every pixel of the rows fast refreshes
 * changed, inverting them in the old plane. The driver's own refreshes read the framebuffer when
 * nothing waits to be shown, so only where its rows' CRCs say it still holds the frame shown;
 * rows it could not re-sync are driven whole by the next update.
 */
static uint32_t prv_row_crc(const struct pbl_display_ssd1677 *epd, const uint8_t *frame,
                            uint32_t y) {
  uint32_t pitch = (epd->disp.props.width + 7U) / 8U;

  return pbl_crc32(0U, &frame[y * pitch], pitch);
}

static bool prv_frame_shown(const struct pbl_display_ssd1677 *epd, uint32_t lo, uint32_t hi) {
  for (uint32_t y = lo; y <= hi; y++) {
    if (prv_row_crc(epd, epd->state->frame, y) != epd->row_crc[y]) {
      return false;
    }
  }

  return true;
}

static void prv_rows_set(uint8_t *bits, uint32_t lo, uint32_t hi, bool set) {
  for (uint32_t y = lo; y <= hi; y++) {
    if (set) {
      bits[y / 8U] |= (uint8_t)(1U << (y % 8U));
    } else {
      bits[y / 8U] &= (uint8_t)~(1U << (y % 8U));
    }
  }
}

// The first and last row set, false for none
static bool prv_rows_range(const struct pbl_display_ssd1677 *epd, const uint8_t *bits, uint32_t *lo,
                           uint32_t *hi) {
  bool any = false;

  for (uint32_t y = 0U; y < epd->disp.props.height; y++) {
    if (prv_row_bit(bits, y)) {
      *lo = any ? *lo : y;
      *hi = y;
      any = true;
    }
  }

  return any;
}

static void prv_rows_clear_mask(uint8_t *dst, const uint8_t *mask, uint32_t height) {
  const uint32_t bytes = (height + 7U) / 8U;

  for (uint32_t i = 0U; i < bytes; i++) {
    dst[i] &= (uint8_t)~mask[i];
  }
}

// After a refresh: both planes back to the frame shown over what it drove, then its completion
static void prv_post(struct pbl_work *work) {
  struct pbl_display_ssd1677_state *state =
      PBL_CONTAINER_OF(work, struct pbl_display_ssd1677_state, post);
  const struct pbl_display *disp = state->disp;
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  int status = state->post_status;
  uint32_t t0 = (uint32_t)pbl_uptime_ms();

  if (status == 0 && state->resync_valid) {
    uint32_t lo = state->resync_lo;
    uint32_t hi = state->resync_hi;
    struct pbl_display_ssd1677_window w = prv_window(epd, (uint16_t)lo, (uint16_t)hi);
    const struct plane_src src = {.frame = state->frame};

    // The driver's own refresh: a new frame may be in the framebuffer already
    if (state->own && !prv_frame_shown(epd, lo, hi)) {
      prv_rows_set(epd->unsynced_rows, lo, hi, true);
    } else if ((state->resync_planes != 2U &&
                prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, &src) != 0) ||
               (state->resync_planes != 1U &&
                prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, &src) != 0)) {
      state->need_half = true;
    } else if (state->own && !prv_frame_shown(epd, lo, hi)) {
      prv_rows_set(epd->unsynced_rows, lo, hi, true);
    }
  }
  state->resync_valid = false;
  if (!state->own) {
    state->last_post_ms = (uint16_t)((uint32_t)pbl_uptime_ms() - t0);
  }
  if (prv_has_idle_work(epd)) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle, PBL_MSEC(epd->idle_ms));
  }
  state->post_pending = false;
  if (!state->own) {
    pbl_display_complete(disp, status);
  }
}

static int prv_topup_nocopy(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t lo;
  uint32_t hi;
  int ret;

  // A new frame on its way comes first, and its update leaves these rows touched
  if (!prv_rows_range(epd, epd->touched_rows, &lo, &hi) || !prv_frame_shown(epd, lo, hi)) {
    return 0;
  }

  // The new plane holds the frame already
  struct pbl_display_ssd1677_window w = prv_window(epd, (uint16_t)lo, (uint16_t)hi);

  ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w,
                        &(struct plane_src){.frame = state->frame, .xor_rows = epd->touched_rows});
  // ponytail: a frame drawn while the old plane went out leaves it unknown: a clean refresh
  if (ret == 0 && !prv_frame_shown(epd, lo, hi)) {
    ret = -EAGAIN;
  }
  if (ret == 0) {
    prv_rows_clear_mask(epd->grey_shown_rows, epd->touched_rows, epd->disp.props.height);
    memset(epd->touched_rows, 0, (epd->disp.props.height + 7U) / 8U);
    state->resync_lo = (uint16_t)lo;
    state->resync_hi = (uint16_t)hi;
    state->resync_valid = true;
    ret = prv_refresh(state->disp, REFRESH_TOPUP, true, 0U);
  }
  if (ret != 0) {
    state->resync_valid = false;
    state->need_half = true;
  }

  return ret;
}

// Both planes whole, from the framebuffer of the last update
static int prv_write_half_nocopy(const struct pbl_display_ssd1677 *epd) {
  uint32_t height = epd->disp.props.height;
  struct pbl_display_ssd1677_window w = prv_window(epd, 0U, (uint16_t)(height - 1U));
  const struct plane_src src = {.frame = epd->state->frame};
  int ret;

  ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, &src);
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, &src);
  }
  memset(epd->unsynced_rows, 0, (height + 7U) / 8U);
  memset(epd->grey_shown_rows, 0, (height + 7U) / 8U);
  if (epd->touched_rows != nullptr) {
    memset(epd->touched_rows, 0, (height + 7U) / 8U);
  }

  return ret;
}

/*
 * A grey update without a frame copy: the levels the detail gives for each row of the frame shown,
 * read a band at a time while the update runs, lighten its grey pixels, which the frame has black,
 * in one refresh; both planes then go back to the frame. Rows it drew are not drawn again until
 * they change, and a change to one drives it whole, grey pixels with it. Returns 1 when there is
 * nothing to do.
 */
static int prv_grey_nocopy(const struct pbl_display_ssd1677 *epd,
                           const struct pbl_display_buffer *buf, uint16_t y0, uint16_t y1) {
  struct pbl_display_ssd1677_state *state = epd->state;
  const struct pbl_display_detail *detail = buf->detail;
  uint32_t lpitch = (epd->disp.props.width + 3U) / 4U;
  int32_t lo = -1;
  int32_t hi = -1;
  int ret = 0;

  if (state->need_half || buf->data != state->frame || !prv_frame_shown(epd, y0, y1)) {
    return 1;
  }
  // A top up still due would drive the grey back to black: it goes first
  uint32_t tlo;
  uint32_t thi;

  if (epd->touched_rows != nullptr && prv_rows_range(epd, epd->touched_rows, &tlo, &thi)) {
    ret = prv_topup_nocopy(epd);
    if (ret == 0 && PBL_POLL_SLEEP_MS(!state->refreshing && !state->post_pending, 1U,
                                      WAIT_IDLE_TIMEOUT_MS) != 0) {
      return -ETIMEDOUT;
    }
    if (ret != 0 || state->need_half) {
      return (ret != 0) ? ret : 1;
    }
  }

  for (uint32_t b0 = y0 - (y0 % SSD1677_GREY_BAND); b0 <= y1; b0 += SSD1677_GREY_BAND) {
    uint32_t b1 = PBL_MIN(b0 + SSD1677_GREY_BAND - 1U, (uint32_t)epd->disp.props.height - 1U);
    bool any = false;

    memset(epd->grey_band, 0, SSD1677_GREY_BAND * lpitch);
    for (uint32_t y = PBL_MAX(b0, (uint32_t)y0); y <= PBL_MIN(b1, (uint32_t)y1); y++) {
      if (!prv_row_bit(epd->grey_shown_rows, y) &&
          detail->row(detail, (uint16_t)y, &epd->grey_band[(y - b0) * lpitch])) {
        prv_rows_set(epd->grey_shown_rows, y, y, true);
        any = true;
      }
    }
    if (!any) {
      continue;
    }

    struct pbl_display_ssd1677_window w = prv_window(epd, (uint16_t)b0, (uint16_t)b1);
    struct plane_src src = {
      .frame = buf->data,
      .levels = epd->grey_band,
      .levels_y0 = (uint16_t)b0
    };

    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, &src);
    src.levels_new = true;
    if (ret == 0) {
      ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, &src);
    }
    if (ret != 0) {
      break;
    }
    lo = (lo < 0) ? (int32_t)b0 : lo;
    hi = (int32_t)b1;
  }
  if (ret == 0 && lo < 0) {
    return 1;
  }

  state->resync_lo = (uint16_t)PBL_MAX(lo, 0);
  state->resync_hi = (uint16_t)PBL_MAX(hi, 0);
  state->resync_valid = ret == 0;
  if (ret == 0) {
    ret = prv_refresh(state->disp, REFRESH_GREY, false, 0U);
  }
  if (ret != 0) {
    state->resync_valid = false;
    state->need_half = true;
  }

  return ret;
}

// Returns 1 for a frame unchanged
static int prv_update_nocopy(const struct pbl_display_ssd1677 *epd,
                             const struct pbl_display_buffer *buf, uint16_t y0, uint16_t y1) {
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t height = epd->disp.props.height;
  int32_t first = -1;
  int32_t last = -1;
  int ret = 0;

  if (buf->pitch != (epd->disp.props.width + 7U) / 8U) {
    return -EINVAL;
  }
  state->frame = buf->data;

  // What the panel shows is not known: every row
  if (state->need_half) {
    y0 = 0U;
    y1 = (uint16_t)(height - 1U);
  }
  for (uint32_t y = y0; y <= y1; y++) {
    uint32_t crc = prv_row_crc(epd, buf->data, y);

    if (crc != epd->row_crc[y] || state->need_half) {
      epd->row_crc[y] = crc;
      // Its grey pixels are not in the old plane: the row is driven whole
      if (prv_row_bit(epd->grey_shown_rows, y)) {
        prv_rows_set(epd->grey_shown_rows, y, y, false);
        prv_rows_set(epd->unsynced_rows, y, y, true);
      }
      first = (first < 0) ? (int32_t)y : first;
      last = (int32_t)y;
    }
  }
  if (first < 0 && !state->need_half) {
    return 1;
  }

  enum refresh_mode mode = prv_mode(
      epd, first >= 0 && (uint32_t)(last - first + 1) * 2U < epd->disp.props.height);

  if (mode == REFRESH_HALF) {
    ret = prv_write_half_nocopy(epd);
    if (ret == 0) {
      ret = prv_refresh(state->disp, REFRESH_HALF, false, 0U);
    }
    if (ret != 0) {
      state->need_half = true;
    }
    return ret;
  }

  uint32_t t0 = (uint32_t)pbl_uptime_ms();
  struct pbl_display_ssd1677_window w = prv_window(epd, (uint16_t)first, (uint16_t)last);
  uint32_t lo;
  uint32_t hi;
  // A change of half the rows or more, as an app switch, drives every pixel of its rows in one
  // longer refresh: unchanged ones too, so nothing of the frame before stays as a ghost, and no
  // top up follows to draw the panel a second time
  bool large = (uint32_t)(last - first + 1) * 2U >= height;

  if (large) {
    if (prv_rows_range(epd, epd->unsynced_rows, &lo, &hi)) {
      first = PBL_MIN(first, (int32_t)lo);
      last = PBL_MAX(last, (int32_t)hi);
      w = prv_window(epd, (uint16_t)first, (uint16_t)last);
      prv_rows_clear_mask(epd->grey_shown_rows, epd->unsynced_rows, height);
      memset(epd->unsynced_rows, 0, (height + 7U) / 8U);
    }
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w,
                          &(struct plane_src){.frame = buf->data, .invert = true});
    // Driven whole, grey included
    prv_rows_set(epd->grey_shown_rows, (uint32_t)first, (uint32_t)last, false);
  } else if (prv_rows_range(epd, epd->unsynced_rows, &lo, &hi)) {
    // Rows whose old plane is not known are driven whole, with the changed rows among them
    struct pbl_display_ssd1677_window u = prv_window(epd, (uint16_t)lo, (uint16_t)hi);

    for (uint32_t y = (uint32_t)first; y <= (uint32_t)last; y++) {
      if (y >= lo && y <= hi) {
        prv_rows_set(epd->unsynced_rows, y, y, true);
      }
    }
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &u,
                          &(struct plane_src){.frame = buf->data, .xor_rows = epd->unsynced_rows});
    prv_rows_clear_mask(epd->grey_shown_rows, epd->unsynced_rows, height);
    memset(epd->unsynced_rows, 0, (height + 7U) / 8U);
    prv_union(&w, &u);
    first = PBL_MIN(first, (int32_t)lo);
    last = PBL_MAX(last, (int32_t)hi);
  }
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, &(struct plane_src){.frame = buf->data});
  }
  if (epd->touched_rows != nullptr) {
    prv_rows_set(epd->touched_rows, (uint32_t)first, (uint32_t)last, !large);
  }
  state->resync_lo = (uint16_t)first;
  state->resync_hi = (uint16_t)last;
  state->resync_valid = true;
  state->last_write_ms = (uint16_t)((uint32_t)pbl_uptime_ms() - t0);
  if (ret == 0) {
    ret = prv_refresh(state->disp, REFRESH_FAST, false, large ? state->wave.large_frames : 0U);
  }
  if (ret != 0) {
    state->resync_valid = false;
    state->need_half = true;
  }

  return ret;
}

// No update for a while: top up, run a deferred clean refresh, bring the planes back to the frame
// shown, or turn the analog supplies off
static void prv_idle_work(struct pbl_work *work) {
  struct pbl_display_ssd1677_state *state =
      PBL_CONTAINER_OF(work, struct pbl_display_ssd1677_state, idle.work);
  const struct pbl_display *disp = state->disp;
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);

  // An update holds it: its refresh starts the wait over at its end
  if (pbl_mutex_lock(&state->lock, PBL_NO_WAIT) != 0) {
#ifdef CONFIG_DISPLAY_UC8279_X4
    if (state->uc8279) {
      (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle, PBL_MSEC(100));
    }
#endif
    return;
  }

#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    uint32_t elapsed = (uint32_t)pbl_uptime_ms() - state->idle_since_ms;
    if (elapsed >= epd->power_off_ms) {
      if (uc8279_x4_power_off(epd) != 0) {
        state->need_half = true;
        (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle, PBL_MSEC(1000));
      }
    } else {
      (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle,
                                        PBL_MSEC(epd->power_off_ms - elapsed));
    }
    pbl_mutex_unlock(&state->lock);
    return;
  }
#endif
  // A refresh in flight starts the wait over at its end
  if (state->refreshing || state->post_pending || pbl_display_update_in_progress(disp) ||
      pbl_gpio_get(&epd->busy) != 0) {
    pbl_mutex_unlock(&state->lock);
    return;
  }

  if (epd->shadow == nullptr) {
    uint32_t lo;
    uint32_t hi;

    if (state->need_half || state->frame == nullptr) {
      prv_idle_power(epd);
    } else if (epd->full_on_idle && epd->full_interval != 0U &&
               state->updates >= epd->full_interval &&
               prv_frame_shown(epd, 0U, disp->props.height - 1U)) {
      state->updates = 0U;
      if (prv_write_half_nocopy(epd) != 0 || prv_refresh(disp, REFRESH_HALF, true, 0U) != 0) {
        state->need_half = true;
      }
    } else if (epd->touched_rows != nullptr && prv_rows_range(epd, epd->touched_rows, &lo, &hi)) {
      (void)prv_topup_nocopy(epd);
    } else {
      prv_idle_power(epd);
    }
    pbl_mutex_unlock(&state->lock);
    return;
  }

  if (epd->full_on_idle && !state->need_half && epd->full_interval != 0U &&
      state->updates >= epd->full_interval) {
    state->updates = 0U;
    if (prv_write_half(epd) != 0 || prv_refresh(disp, REFRESH_HALF, true, 0U) != 0) {
      state->need_half = true;
    }
  } else if (epd->touched != nullptr && state->dirty_valid && !state->need_half) {
    (void)prv_topup(epd);
  } else {
    // So the next update writes only its own rows
    if (state->stale_valid && !state->need_half) {
      const struct plane_src bw = {.frame = epd->shadow, .cells = true};

      state->stale_valid = false;
      if (prv_write_plane(epd, CMD_WRITE_RAM_RED, &state->stale, &bw) != 0 ||
          prv_write_plane(epd, CMD_WRITE_RAM_BW, &state->stale, &bw) != 0) {
        state->need_half = true;
      }
    }
    prv_idle_power(epd);
  }

  pbl_mutex_unlock(&state->lock);
}

static void prv_set_touched(const struct pbl_display_ssd1677 *epd, uint32_t y, uint32_t x) {
  epd->touched[y * ((epd->disp.props.width + 7U) / 8U) + x / 8U] |= (uint8_t)(1U << (x % 8U));
}

// Row y's detail from the update into cellbuf, and how many pixels have it
static uint32_t prv_detail_row(const struct pbl_display_ssd1677 *epd,
                               const struct pbl_display_detail *detail, uint32_t y) {
  uint32_t n = 0U;

  if (detail == nullptr || !detail->row(detail, (uint16_t)y, epd->cellbuf)) {
    return 0U;
  }
  for (uint32_t x = 0U; x < epd->disp.props.width; x++) {
    n += (epd->cellbuf[x] != 0U) ? 1U : 0U;
  }

  return n;
}

// The n cells of row y in cellbuf are those kept
static bool prv_cells_same(const struct pbl_display_ssd1677 *epd, uint32_t y, uint32_t n) {
  const struct pbl_display_ssd1677_cell *c = &epd->cells[epd->cell_start[y]];
  uint32_t kept = epd->cell_start[y + 1U] - epd->cell_start[y];

  if (n != kept) {
    return false;
  }
  for (uint32_t x = 0U; x < epd->disp.props.width && n != 0U; x++) {
    if (epd->cellbuf[x] != 0U) {
      if (c->x != x || c->levels != epd->cellbuf[x]) {
        return false;
      }
      c++;
      n--;
    }
  }

  return true;
}

/*
 * The update's detail of rows first..last replaces the kept one, delta cells more in all; rows
 * that do not fit get none. With mark, the pixels of either get the top up's drive, as changes;
 * after a large change, which got it at once, only the grey ones, which the old plane forced.
 */
static void prv_cells_update(const struct pbl_display_ssd1677 *epd,
                             const struct pbl_display_detail *detail, uint32_t first, uint32_t last,
                             int32_t delta, bool mark, bool large) {
  struct pbl_display_ssd1677_cell *cells = epd->cells;
  uint16_t *start = epd->cell_start;
  uint32_t height = epd->disp.props.height;
  uint32_t total = start[height];
  uint32_t at = start[first];
  uint32_t end = start[last + 1U];

  PBL_ASSERTN(last < height && at <= end && end <= total && total <= epd->cell_cap);
  for (uint32_t y = first; y <= last; y++) {
    bool grey = prv_row_bit(epd->grey_rows, y);

    for (uint32_t k = start[y]; mark && k < start[y + 1U]; k++) {
      if (!large || (grey && prv_has_grey(cells[k].levels))) {
        prv_set_touched(epd, y, cells[k].x);
      }
    }
    epd->grey_rows[y / 8U] &= (uint8_t)~(1U << (y % 8U));
  }

  uint32_t n = (uint32_t)((int32_t)(end - at) + delta);
  bool fit = total - (end - at) + n <= epd->cell_cap;

  if (!fit) {
    n = 0U;
  }
  memmove(&cells[at + n], &cells[end], (total - end) * sizeof(*cells));

  uint32_t k = at;

  for (uint32_t y = first; y <= last; y++) {
    start[y] = (uint16_t)k;
    if (!fit || prv_detail_row(epd, detail, y) == 0U) {
      continue;
    }
    for (uint32_t x = 0U; x < epd->disp.props.width && k < at + n; x++) {
      if (epd->cellbuf[x] != 0U) {
        cells[k++] = (struct pbl_display_ssd1677_cell){(uint8_t)x, epd->cellbuf[x]};
        if (mark && !large) {
          prv_set_touched(epd, y, x);
        }
      }
    }
  }

  // The detail changed while it was read: none is kept
  if (k != at + n) {
    memset(start, 0, (height + 1U) * sizeof(*start));
    return;
  }
  for (uint32_t y = last + 1U; y <= height; y++) {
    start[y] = (uint16_t)(start[y] + at + n - end);
  }
  PBL_ASSERTN(start[height] <= epd->cell_cap);
}

/*
 * The grey levels of the frame shown, in the rows that have some the pass has not drawn yet. What
 * idle work is due runs first, as it would draw over them. Returns 1 when there is nothing to do.
 */
static int prv_grey_pass(const struct pbl_display_ssd1677 *epd,
                         const struct pbl_display_buffer *buf, uint16_t y0, uint16_t y1) {
  struct pbl_display_ssd1677_state *state = epd->state;
  const struct pbl_display *disp = state->disp;
  uint32_t pitch = (disp->props.width + 7U) / 8U;
  int ret = 0;

  if (state->need_half) {
    return 1;
  }
  for (uint32_t y = y0; y <= y1; y++) {
    if (memcmp(&epd->shadow[y * pitch], &buf->data[y * buf->pitch], pitch) != 0) {
      return 1;
    }
  }

  if (epd->full_on_idle && epd->full_interval != 0U && state->updates >= epd->full_interval) {
    state->updates = 0U;
    ret = prv_write_half(epd);
    if (ret == 0) {
      ret = prv_refresh(disp, REFRESH_HALF, true, 0U);
    }
  } else if (epd->touched != nullptr && state->dirty_valid) {
    ret = prv_topup(epd);
  }
  (void)PBL_POLL_SLEEP_MS(!state->refreshing, 1U, REFRESH_TIMEOUT_MS);
  if (ret != 0 || state->need_half) {
    state->need_half = true;
    return (ret != 0) ? ret : 1;
  }

  int32_t lo = -1;
  int32_t hi = -1;

  for (uint32_t y = 0U; y < disp->props.height; y++) {
    if (prv_row_bit(epd->grey_rows, y)) {
      continue;
    }
    for (uint32_t k = epd->cell_start[y]; k < epd->cell_start[y + 1U]; k++) {
      if (prv_has_grey(epd->cells[k].levels)) {
        lo = (lo < 0) ? (int32_t)y : lo;
        hi = (int32_t)y;
        break;
      }
    }
  }
  if (lo < 0) {
    return 1;
  }

  // And wherever the planes may differ, which the refresh would drive
  struct pbl_display_ssd1677_window w = prv_window(epd, (uint16_t)lo, (uint16_t)hi);

  if (state->stale_valid) {
    prv_union(&w, &state->stale);
  }
  ret = prv_write_plane(
      epd, CMD_WRITE_RAM_RED, &w,
      &(struct plane_src){.frame = epd->shadow, .map = MAP_GREY_OLD, .cells = true});
  if (ret == 0) {
    ret = prv_write_plane(
        epd, CMD_WRITE_RAM_BW, &w,
        &(struct plane_src){.frame = epd->shadow, .map = MAP_GREY_NEW, .cells = true});
  }
  for (uint32_t y = (uint32_t)lo; y <= (uint32_t)hi; y++) {
    for (uint32_t k = epd->cell_start[y]; k < epd->cell_start[y + 1U]; k++) {
      if (prv_has_grey(epd->cells[k].levels)) {
        epd->grey_rows[y / 8U] |= (uint8_t)(1U << (y % 8U));
        break;
      }
    }
  }
  if (ret == 0) {
    ret = prv_refresh(disp, REFRESH_GREY, false, 0U);
  }
  if (ret != 0) {
    state->need_half = true;
    return ret;
  }

  // The planes hold grey classes there now
  state->stale = w;
  state->stale_valid = true;

  return 0;
}

/*
 * Which plane holds what after a fast refresh is not relied on: the controller may swap them
 * (seen on the panel) or not. A fast update writes both over its own rows and the window where
 * the last refresh left them different; elsewhere both hold the frame shown. A half one writes
 * both whole.
 */
static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t pitch = (disp->props.width + 7U) / 8U;
  struct pbl_display_ssd1677_window w = {0};
  struct pbl_display_ssd1677_window bw = {0};
  enum refresh_mode mode;
  int ret = 0;

  (void)pbl_mutex_lock(&state->lock, PBL_FOREVER);

#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    struct pbl_display_buffer next = *buf;
    if (buf->detail && buf->detail->grey) {
      enum uc8279_grey_action action;
      ret = uc8279_x4_prepare_grey(epd, buf, &action);
      if (ret != 0 || action == UC8279_GREY_SKIP) {
        pbl_mutex_unlock(&state->lock);
        if (ret == 0) {
          pbl_display_complete(disp, 0);
        }
        return ret;
      }
      if (action == UC8279_GREY_CLEAR) {
        next.detail = nullptr;
      }
    }
    uc8279_x4_submit(epd, &next, y0, y1);
    pbl_mutex_unlock(&state->lock);
    return 0;
  }
#endif

  // The driver's own refresh, a top up or a clean one, ends first, and its re-sync
  if (PBL_POLL_SLEEP_MS(!state->refreshing && !state->post_pending, 1U, WAIT_IDLE_TIMEOUT_MS) !=
      0) {
    pbl_mutex_unlock(&state->lock);
    return -ETIMEDOUT;
  }

  // Still busy with a refresh that timed out: start the controller over, and its planes with it
  if (pbl_gpio_get(&epd->busy) != 0) {
    ret = prv_controller_init(epd);
    state->need_half = true;
    state->powered = false;
    state->stale_valid = false;
    if (ret != 0) {
      pbl_mutex_unlock(&state->lock);
      return ret;
    }
  }

  if (epd->shadow == nullptr) {
    // Grey is drawn by the mapper of a panel at the framebuffer's size, mounted a quarter turn
    if (buf->detail != nullptr && buf->detail->grey) {
      ret = (epd->quarter && epd->scale == 1U) ? prv_grey_nocopy(epd, buf, y0, y1) : 1;
    } else {
      ret = prv_update_nocopy(epd, buf, y0, y1);
    }
    pbl_mutex_unlock(&state->lock);
    if (ret > 0) {
      pbl_display_complete(disp, 0);
      ret = 0;
    }
    return ret;
  }

  const struct pbl_display_detail *detail = (epd->cells != nullptr) ? buf->detail : nullptr;

  if (detail != nullptr && detail->grey) {
    ret = prv_grey_pass(epd, buf, y0, y1);
    pbl_mutex_unlock(&state->lock);
    if (ret > 0) {
      pbl_display_complete(disp, 0);
      ret = 0;
    }
    return ret;
  }

  // Only the rows that differ from the shadow, or in their detail: an unchanged frame needs no
  // refresh, unless what the panel shows is not known
  int32_t first = -1;
  int32_t last = -1;
  int32_t delta = 0;

  for (uint32_t y = y0; y <= y1; y++) {
    bool differ = memcmp(&epd->shadow[y * pitch], &buf->data[y * buf->pitch], pitch) != 0;

    if (epd->cells != nullptr) {
      uint32_t n = prv_detail_row(epd, detail, y);

      delta += (int32_t)n - (int32_t)(epd->cell_start[y + 1U] - epd->cell_start[y]);
      differ = !prv_cells_same(epd, y, n) || differ;
    }
    if (differ) {
      first = (first < 0) ? (int32_t)y : first;
      last = (int32_t)y;
    }
  }

  if (first < 0 && !state->need_half) {
    pbl_mutex_unlock(&state->lock);
    pbl_display_complete(disp, 0);
    return 0;
  }

  mode = prv_mode(
      epd, first >= 0 && (uint32_t)(last - first + 1) * 2U < epd->disp.props.height);

  // A change over half the frame or more, as an app switch, gets the top up's drive at once, so
  // large fields come out as dark as the ones the top up finishes; smaller ones are left for it
  bool large = epd->touched != nullptr && mode == REFRESH_FAST &&
               (uint32_t)(last - first + 1) * 2U >= disp->props.height;

  // Both planes over the changed rows and wherever they may differ: the old one gets the frame
  // shown, from the shadow before the new rows go in, the new one the new frame
  if (mode == REFRESH_FAST) {
    w = prv_window(epd, (uint16_t)first, (uint16_t)last);
    bw = w;
    if (state->stale_valid) {
      prv_union(&bw, &state->stale);
    }
    state->stale_valid = false;
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &bw,
                          &(struct plane_src){
                            .frame = epd->shadow,
                            .map = MAP_OLD,
                            .cells = true,
                            .force_lo = (uint16_t)PBL_MAX(first, 0),
                            .force_hi = (uint16_t)PBL_MAX(last, 0)
                          });
  }

  for (int32_t y = first; y >= 0 && y <= last; y++) {
    const uint8_t *src = &buf->data[(uint32_t)y * buf->pitch];
    uint8_t *dst = &epd->shadow[(uint32_t)y * pitch];

    if (epd->touched != nullptr && mode == REFRESH_FAST) {
      uint8_t *t = &epd->touched[(uint32_t)y * pitch];

      for (uint32_t i = 0U; i < pitch; i++) {
        uint8_t changed = dst[i] ^ src[i];

        t[i] = large ? (uint8_t)(t[i] & ~changed) : (uint8_t)(t[i] | changed);
      }
    }
    memcpy(dst, src, pitch);
  }
  if (epd->cells != nullptr && first >= 0) {
    prv_cells_update(epd, detail, (uint32_t)first, (uint32_t)last, delta,
                     epd->touched != nullptr && mode == REFRESH_FAST, large);
  }

  if (mode == REFRESH_HALF) {
    ret = prv_write_half(epd);
  } else if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &bw,
                          &(struct plane_src){.frame = epd->shadow, .cells = true});
  }
  if (ret == 0) {
    ret = prv_refresh(disp, mode, false, large ? state->wave.topup_frames : 0U);
  }
  if (ret != 0) {
    state->need_half = true;
  } else if (mode == REFRESH_FAST) {
    state->stale = w;
    state->stale_valid = true;
    if (state->dirty_valid) {
      prv_union(&state->dirty, &w);
    } else {
      state->dirty = w;
      state->dirty_valid = true;
    }
  }

  pbl_mutex_unlock(&state->lock);

  return ret;
}

#ifdef CONFIG_DISPLAY_SSD1677_GREY
void pbl_display_ssd1677_grey_tune(uint8_t light, uint8_t dark, uint8_t frame_rate, uint8_t tail) {
  s_grey_tail = tail;
  s_grey_light = (uint8_t)PBL_MIN(light, 40U);
  s_grey_dark = (uint8_t)PBL_MIN(dark, 40U);
  s_grey_frame_rate = frame_rate;
}

int pbl_display_ssd1677_grey(const struct pbl_display *disp, const uint8_t *old_frame,
                             const uint8_t *new_frame, uint8_t vcom) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  struct pbl_display_ssd1677_window w = prv_window(epd, 0U, disp->props.height - 1U);
  int ret;

#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    return -ENOTSUP;
  }
#endif
  (void)pbl_mutex_lock(&state->lock, PBL_FOREVER);
  (void)PBL_POLL_SLEEP_MS(!state->refreshing && pbl_gpio_get(&epd->busy) == 0, 1U,
                          REFRESH_TIMEOUT_MS);

  ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, &(struct plane_src){.frame = old_frame});
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, &(struct plane_src){.frame = new_frame});
  }
  if (ret == 0) {
    ret = prv_load_grey(epd, vcom, 2U);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_UPDATE_CTRL1, CTRL1_NORMAL);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_UPDATE_CTRL2, SEQ_CLOCK_ANALOG | SEQ_CUSTOM_LUT);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_ACTIVATE, nullptr, 0U);
  }
  if (ret == 0) {
    pbl_sleep_ms(RESET_MS);
    ret = PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 0, 1U, REFRESH_TIMEOUT_MS);
  }

  // The planes hold grey classes, not the frame shown: the next update starts over clean
  state->powered = true;
  state->need_half = true;
  state->stale_valid = false;
  pbl_mutex_unlock(&state->lock);

  return ret;
}
#endif

const struct pbl_display_ops pbl_display_ssd1677_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_set_rotated,
  .update = prv_update,
};

#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>

// Timing and tuning of the first SSD1677, for measurements on the panel
static const struct pbl_display_ssd1677 *prv_first(void) {
  const struct pbl_device *dev;

  PBL_DEVICE_CLASS_FOREACH(&pbl_display_class, dev) {
    const struct pbl_display *disp = PBL_CONTAINER_OF(dev, const struct pbl_display, dev);

    if (disp->ops == &pbl_display_ssd1677_ops) {
      return prv_epd(disp);
    }
  }

  return nullptr;
}

static int prv_cmd_stat(const struct pbl_shell *sh, size_t argc, char **argv) {
  const struct pbl_display_ssd1677 *epd = prv_first();

  if (epd == nullptr) {
    return -ENODEV;
  }
#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    pbl_mutex_lock(&epd->state->lock, PBL_FOREVER);
  }
  for (unsigned i = 0; i < 2; i++) {
    const uint8_t *p = epd->state->panel_id[i];
    pbl_shell_print(sh, "display FLG %02x VER %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3],
                    p[4], p[5]);
  }
#endif
  pbl_shell_print(sh, "last update: write %u ms, refresh %u ms, re-sync %u ms",
                  epd->state->last_write_ms, epd->state->last_refresh_ms, epd->state->last_post_ms);
#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    pbl_shell_print(sh, "last drive window %ux%u", epd->state->last_window_width,
                    epd->state->last_window_height);
    static const char *const modes[] = {
      "none", "full monochrome OTP", "partial monochrome OTP", "four-level grayscale",
      "gray-to-monochrome transition", "monochrome motion"
    };
    pbl_shell_print(sh, "UC8279 %s; %u completed drives, panel power %s",
                    modes[epd->state->last_drive], (unsigned)epd->state->completed_drives,
                    epd->state->powered ? "on" : "off");
    pbl_shell_print(sh, "%u full monochrome drives", (unsigned)epd->state->full_mono_drives);
    pbl_shell_print(sh, "%u motion drives", (unsigned)epd->state->motion_drives);
    pbl_shell_print(sh, "sparse updates %u, regions %u, bytes saved across both planes %u",
                    (unsigned)epd->state->sparse_updates, (unsigned)epd->state->sparse_regions,
                    (unsigned)epd->state->sparse_bytes_saved);
    if (epd->state->measured_motion_drives) {
      pbl_shell_print(sh, "motion trigger to BUSY release: %u ms across %u drives, minimum %u ms",
                      (unsigned)epd->state->motion_busy_ms,
                      (unsigned)epd->state->measured_motion_drives,
                      (unsigned)epd->state->motion_min_busy_ms);
    }
    pbl_mutex_unlock(&epd->state->lock);
    return 0;
  }
#endif
  if (argc == 2 && strcmp(argv[1], "reset") != 0) {
    return -EINVAL;
  }
  pbl_irq_lock();
  if (argc == 2) {
    epd->state->completed_updates = 0;
    epd->state->first_update_ms = 0;
    epd->state->last_update_ms = 0;
  }
  uint32_t completed = epd->state->completed_updates;
  uint32_t elapsed = epd->state->last_update_ms - epd->state->first_update_ms;
  pbl_irq_unlock();
  pbl_shell_print(sh, "%u completed updates, first to last %u ms", (unsigned)completed,
                  (unsigned)elapsed);
  pbl_shell_print(sh, "fast %u frames, large +%u, top up %u, frame rate 0x%02x, motion %u",
                  epd->state->wave.phases[1], epd->state->wave.large_frames,
                  epd->state->wave.topup_frames, epd->state->wave.frame_rate,
                  epd->state->motion_frames);

  return 0;
}

// Short differential drives apply only to consecutive small changes.
static int prv_cmd_motion(const struct pbl_shell *sh, size_t argc, char **argv) {
  const struct pbl_display_ssd1677 *epd = prv_first();
  unsigned long frames;
  if (epd == nullptr || !epd->fast_differential) {
    return -ENOTSUP;
  }
#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    return -ENOTSUP;
  }
#endif
  if (pbl_shell_strtoul(argv[1], &frames) != 0 || frames > 2U) {
    return -EINVAL;
  }
  (void)pbl_mutex_lock(&epd->state->lock, PBL_FOREVER);
  if (frames != 0U && (epd->state->wave.frame_rate != 0x22U || !epd->full_on_idle ||
                       epd->state->wave.topup_frames == 0U)) {
    pbl_mutex_unlock(&epd->state->lock);
    return -ENOTSUP;
  }
  epd->state->motion_frames = (uint8_t)frames;
  epd->state->short_drive = false;
  pbl_mutex_unlock(&epd->state->lock);
  pbl_shell_print(sh, "Motion frames %u (0 uses the normal waveform)", (unsigned)frames);
  return 0;
}

// tune <fast frames> <large extra frames> <top up frames> [frame rate code]
static int prv_cmd_tune(const struct pbl_shell *sh, size_t argc, char **argv) {
  const struct pbl_display_ssd1677 *epd = prv_first();
  unsigned long v[4];

  if (epd == nullptr) {
    return -ENODEV;
  }
#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    pbl_shell_error(sh, "UC8279 uses its built-in monochrome waveforms");
    return -ENOTSUP;
  }
#endif
  for (size_t i = 1U; i < argc; i++) {
    if (pbl_shell_strtoul(argv[i], &v[i - 1U]) != 0 || v[i - 1U] > 255U) {
      pbl_shell_error(sh, "invalid '%s'", argv[i]);
      return -EINVAL;
    }
  }
  (void)pbl_mutex_lock(&epd->state->lock, PBL_FOREVER);
  epd->state->motion_frames = 0U;
  epd->state->wave.phases[1] = (uint8_t)v[0];
  epd->state->wave.large_frames = (uint8_t)v[1];
  epd->state->wave.topup_frames = (uint8_t)v[2];
  if (argc > 4U) {
    epd->state->wave.frame_rate = (uint8_t)v[3];
  }
  pbl_mutex_unlock(&epd->state->lock);

  return prv_cmd_stat(sh, 0U, nullptr);
}

// resync <0 both | 1 old plane only | 2 new plane only>, to find which the controller keeps
static int prv_cmd_resync(const struct pbl_shell *sh, size_t argc, char **argv) {
  const struct pbl_display_ssd1677 *epd = prv_first();
  unsigned long v;

  if (epd == nullptr) {
    return -ENODEV;
  }
#ifdef CONFIG_DISPLAY_UC8279_X4
  if (epd->state->uc8279) {
    pbl_shell_error(sh, "UC8279 uses its built-in monochrome waveforms");
    return -ENOTSUP;
  }
#endif
  if (pbl_shell_strtoul(argv[1], &v) != 0 || v > 2U) {
    pbl_shell_error(sh, "0, 1 or 2");
    return -EINVAL;
  }
  epd->state->resync_planes = (uint8_t)v;

  return 0;
}

PBL_SHELL_SUBCMD_SET_CREATE(sub_epd);
PBL_SHELL_CMD_REGISTER(epd, sub_epd, "E-paper controller timing and waveform", nullptr);
PBL_SHELL_SUBCMD_ADD(sub_epd, motion, nullptr, "Small animation drive frames: 0 normal, 1 or 2 fast",
                    prv_cmd_motion, 2, 0);
PBL_SHELL_SUBCMD_ADD(sub_epd, resync, nullptr, "Planes re-synced: 0 both, 1 old, 2 new",
                     prv_cmd_resync, 2, 0);
PBL_SHELL_SUBCMD_ADD(sub_epd, stat, nullptr, "Update timings and completed drives [reset]",
                    prv_cmd_stat, 1, 1);
PBL_SHELL_SUBCMD_ADD(sub_epd, tune, nullptr,
                     "Set <fast> <large extra> <top up> frames [frame rate]", prv_cmd_tune, 4, 1);
#endif

#include PBL_DT_INSTANCES(solomon_ssd1677)
