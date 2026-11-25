/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "uc8279_x4.h"

#include "geometry.h"

#include <errno.h>
#include <string.h>

#include <pbl/crc/crc.h>
#include <pbl/drivers/pinctrl.h>
#include <pbl/kernel/delay.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

static void prv_probe_reg(const struct pbl_display_ssd1677 *epd, uint8_t cmd, uint8_t *data,
                          size_t count) {
  const struct pbl_gpio *sda = &epd->probe_data;
  const struct pbl_gpio *clk = &epd->probe_clock;
  const struct pbl_gpio *cs = &epd->spi.bus->cs_gpios[epd->spi.cs];

  (void)pbl_gpio_configure(clk, PBL_GPIO_OUTPUT_LOW);
  (void)pbl_gpio_configure(sda, PBL_GPIO_OUTPUT_LOW);
  (void)pbl_gpio_set(&epd->dc, false);
  (void)pbl_gpio_set(cs, true);
  for (unsigned i = 0; i < 8; i++, cmd <<= 1) {
    (void)pbl_gpio_set(sda, (cmd & 0x80U) != 0U);
    pbl_busy_wait_us(1);
    (void)pbl_gpio_set(clk, true);
    pbl_busy_wait_us(1);
    (void)pbl_gpio_set(clk, false);
  }
  (void)pbl_gpio_set(&epd->dc, true);
  (void)pbl_gpio_configure(sda, PBL_GPIO_INPUT);
  for (size_t i = 0; i < count; i++) {
    uint8_t value = 0;
    for (unsigned j = 0; j < 8; j++) {
      pbl_busy_wait_us(1);
      value = (value << 1) | (pbl_gpio_get(sda) > 0);
      (void)pbl_gpio_set(clk, true);
      pbl_busy_wait_us(1);
      (void)pbl_gpio_set(clk, false);
    }
    data[i] = value;
  }
  (void)pbl_gpio_set(cs, false);
}

int uc8279_x4_probe(const struct pbl_display_ssd1677 *epd) {
  if (!pbl_gpio_is_connected(&epd->probe_data)) {
    return 0;
  }
  if (!pbl_gpio_is_connected(&epd->probe_clock) || epd->probe_restore == nullptr) {
    return -EINVAL;
  }
  const struct pbl_pinctrl pins = PBL_PINCTRL(epd->probe_restore, nullptr, 0);

  (void)pbl_sem_take(&epd->spi.bus->state->lock, PBL_FOREVER);
  for (unsigned i = 0; i < 2; i++) {
    (void)pbl_gpio_set(&epd->reset, true);
    pbl_sleep_ms(i == 0 ? 1 : 50);
    (void)pbl_gpio_set(&epd->reset, false);
    pbl_sleep_ms(30);
    prv_probe_reg(epd, 0x71, &epd->state->panel_id[i][0], 1);
    prv_probe_reg(epd, 0x70, &epd->state->panel_id[i][1], 5);
  }
  const uint8_t *first = epd->state->panel_id[0];
  const uint8_t *second = epd->state->panel_id[1];
  uint8_t id = first[3];
  bool known = id == 2 || id == 3 || id == 0x41 || id == 0x42 || (id >= 0x67 && id <= 0x69);
  bool driven = (first[0] & 1U) && first[0] != 0xff && memcmp(first + 1, first + 2, 4) != 0;
  int ret = pbl_pinctrl_select_state(&pins, PBL_PINCTRL_STATE_DEFAULT);
  pbl_sem_give(&epd->spi.bus->state->lock);
  if (ret != 0) {
    return ret;
  }
  if (!driven) {
    return 0;
  }
  if (memcmp(first, second, 6) != 0) {
    return -EIO;
  }
  if (!known || !epd->quarter || epd->panel_height != 800 || epd->panel_width != 480) {
    return -ENOTSUP;
  }
  epd->state->uc8279 = true;
  return 0;
}

struct uc_command {
  uint8_t command;
  uint8_t length;
  uint8_t data[9];
};

