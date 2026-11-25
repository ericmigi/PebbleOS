/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <pbl/analytics/analytics.h>
#include <pbl/board.h>
#include <pbl/drivers/display.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/malloc.h>
#include <pbl/kernel/time.h>
#include <pbl/services/compositor/compositor.h>
#include <pbl/services/compositor/compositor_private.h>
#include <pbl/util/math.h>

#include <applib/graphics/framebuffer.h>
#include <applib/graphics/gcolor_definitions.h>
#include <applib/graphics/gtypes.h>
#include <kernel/events.h>
#include <kernel/pebble_tasks.h>

#ifdef CONFIG_TEXT_DETAIL
#include <pbl/services/evented_timer.h>

#include <applib/graphics/text_detail.h>
#endif

static void (*s_update_complete_handler)(bool success);
// From the start of a flush until its completion has run here, after the display's own: a flush
// started in between would have its dirty rows reset by the previous one's completion
static bool s_flush_pending;

static uint64_t s_flush_start_ticks;
static uint32_t s_flush_count;
static uint64_t s_flush_ticks;

static uint16_t s_flush_y0;
static uint16_t s_flush_y1;

// Corner pixels hidden by the display mounting, drawn into the framebuffer for the flush and
// undone after it. Holds the bytes of each masked row: [row][left, right][byte]
#ifdef CONFIG_BOARD_DISPLAY_CORNERS
static uint8_t *s_saved_corners;
static uint8_t s_corner_bytes;

#if PBL_BW
#define PIXEL_BYTE(x) ((x) / 8)
#else
#define PIXEL_BYTE(x) (x)
#endif

static void prv_mask_pixel(uint8_t *line, uint16_t x) {
#if PBL_BW
  line[x / 8] &= ~(1U << (x % 8));
#else
  line[x] = GColorBlackARGB8;
#endif
}

static uint8_t prv_corner_row(uint16_t y, uint8_t *save_idx) {
  const uint8_t rows = BOARD_DISPLAY_NUM_CORNERS;

  if (y < rows) {
    *save_idx = y;
    return BOARD_DISPLAY_CORNERS[y];
  }

  if (y >= PBL_DISPLAY_HEIGHT - rows) {
    *save_idx = rows + (PBL_DISPLAY_HEIGHT - y - 1);
    return BOARD_DISPLAY_CORNERS[PBL_DISPLAY_HEIGHT - y - 1];
  }

  return 0;
}

static void prv_corners_apply(FrameBuffer *fb, bool mask) {
  if (s_saved_corners == nullptr) {
    return;
  }

  for (uint16_t y = s_flush_y0; y <= s_flush_y1; y++) {
    uint8_t idx;
    const uint8_t width = prv_corner_row(y, &idx);
    if (width == 0) {
      continue;
    }

    uint8_t *line = (uint8_t *)framebuffer_get_line(fb, y);
    uint8_t *saved = &s_saved_corners[idx * 2 * s_corner_bytes];
    uint16_t left = PIXEL_BYTE(0);
    uint16_t right = PIXEL_BYTE(PBL_DISPLAY_WIDTH - width);
    uint16_t len = PIXEL_BYTE(width - 1) - left + 1;

    if (mask) {
      memcpy(saved, &line[left], len);
      memcpy(&saved[s_corner_bytes], &line[right], len);
      for (uint8_t x = 0; x < width; x++) {
        prv_mask_pixel(line, x);
        prv_mask_pixel(line, PBL_DISPLAY_WIDTH - x - 1);
      }
    } else {
      memcpy(&line[left], saved, len);
      memcpy(&line[right], &saved[s_corner_bytes], len);
    }
  }
}
#endif

#ifdef CONFIG_TEXT_DETAIL
// Text edges of the frame on the display, at panel resolution
static TextDetailFrame s_detail_frame;
static EventedTimerID s_grey_timer = EVENTED_TIMER_INVALID_ID;

static bool prv_detail_row(const struct pbl_display_detail *detail, uint16_t y, uint8_t *cells) {
  TextDetailFrame frame = s_detail_frame;
  frame.grey = detail->grey;
  return text_detail_row(
      &frame, y, (const uint8_t *)framebuffer_get_line(compositor_get_framebuffer(), y), cells);
}

static struct pbl_display_detail s_detail = {.row = prv_detail_row};

// Grey levels come once the screen is idle, as on e-ink, where they take a pass of their own after
// the black and white frame and its top-up: interaction keeps the speed of black and white.
#define GREY_IDLE_MS (150 + 200)

static void prv_send(uint16_t y0, uint16_t y1, bool grey);

static void prv_grey_refine(void *data) {
  s_grey_timer = EVENTED_TIMER_INVALID_ID;
  if (s_flush_pending || !text_detail_wants_grey()) {
    return;
  }
  s_flush_pending = true;
  s_flush_y0 = 0;
  s_flush_y1 = PBL_DISPLAY_HEIGHT * FRAMEBUFFER_SCALE - 1;
  s_flush_start_ticks = pbl_uptime_ticks();
  prv_send(s_flush_y0, s_flush_y1, true);
}
#endif

