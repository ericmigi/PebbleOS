/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "geometry.h"

#include <errno.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/display/ssd1677.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
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

enum refresh_mode {
  REFRESH_HALF,
  REFRESH_FAST,
  REFRESH_TOPUP,
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

// One controller row's bytes x0..x0+len-1 from a frame; across is its framebuffer row, or column
// mounted a quarter turn, -1 on the border. The frame's pixels lo..hi along the row are unpacked
// into line first, one byte each: a scale-up reads each of them several times
static void prv_map_row(const struct pbl_display_ssd1677 *epd, const uint8_t *frame, int32_t across,
                        uint32_t x0, uint32_t len, uint32_t lo, uint32_t hi, uint8_t *out) {
  uint32_t pitch = (epd->disp.props.width + 7U) / 8U;
  uint8_t *line = epd->line;

  if (across < 0) {
    memset(out, 0xFF, len);
    return;
  }

  if (epd->quarter) {
    const uint8_t *src = &frame[(uint32_t)across / 8U];
    uint32_t shift = (uint32_t)across % 8U;

    for (uint32_t a = lo; a <= hi; a++) {
      line[a] = (src[a * pitch] >> shift) & 1U;
    }
  } else {
    const uint8_t *src = &frame[(uint32_t)across * pitch];

    for (uint32_t a = lo; a <= hi; a++) {
      line[a] = (src[a / 8U] >> (a % 8U)) & 1U;
    }
  }

  // RAM bytes are most significant bit first, the framebuffer's least; 1 is white. The border is
  // white: line[-1] is not read, a negative coordinate picks 1 instead
  const int16_t *along = &epd->along[x0 * 8U];

  for (uint32_t i = 0U; i < len; i++, along += 8) {
#define PIX(b) ((along[b] < 0) ? 1U : (uint32_t)line[along[b]])
    out[i] = (uint8_t)((PIX(0) << 7) | (PIX(1) << 6) | (PIX(2) << 5) | (PIX(3) << 4) |
                       (PIX(4) << 3) | (PIX(5) << 2) | (PIX(6) << 1) | PIX(7));
#undef PIX
  }
}

// A window of a frame, scaled and turned, to a RAM plane; rows a scale-up repeats are mapped once,
// and several go out per transfer
static int prv_write_plane(const struct pbl_display_ssd1677 *epd, uint8_t ram,
                           const struct pbl_display_ssd1677_window *w, const uint8_t *frame) {
  struct display_geometry g = prv_geometry(epd);
  bool flip = epd->state->flip;
  uint32_t len = w->x1 - w->x0 + 1U;
  uint32_t cap = display_geometry_scan_width(&g) / 8U * SSD1677_ROWS_PER_WRITE;
  uint32_t fill = 0U;
  int32_t prev = INT32_MIN;
  int ret;

  if (w->x0 > w->x1 || w->y0 > w->y1) {
    return 0;
  }

  // The frame's pixels the window's columns show
  uint32_t lo = UINT32_MAX;
  uint32_t hi = 0U;

  for (uint32_t cx = w->x0 * 8U; cx < (w->x1 + 1U) * 8U; cx++) {
    if (epd->along[cx] >= 0) {
      lo = PBL_MIN(lo, (uint32_t)epd->along[cx]);
      hi = PBL_MAX(hi, (uint32_t)epd->along[cx]);
    }
  }

  ret = prv_ram_window(epd, w);
  if (ret == 0) {
    ret = prv_cmd(epd, ram, nullptr, 0U);
  }

  for (uint32_t cy = w->y0; ret == 0 && cy <= w->y1; cy++) {
    int32_t across = display_geometry_across(&g, flip, cy);
    uint8_t *row = &epd->row[fill];

    if (fill != 0U && across == prev) {
      memcpy(row, row - len, len);
    } else {
      prv_map_row(epd, frame, (lo <= hi) ? across : -1, w->x0, len, lo, hi, row);
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

static bool prv_has_idle_work(const struct pbl_display_ssd1677 *epd) {
  return epd->touched != nullptr || epd->full_on_idle || epd->power_off_ms != 0U;
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

  state->disp = disp;
  state->refreshing = false;
  state->own = false;
  state->dirty_valid = false;
  pbl_mutex_init(&state->lock);
  pbl_delayed_work_init(&state->idle, prv_idle_work);
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
  state->need_half = true;
  state->powered = false;
  state->stale_valid = false;
  state->updates = 0U;
  pbl_timer_init(&state->timer, prv_timeout);

  ret = pbl_intc_request(&state->busy_irq, prv_busy_isr, (void *)disp);
  if (ret != 0) {
    return ret;
  }

  // Unknown until the first update: the frame is white, as the RAM
  memset(epd->shadow, 0xFF, (disp->props.width + 7U) / 8U * disp->props.height);

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
  if (epd->state->refreshing) {
    pbl_mutex_unlock(&epd->state->lock);
    return -EBUSY;
  }

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

  epd->state->need_half = true;
  epd->state->powered = false;
  pbl_mutex_unlock(&epd->state->lock);

  return ret;
}

static int prv_set_rotated(const struct pbl_display *disp, bool rotated) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct display_geometry g = prv_geometry(epd);

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

static enum refresh_mode prv_mode(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *state = epd->state;

  if (state->need_half) {
    state->need_half = false;
    state->updates = 0U;
    return REFRESH_HALF;
  }

  // The periodic clean refresh is the half one too, the stock firmware's only one: the OTP full
  // waveform (22=F7) takes 3.7 s here
  // On idle if asked, unless that never comes
  if (epd->full_interval != 0U && ++state->updates >= epd->full_interval &&
      (!epd->full_on_idle || state->updates >= 2U * epd->full_interval)) {
    state->updates = 0U;
    return REFRESH_HALF;
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
    ret = prv_cmd1(epd, CMD_TEMP_WRITE, HALF_TEMP);
  }
  // A half refresh loads the OTP waveforms over the LUT, so it goes in before every fast one
  if (ret == 0 && mode == REFRESH_FAST) {
    seq = SEQ_FAST;
    if (state->wave.phases[1] != 0U) {
      uint8_t phases[4];

      memcpy(phases, state->wave.phases, sizeof(phases));
      phases[1] = (uint8_t)PBL_MIN(phases[1] + boost, UINT8_MAX);
      seq = state->powered ? SEQ_CUSTOM_LUT : (SEQ_CLOCK_ANALOG | SEQ_CUSTOM_LUT);
      ret = prv_load_lut(epd, phases, state->wave.vs);
    }
  }
  if (ret == 0 && mode == REFRESH_TOPUP) {
    const uint8_t phases[4] = {0U, state->wave.topup_frames, 0U, 0U};

    seq = state->powered ? SEQ_CUSTOM_LUT : (SEQ_CLOCK_ANALOG | SEQ_CUSTOM_LUT);
    ret = prv_load_lut(epd, phases, s_diff_vs);
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

  state->stale_valid = false;
  ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, epd->shadow);
  // The half refresh is not blind to the old plane, whatever 0x21 says: without the new image
  // there too, what it turns black greys out at the next fast refresh
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, epd->shadow);
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
  uint32_t size = prv_frame_size(epd);
  int ret = 0;

  for (uint32_t i = 0U; i < size; i++) {
    epd->touched[i] ^= epd->shadow[i];
  }

  struct pbl_display_ssd1677_window u = state->dirty;

  if (state->stale_valid) {
    prv_union(&u, &state->stale);
  }
  ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &u, epd->shadow);
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &u, epd->touched);
  }
  memset(epd->touched, 0, size);
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

// No update for a while: top up, run a deferred clean refresh, or turn the analog supplies off
static void prv_idle_work(struct pbl_work *work) {
  struct pbl_display_ssd1677_state *state =
      PBL_CONTAINER_OF(work, struct pbl_display_ssd1677_state, idle.work);
  const struct pbl_display *disp = state->disp;
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  uint32_t idle;

  // An update holds it: its refresh starts the wait over at its end
  if (pbl_mutex_lock(&state->lock, PBL_NO_WAIT) != 0) {
    return;
  }

  // A refresh in flight starts the wait over at its end
  if (state->refreshing || pbl_display_update_in_progress(disp) || pbl_gpio_get(&epd->busy) != 0) {
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
  } else if (epd->power_off_ms != 0U && state->powered) {
    idle = (uint32_t)pbl_uptime_ms() - state->idle_since_ms;
    if (idle >= epd->power_off_ms) {
      if (prv_power_off(epd) != 0) {
        state->need_half = true;
      }
    } else {
      (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle,
                                        PBL_MSEC(epd->power_off_ms - idle));
    }
  }

  pbl_mutex_unlock(&state->lock);
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

  // The driver's own refresh, a top up or a clean one, ends first
  (void)PBL_POLL_SLEEP_MS(!state->refreshing, 1U, REFRESH_TIMEOUT_MS);

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

  // Only the rows that differ from the shadow: an unchanged frame needs no refresh, unless what
  // the panel shows is not known
  int32_t first = -1;
  int32_t last = -1;

  for (uint32_t y = y0; y <= y1; y++) {
    if (memcmp(&epd->shadow[y * pitch], &buf->data[y * buf->pitch], pitch) != 0) {
      first = (first < 0) ? (int32_t)y : first;
      last = (int32_t)y;
    }
  }

  if (first < 0 && !state->need_half) {
    pbl_mutex_unlock(&state->lock);
    pbl_display_complete(disp, 0);
    return 0;
  }

  mode = prv_mode(epd);

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
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &bw, epd->shadow);
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