static int prv_uc_command_list(const struct pbl_display_ssd1677 *epd, const struct uc_command *list,
                               size_t count) {
  for (size_t i = 0; i < count; i++) {
    int ret = epaper_write_command(epd, list[i].command, list[i].data, list[i].length);
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}

static int prv_update_bw(const struct pbl_display_ssd1677 *epd,
                         const struct pbl_display_buffer *buf, uint16_t y0, uint16_t y1);

static void prv_panel_thread(void *arg) {
  const struct pbl_display_ssd1677 *epd = arg;
  struct pbl_display_ssd1677_state *state = epd->state;
  for (;;) {
    (void)pbl_sem_take(&state->frame_ready, PBL_FOREVER);
    (void)pbl_mutex_lock(&state->lock, PBL_FOREVER);
    int ret = state->next_grey
                  ? uc8279_x4_finish_grey(epd, &state->next_frame)
                  : prv_update_bw(epd, &state->next_frame, state->next_y0, state->next_y1);
    pbl_mutex_unlock(&state->lock);
    pbl_display_complete(&epd->disp, ret);
  }
}

int uc8279_x4_init(const struct pbl_display_ssd1677 *epd) {
  static const struct uc_command init[] = {
    {0x00, 2, {0x37, 0x4d}},             // Panel setting
    {0x61, 4, {0x03, 0x20, 0x02, 0x58}}, // Resolution: 800x600
    {0x65, 4, {0, 0, 0, 0}},             // Gate/source start
    {0x03, 1, {0x20}},                   // Power-off sequence
    {0x30, 1, {0x0e}},                   // PLL
    {0xe1, 1, {0x02}},                   // Gate scan
  };
  int ret = prv_uc_command_list(epd, init, PBL_ARRAY_SIZE(init));
  if (ret != 0) {
    return ret;
  }
  struct pbl_display_ssd1677_state *state = epd->state;
  pbl_sem_init(&state->frame_ready, 0, 1);
  const struct pbl_thread_attr attr = {
    .name = "UC8279",
    .entry = prv_panel_thread,
    .arg = (void *)epd,
    // A runnable app can outrank the system work queue for its entire animation.
    .prio = CONFIG_KERNEL_MAIN_THREAD_PRIO,
    .privileged = true,
    .stack = state->panel_stack,
    .stack_size = sizeof(state->panel_stack),
  };
  return pbl_thread_create(&state->panel_thread, &attr);
}

static int prv_uc_wait(const struct pbl_display_ssd1677 *epd) {
  pbl_sleep_ms(2);
  return PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 1, 1, 5000U);
}

int uc8279_x4_power_off(const struct pbl_display_ssd1677 *epd) {
  if (!epd->state->powered && !epd->state->power_off_pending) {
    return 0;
  }
  int ret = epaper_write_command(epd, 0x02, nullptr, 0);
  if (ret == 0) {
    ret = prv_uc_wait(epd);
  }
  epd->state->powered = false;
  epd->state->power_off_pending = ret != 0;
  return ret;
}

// Hash the tile in one call. Gather short rows separately so the common four-byte
// case is a fixed-size copy even when the packed framebuffer pitch is unaligned.
static uint32_t prv_tile_crc(const uint8_t *data, unsigned pitch, unsigned bytes, unsigned rows) {
  uint8_t tile[32 * 4];
  if (bytes == 4) {
    for (unsigned r = 0; r < rows; ++r) {
      memcpy(tile + 4 * r, data + r * pitch, 4);
    }
  } else {
    for (unsigned r = 0; r < rows; ++r) {
      memcpy(tile + bytes * r, data + r * pitch, bytes);
    }
  }
#ifdef CONFIG_SOC_ESP32S3
  // esp32s3.rom.ld: crc32_le, also exported as esp_rom_crc32_le.
  return ((uint32_t (*)(uint32_t, const uint8_t *, uint32_t))0x40001c98U)(0, tile, bytes * rows);
#else
  return pbl_crc32(0, tile, bytes * rows);
#endif
}

// Keep a bounded list for sparse RAM writes. The waveform always uses the union.
struct uc_dirty_regions {
  struct pbl_display_ssd1677_window rects[12];
  unsigned count;
#ifdef CONFIG_SHELL
  unsigned saved_per_plane;
#endif
};

static struct pbl_display_ssd1677_window prv_scan_rect(const struct pbl_display_ssd1677 *epd,
                                                       unsigned left, unsigned top, unsigned right,
                                                       unsigned bottom) {
  struct display_geometry g = {
    epd->disp.props.width, epd->disp.props.height, epd->panel_width, epd->panel_height, epd->scale,
    epd->quarter
  };
  uint32_t x0, y0, x1, y1;
  display_geometry_scan_pos(&g, epd->state->flip, left, top, 0, 0, &x0, &y0);
  display_geometry_scan_pos(&g, epd->state->flip, right, bottom, epd->scale - 1U, epd->scale - 1U,
                            &x1, &y1);
  return (struct pbl_display_ssd1677_window){
    .x0 = PBL_MIN(x0, x1) / 8U,
    .x1 = PBL_MAX(x0, x1) / 8U,
    .y0 = PBL_MIN(y0, y1),
    .y1 = PBL_MAX(y0, y1),
  };
}