static void prv_flush_finish(void *data) {
  bool success = (data == nullptr);

  s_flush_pending = false;

#ifdef CONFIG_BOARD_DISPLAY_CORNERS
  prv_corners_apply(compositor_get_framebuffer(), false);
#endif

  uint64_t elapsed = pbl_uptime_ticks() - s_flush_start_ticks;
  pbl_irq_lock();
  s_flush_count++;
  s_flush_ticks += elapsed;
  pbl_irq_unlock();

  framebuffer_reset_dirty(compositor_get_framebuffer());

#ifdef CONFIG_TEXT_DETAIL
  if (success && !s_detail.grey && text_detail_wants_grey()) {
    s_grey_timer =
        evented_timer_register_or_reschedule(s_grey_timer, GREY_IDLE_MS, prv_grey_refine, nullptr);
  }
#endif

  if (s_update_complete_handler) {
    s_update_complete_handler(success);
  }
}

// The display reports synchronously from compositor_display_update(), or from its interrupt or
// a thread of its own, which hand the end to KernelMain
static void prv_flush_done(int status, void *arg) {
  void *data = (status == 0) ? nullptr : (void *)1;
  const bool isr = pbl_in_isr();

  if (!isr && pebble_task_get_current() == PebbleTask_KernelMain) {
    prv_flush_finish(data);
    return;
  }

  PebbleEvent e = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = {
      .callback = prv_flush_finish,
      .data = data,
    },
  };

  if (isr) {
    event_put_isr(&e);
  } else {
    event_put(&e);
  }
}

void compositor_display_init(void) {
#if PBL_BW
  const enum pbl_display_format format = PBL_DISPLAY_FORMAT_MONO;
#else
  const enum pbl_display_format format = PBL_DISPLAY_FORMAT_ARGB2222;
#endif
  const struct pbl_display_props *props = pbl_display_get_props(DISPLAY);

  PBL_ASSERTN((props->width == PBL_DISPLAY_WIDTH * FRAMEBUFFER_SCALE) &&
              (props->height == PBL_DISPLAY_HEIGHT * FRAMEBUFFER_SCALE) &&
              (props->format == format));
#if PBL_ROUND
  PBL_ASSERTN(props->round_mask != nullptr);
#endif

#ifdef CONFIG_BOARD_DISPLAY_CORNERS
  uint8_t max_width = 0;
  for (uint8_t i = 0; i < BOARD_DISPLAY_NUM_CORNERS; i++) {
    max_width = PBL_MAX(max_width, BOARD_DISPLAY_CORNERS[i]);
  }

  if (max_width > 0) {
    s_corner_bytes = PIXEL_BYTE(max_width - 1) + 1;
    s_saved_corners = pbl_malloc_check(BOARD_DISPLAY_NUM_CORNERS * 2 * 2 * s_corner_bytes);
  }
#endif
}

void compositor_display_update(void (*handle_update_complete_cb)(bool success)) {
  FrameBuffer *fb = compositor_get_framebuffer();
  if (!framebuffer_is_dirty(fb)) {
    return;
  }
  // Every flush drops the mirror; the compositor restores it after an app frame's flush
  compositor_app_framebuffer_mirror_invalidate();

  // The display's rows, FRAMEBUFFER_SCALE per row of the dirty rectangle
  s_flush_y0 = fb->dirty_rect.origin.y * FRAMEBUFFER_SCALE;
  s_flush_y1 = (fb->dirty_rect.origin.y + fb->dirty_rect.size.h) * FRAMEBUFFER_SCALE - 1;
  s_update_complete_handler = handle_update_complete_cb;
  s_flush_start_ticks = pbl_uptime_ticks();
  s_flush_pending = true;

#ifdef CONFIG_BOARD_DISPLAY_CORNERS
  prv_corners_apply(fb, true);
#endif

#ifdef CONFIG_TEXT_DETAIL
  evented_timer_cancel(s_grey_timer);
  s_grey_timer = EVENTED_TIMER_INVALID_ID;
  compositor_get_text_detail_frame(&s_detail_frame);
  prv_send(s_flush_y0, s_flush_y1, false);
#else
  struct pbl_display_buffer buf = {
    .data = (uint8_t *)framebuffer_get_line(fb, 0),
    .pitch = FRAMEBUFFER_BYTES_PER_ROW,
  };

  if (pbl_display_update(DISPLAY, &buf, s_flush_y0, s_flush_y1, prv_flush_done, nullptr) != 0) {
    prv_flush_finish((void *)1);
  }
#endif
}