  if (mode == REFRESH_HALF) {
    ret = prv_write_half(epd);
  } else if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &bw, epd->shadow);
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

int pbl_display_ssd1677_grey(const struct pbl_display *disp, const uint8_t *old_frame,
                             const uint8_t *new_frame, uint8_t vcom) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  struct pbl_display_ssd1677_window w = prv_window(epd, 0U, disp->props.height - 1U);
  uint8_t lut[LUT_LEN] = {0};
  int ret;

  (void)pbl_mutex_lock(&state->lock, PBL_FOREVER);
  (void)PBL_POLL_SLEEP_MS(!state->refreshing && pbl_gpio_get(&epd->busy) == 0, 1U,
                          REFRESH_TIMEOUT_MS);

  for (uint32_t c = 0U; c < 4U; c++) {
    for (uint32_t g = 0U; g < 3U; g++) {
      lut[c * 10U + g] = s_grey_vs[c][g];
    }
  }
  for (uint32_t g = 0U; g < 3U; g++) {
    memset(&lut[50U + g * 5U], 1, 4U);
  }
  memset(&lut[100], GREY_FRAME_RATE, 5U);

  ret = prv_write_plane(epd, CMD_WRITE_RAM_RED, &w, old_frame);
  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_BW, &w, new_frame);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_WRITE_LUT, lut, sizeof(lut));
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_GATE_VOLTAGE, GATE_VOLTAGE);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_SOURCE_VOLTAGE, state->wave.source, sizeof(state->wave.source));
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_VCOM, vcom);
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

#include PBL_DT_INSTANCES(solomon_ssd1677)