// Consecutive tile strips with identical coverage can share one RAM address setup.
static void prv_add_region(struct uc_dirty_regions *regions, struct pbl_display_ssd1677_window w) {
  if (regions->count > PBL_ARRAY_SIZE(regions->rects)) {
    return;
  }
  for (unsigned i = 0; i < regions->count; ++i) {
    struct pbl_display_ssd1677_window *r = &regions->rects[i];
    if (r->x0 == w.x0 && r->x1 == w.x1 && (r->y1 + 1 == w.y0 || w.y1 + 1 == r->y0)) {
      r->y0 = PBL_MIN(r->y0, w.y0);
      r->y1 = PBL_MAX(r->y1, w.y1);
      return;
    }
    if (r->y0 == w.y0 && r->y1 == w.y1 && (r->x1 + 1 == w.x0 || w.x1 + 1 == r->x0)) {
      r->x0 = PBL_MIN(r->x0, w.x0);
      r->x1 = PBL_MAX(r->x1, w.x1);
      return;
    }
  }
  if (regions->count < PBL_ARRAY_SIZE(regions->rects)) {
    regions->rects[regions->count] = w;
  }
  ++regions->count;
}

static unsigned prv_window_bytes(const struct pbl_display_ssd1677_window *w) {
  return (w->x1 - w->x0 + 1U) * (w->y1 - w->y0 + 1U);
}

// Find changed tiles even when the compositor invalidates the whole screen.
// On transfer failure need_half invalidates all checksums before the next update.
static bool prv_dirty_window(const struct pbl_display_ssd1677 *epd,
                             const struct pbl_display_buffer *buf, uint16_t y0, uint16_t y1,
                             struct pbl_display_ssd1677_window *window,
                             struct uc_dirty_regions *regions) {
  struct pbl_display_ssd1677_state *s = epd->state;
  const uint16_t width = epd->disp.props.width;
  const uint16_t height = epd->disp.props.height;
  uint16_t left = width, top = height, right = 0, bottom = 0;
  if (s->need_half) {
    y0 = 0;
    y1 = height - 1U;
  }
  for (unsigned y = y0 & ~31U; y <= y1; y += 32) {
    unsigned rows = PBL_MIN(32U, height - y);
    unsigned dirty = 0;
    for (unsigned x = 0; x < width; x += 32) {
      unsigned cols = PBL_MIN(32U, width - x);
      unsigned bytes = (cols + 7U) / 8U;
      const uint8_t *data = buf->data + y * buf->pitch + x / 8U;
      uint32_t crc = prv_tile_crc(data, buf->pitch, bytes, rows);
      uint32_t *previous = &s->tile_crc[y / 32U][x / 32U];
      if (!s->need_half && *previous == crc) {
        continue;
      }
      *previous = crc;
      dirty |= 1U << (x / 32U);
      left = PBL_MIN(left, x);
      right = PBL_MAX(right, x + cols - 1U);
      top = PBL_MIN(top, y);
      bottom = PBL_MAX(bottom, y + rows - 1U);
    }
    if (!s->need_half && dirty && regions->count <= PBL_ARRAY_SIZE(regions->rects)) {
      for (unsigned col = 0; dirty; ++col, dirty >>= 1) {
        if (!(dirty & 1U)) {
          continue;
        }
        const unsigned first = col;
        while (dirty & 2U) {
          ++col;
          dirty >>= 1;
        }
        prv_add_region(regions,
                       prv_scan_rect(epd, first * 32, y, PBL_MIN(width, (col + 1U) * 32U) - 1U,
                                     y + rows - 1U));
      }
    }
  }
  if (left > right) {
    return false;
  }
  *window = prv_scan_rect(epd, left, top, right, bottom);
  bool sparse = regions->count > 1 && regions->count <= PBL_ARRAY_SIZE(regions->rects);
  if (sparse) {
    unsigned bytes = 0;
    for (unsigned i = 0; i < regions->count; ++i) {
      bytes += prv_window_bytes(&regions->rects[i]);
    }
    // Allow for each additional SPI command sequence and short row transfer.
    unsigned full_bytes = prv_window_bytes(window);
    sparse = bytes + (regions->count - 1U) * 256U < full_bytes;
#ifdef CONFIG_SHELL
    regions->saved_per_plane = full_bytes - bytes;
#endif
  }
  if (!sparse) {
    regions->rects[0] = *window;
    regions->count = 1;
  }
  return true;
}