#ifdef CONFIG_TEXT_DETAIL
static void prv_send(uint16_t y0, uint16_t y1, bool grey) {
  s_detail.grey = grey;
  struct pbl_display_buffer buf = {
    .data = (uint8_t *)framebuffer_get_line(compositor_get_framebuffer(), 0),
    .pitch = FRAMEBUFFER_BYTES_PER_ROW,
    .detail = &s_detail,
  };

  if (pbl_display_update(DISPLAY, &buf, y0, y1, prv_flush_done, nullptr) != 0) {
    prv_flush_finish((void *)1);
  }
}
#endif

bool compositor_display_update_in_progress(void) {
  return s_flush_pending;
}

static void prv_collect_display_stats(void) {
  pbl_irq_lock();
  uint32_t count = s_flush_count;
  uint64_t ticks = s_flush_ticks;
  s_flush_count = 0;
  s_flush_ticks = 0;
  pbl_irq_unlock();

  PBL_ANALYTICS_SET_UNSIGNED(display_update_count, count);
  PBL_ANALYTICS_SET_UNSIGNED(display_flush_time_ms, (uint32_t)((ticks * 1000) / PBL_TICK_HZ));
}
PBL_ANALYTICS_COLLECTOR_DEFINE(prv_collect_display_stats);

#ifdef CONFIG_TEXT_DETAIL
#include <pbl/shell/shell.h>

#include <kernel/event_loop.h>

// Records: edge pixels at 2x, glyphs at panel resolution
#ifdef CONFIG_FRAMEBUFFER_SCALE2
#define TEXT_DETAIL_CAPACITY CONFIG_TEXT_DETAIL_GLYPHS
#else
#define TEXT_DETAIL_CAPACITY CONFIG_TEXT_DETAIL_CELLS
#endif

static const char *const s_mode_names[TextDetailModeCount] = {
  [TextDetailModeMono] = "mono",
  [TextDetailModeGrey] = "grey",
  [TextDetailModeSubpixel] = "sub",
  [TextDetailModeSubpixelGrey] = "subgrey",
};

static void prv_redraw(void *data) {
  compositor_redraw();
}

static int prv_cmd_text_mode(const struct pbl_shell *sh, size_t argc, char **argv) {
  if (argc > 1) {
    TextDetailMode mode = TextDetailModeCount;
    for (int i = 0; i < TextDetailModeCount; i++) {
      if (strcmp(argv[1], s_mode_names[i]) == 0) {
        mode = (TextDetailMode)i;
      }
    }
    if (mode == TextDetailModeCount) {
      pbl_shell_error(sh, "modes: mono, grey, sub, subgrey");
      return -EINVAL;
    }
    text_detail_set_mode(mode);
    launcher_task_add_callback(prv_redraw, nullptr);
  }
  uint32_t used;
  uint32_t peak;
  uint32_t dropped;
  text_detail_get_stats(&used, &peak, &dropped);
  pbl_shell_print(sh, "%s; edge pixels %" PRIu32 " (peak %" PRIu32 " of %d, %" PRIu32 " dropped)",
                  s_mode_names[text_detail_get_mode()], used, peak, TEXT_DETAIL_CAPACITY, dropped);
  return 0;
}

#ifdef CONFIG_FRAMEBUFFER_SCALE2
// Black bars and pixels under the test pattern of text_detail_test(), which the grey pass
// lightens: light grey, then dark grey bars, rows 96-159, and single pixels on rows 200-201
static void prv_greytest(void *data) {
  FrameBuffer *fb = compositor_get_framebuffer();
  for (uint16_t y = 0; y < PBL_DISPLAY_HEIGHT * FRAMEBUFFER_SCALE; y++) {
    uint8_t *row = (uint8_t *)framebuffer_get_line(fb, y);
    for (int x = 0; x < PBL_DISPLAY_WIDTH * FRAMEBUFFER_SCALE; x++) {
      const bool bar = (y >= 96 && y < 160 && x >= 48 && x < 432);
      const bool dot = (y == 200 && (x == 100 || x == 102)) || (y == 201 && (x == 300 || x == 301));
      if (bar || dot) {
        row[x / 8] &= (uint8_t)~(1U << (x % 8));
      }
    }
  }
  text_detail_test();
  compositor_redraw();
}

static int prv_cmd_text_greytest(const struct pbl_shell *sh, size_t argc, char **argv) {
  launcher_task_add_callback(prv_greytest, nullptr);
  pbl_shell_print(sh, "grey test pattern: black bars, then grey at the next idle pass");
  return 0;
}
#endif

static const struct pbl_shell_cmd sub_text[] = {
  PBL_SHELL_CMD(mode, nullptr, "Show or set the text mode: mono, grey, sub or subgrey",
                prv_cmd_text_mode),
#ifdef CONFIG_FRAMEBUFFER_SCALE2
  PBL_SHELL_CMD(greytest, nullptr, "Draw a grey test pattern through the grey pass",
                prv_cmd_text_greytest),
#endif
  PBL_SHELL_SUBCMD_SET_END,
};

PBL_SHELL_CMD_REGISTER(text, sub_text, "System UI text", nullptr);
#endif