static int prv_partial_window(const struct pbl_display_ssd1677 *epd,
                              const struct pbl_display_ssd1677_window *w) {
  uint16_t x0 = w->x0 * 8U, x1 = w->x1 * 8U + 7U;
  uint16_t y0 = w->y0 + 120U, y1 = w->y1 + 120U;
  const uint8_t coords[] = {x0 >> 8, x0, x1 >> 8, x1, y0 >> 8, y0, y1 >> 8, y1, 1};
  int ret = epaper_write_command(epd, 0x91, nullptr, 0);
  return ret != 0 ? ret : epaper_write_command(epd, 0x90, coords, sizeof(coords));
}

static int prv_write_regions(const struct pbl_display_ssd1677 *epd, uint8_t command,
                             const uint8_t *frame, const struct uc_dirty_regions *regions) {
  for (unsigned i = 0; i < regions->count; ++i) {
    int ret = prv_partial_window(epd, &regions->rects[i]);
    if (ret == 0) {
      ret = epaper_write_window(epd, command, frame, &regions->rects[i]);
    }
    if (ret == 0 && i + 1 < regions->count) {
      ret = epaper_write_command(epd, 0x92, nullptr, 0);
    }
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}

#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>
#include <pbl/util/misc.h>

static int prv_motion(const struct pbl_shell *sh, size_t argc, char **argv) {
  unsigned long frames = 0;
  bool fast = strcmp(argv[1], "fast") == 0;
  bool automatic = fast || strcmp(argv[1], "auto") == 0;
  if ((!automatic && pbl_shell_strtoul(argv[1], &frames)) || frames > 3 ||
      (argc > 2 && strcmp(argv[2], "text-clock") != 0)) {
    return -EINVAL;
  }
  const struct pbl_device *dev;
  PBL_DEVICE_CLASS_FOREACH(&pbl_display_class, dev) {
    const struct pbl_display *disp = PBL_CONTAINER_OF(dev, const struct pbl_display, dev);
    if (!pbl_device_is_ready(dev) || disp->ops != &pbl_display_ssd1677_ops) {
      continue;
    }
    const struct pbl_display_ssd1677 *epd =
        PBL_CONTAINER_OF(disp, const struct pbl_display_ssd1677, disp);
    struct pbl_display_ssd1677_state *state = epd->state;
    if (!state->uc8279 || state->panel_id[0][3] != 0x68) {
      return -ENOTSUP;
    }
    pbl_mutex_lock(&state->lock, PBL_FOREVER);
    state->measured_motion_drives = 0;
    state->motion_busy_ms = 0;
    state->motion_min_busy_ms = 0;
    state->motion_override = !automatic;
    state->motion_text_clock = argc > 2;
    state->pulse_frames = automatic ? (fast ? 1 : 3) : frames;
    state->motion_left = frames ? 96 : 0;
    pbl_mutex_unlock(&state->lock);
    if (automatic) {
      pbl_shell_print(sh, "Adaptive motion enabled (%u scan frames)", fast ? 1U : 3U);
      return 0;
    }
    pbl_shell_print(sh, "Motion pulse %u frames, limited to 96 small updates", (unsigned)frames);
    return 0;
  }
  return -ENODEV;
}

PBL_SHELL_SUBCMD_ADD(sub_epd, pulse, nullptr,
                     "Test paired transition pulses, 0=OTP, fast=low contrast, auto=balanced",
                     prv_motion, 2, 1);

#endif

static int prv_motion_draw(const struct pbl_display_ssd1677 *epd, unsigned frames,
                           bool text_clock) {
  const uint8_t reg[] = {0x37, 0x4d};
  int ret = epaper_write_command(epd, 0x00, reg, sizeof(reg));
  if (ret == 0 && text_clock) {
    const uint8_t pll = 0x0f; // X4 Pro factory text-AA scan clock
    ret = epaper_write_command(epd, 0x30, &pll, 1);
  }
  // Reuse the factory settle bank's transition polarities. Static pixels and VCOM stay idle.
  for (unsigned i = 0; i < 5 && ret == 0; ++i) {
    uint8_t bank[42] = {1, frames, 0, 0, 0, 1, 1};
    if (i == 2)
      bank[1] |= 0x80;
    if (i == 3)
      bank[1] |= 0x40;
    ret = epaper_write_command(epd, 0x20 + i, bank, sizeof(bank));
  }
  return ret ? ret : epaper_write_command(epd, 0x12, nullptr, 0);
}

static int prv_update_bw(const struct pbl_display_ssd1677 *epd,
                         const struct pbl_display_buffer *buf, uint16_t y0, uint16_t y1) {
  struct pbl_display_ssd1677_state *state = epd->state;
  static const struct uc_command partial_drive[] = {
    {0x03, 1, {0x20}}, // Partial-drive power-off sequence
    {0xe1, 1, {0x02}}, // Gate scan
  };
  static const struct uc_command draw[] = {
    {0x00, 2, {0x17, 0x4d}}, // Select OTP after power on reloads panel defaults
    {0x12, 0, {0}},
  };
  if (buf->pitch != (epd->disp.props.width + 7U) / 8U) {
    return -EINVAL;
  }
  uint32_t start = (uint32_t)pbl_uptime_ms();
  struct pbl_display_ssd1677_window w = {0};
  struct uc_dirty_regions regions = {0};
  if (!prv_dirty_window(epd, buf, y0, y1, &w, &regions)) {
    if (!state->last_was_grey || state->next_retains_grey) {
      return 0;
    }
    // Both RAM planes already hold the same B/W base. Re-drive the glass absolutely.
    state->need_half = true;
  }
  bool partial =
      !state->need_half && (epd->full_interval == 0U || state->updates < epd->full_interval);
  // A legacy 144x168 watchface can animate across its entire doubled canvas.
  unsigned window_pixels = (w.x1 - w.x0 + 1U) * 8U * (w.y1 - w.y0 + 1U);
  bool rapid = false;
#ifdef CONFIG_TEXT_DETAIL
  // The compositor restores full contrast at idle on boards with text refinement.
  rapid = state->powered && start - state->idle_since_ms <= 250U;
#endif
  // The optional low-contrast profile permits a longer burst on very small windows.
  // Larger motion, idle refinement and the balanced profile retain the shorter cleaning interval.
  bool small_fast = !state->motion_override && state->pulse_frames == 1U &&
                    window_pixels <= 16384U;
  bool bounded = state->unsettled_pulses < (small_fast ? 256U : 96U);
  bool motion = state->motion_override ? state->motion_left != 0U : rapid && bounded;
  bool motion_window =
      state->panel_id[0][3] == 0x68 && motion && !state->need_half && window_pixels <= 160000U;
  bool text_clock = !state->motion_override || state->motion_text_clock;
  // A small clock tick can use the factory differential waveform indefinitely between
  // animations. Counting those ticks as short pulses forces a full-screen flash.
  bool clock_tick = !rapid && !state->motion_override && !state->need_half &&
                    !state->last_was_grey && window_pixels <= 16384U;
  bool continuous = buf->continuous && !state->need_half && !state->last_was_grey;
  partial = continuous || clock_tick ||
            ((partial || motion_window) && (bounded || !state->unsettled_pulses));
  const struct uc_command drive[] = {
    {0x50, 1, {partial ? 0xd7 : 0x97}}, // VCOM/data interval
    {0xe0, 1, {0x02}},                  // Cascade
    {0xe5, 1, {partial ? 0x5a : 0x1e}}, // Temperature setting for OTP waveform
  };
  int ret = 0;
  if (partial && state->last_was_grey) {
    return uc8279_x4_leave_grey(epd, buf);
  }
  state->last_was_grey = false;
  if (partial) {
    state->last_window_width = (w.x1 - w.x0 + 1U) * 8U;
    state->last_window_height = w.y1 - w.y0 + 1U;
    ret = prv_write_regions(epd, 0x13, buf->data, &regions);
    if (ret == 0) {
      ret = epaper_write_command(epd, 0x92, nullptr, 0);
    }
  } else {
    state->last_window_width = epd->panel_height;
    state->last_window_height = epd->panel_width;
    ret = epaper_write_frame(epd, 0x13, buf->data);
  }
  if (ret == 0 && !partial) {
    ret = epaper_write_frame(epd, 0x10, nullptr);
  }
  state->last_write_ms = (uint32_t)pbl_uptime_ms() - start;
  start = (uint32_t)pbl_uptime_ms();
  if (ret == 0) {
    ret = prv_uc_command_list(epd, drive, PBL_ARRAY_SIZE(drive));
  }
  if (ret == 0 && !state->powered) {
    ret = epaper_write_command(epd, 0x04, nullptr, 0);
    if (ret == 0) {
      ret = prv_uc_wait(epd);
    }
    state->powered = ret == 0;
    if (state->powered) {
      state->power_off_pending = false;
    }
  }
  if (ret == 0 && partial) {
    ret = prv_uc_command_list(epd, partial_drive, PBL_ARRAY_SIZE(partial_drive));
    if (ret == 0) {
      ret = prv_partial_window(epd, &w);
    }
  }
  if (ret == 0) {
    if (partial && motion_window) {
      if (state->motion_override) {
        state->motion_left--;
      }
      ret = prv_motion_draw(epd, state->pulse_frames ? state->pulse_frames : 3, text_clock);
    } else {
      ret = prv_uc_command_list(epd, draw, PBL_ARRAY_SIZE(draw));
    }
  }
#ifdef CONFIG_SHELL
  uint32_t fired = (uint32_t)pbl_uptime_ms();
#endif
  if (ret == 0) {
    ret = PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 0, 1, 100);
  }
  if (ret == 0) {
    ret = prv_uc_wait(epd);
  }
  if (ret == 0) {
    state->completed_drives++;
    state->last_drive = partial ? UC8279_DRIVE_PARTIAL : UC8279_DRIVE_FULL;
    if (!partial) {
      state->full_mono_drives++;
      state->unsettled_pulses = 0;
    } else if (motion_window) {
      state->unsettled_pulses++;
      state->motion_drives++;
#ifdef CONFIG_SHELL
      uint32_t busy = (uint32_t)pbl_uptime_ms() - fired;
      if (!state->measured_motion_drives || busy < state->motion_min_busy_ms) {
        state->motion_min_busy_ms = busy;
      }
      state->measured_motion_drives++;
      state->motion_busy_ms += busy;
#endif
      state->last_drive = UC8279_DRIVE_MOTION;
    }
  }
  state->last_refresh_ms = (uint32_t)pbl_uptime_ms() - start;
  if (partial && motion_window && text_clock) {
    const uint8_t normal_pll = 0x0e;
    int restore = epaper_write_command(epd, 0x30, &normal_pll, 1);
    if (!ret)
      ret = restore;
  }
  start = (uint32_t)pbl_uptime_ms();
  if (ret == 0 && partial) {
    ret = prv_write_regions(epd, 0x10, buf->data, &regions);
  } else if (ret == 0) {
    ret = epaper_write_frame(epd, 0x10, buf->data);
  }
  if (partial) {
    int leave = epaper_write_command(epd, 0x92, nullptr, 0);
    if (ret == 0) {
      ret = leave;
    }
  }
#ifdef CONFIG_SHELL
  if (ret == 0 && partial && regions.count > 1) {
    state->sparse_updates++;
    state->sparse_regions += regions.count;
    state->sparse_bytes_saved += regions.saved_per_plane * 2U;
  }
#endif
  state->last_post_ms = (uint32_t)pbl_uptime_ms() - start;
  state->need_half = ret != 0;
  // A stock differential tick between bursts restores the changing pixels. Retain the
  // pending idle cleanup for when the user leaves the watchface.
  if (ret == 0 && continuous && !rapid && !motion_window && state->unsettled_pulses) {
    state->unsettled_pulses = 1;
  }
  state->updates = partial ? state->updates + 1U : 0U;
  state->idle_since_ms = (uint32_t)pbl_uptime_ms();
  if (state->powered && epd->power_off_ms != 0U) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &state->idle, PBL_MSEC(epd->power_off_ms));
  }
  return ret;
}

void uc8279_x4_submit(const struct pbl_display_ssd1677 *epd, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  // The display class retains the framebuffer until completion. The descriptor is caller-owned.
  epd->state->next_grey = buf->detail && buf->detail->grey;
  epd->state->next_retains_grey = buf->detail != nullptr;
  epd->state->next_frame = *buf;
  epd->state->next_frame.detail = nullptr;
  epd->state->next_y0 = y0;
  epd->state->next_y1 = y1;
  pbl_sem_give(&epd->state->frame_ready);
}
