/* SPDX-License-Identifier: Apache-2.0 */

// Boot splash: same sequence the shipping QEMU build renders via
// services/boot_splash/service.c — pebbleOS logo + ping-pong progress bar at
// 100 ms/frame, then a logo-only final frame once boot completes. Drawing
// mirrors that file exactly (same xbm, geometry, ARGB2222 colors) so the
// frames are pixel-identical to the reference.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "board/splash.h"

#define DISP_W 200
#define DISP_H 228

#define SPLASH_COLOR_WHITE 0xFF
#define SPLASH_COLOR_BLACK 0xC0
#define SPLASH_COLOR_LGRAY 0xEA

#define PROGRESS_BAR_WIDTH 80
#define PROGRESS_BAR_HEIGHT 4
#define PROGRESS_INDICATOR_WIDTH 20
#define PROGRESS_FRAME_DELAY_MS 100
#define PROGRESS_TOTAL_FRAMES 20

// sandbox_launcher.c / watchface_sandboxed port.c
void fw_display_push_buffer(const uint8_t *buffer);
uint8_t *watchface_framebuffer_bytes(size_t *size_out, uint16_t *stride_out);

static void prv_fill_rect(uint8_t *fb, int16_t x0, int16_t y0, int16_t w, int16_t h,
                          uint8_t color) {
  for (int16_t y = y0; y < y0 + h; y++) {
    for (int16_t x = x0; x < x0 + w; x++) {
      if (x >= 0 && x < DISP_W && y >= 0 && y < DISP_H) {
        fb[(uint32_t)y * DISP_W + x] = color;
      }
    }
  }
}

static void prv_draw_logo(uint8_t *fb) {
  const uint16_t x0 = (DISP_W - splash_width) / 2;
  const uint16_t y0 = (DISP_H - splash_height) / 2;
  memset(fb, SPLASH_COLOR_WHITE, (size_t)DISP_W * DISP_H);
  for (uint16_t y = 0; y < splash_height; y++) {
    for (uint16_t x = 0; x < splash_width; x++) {
      if (splash_bits[y * (splash_width / 8) + x / 8] & (0x1U << (x & 7))) {
        fb[(y + y0) * DISP_W + (x + x0)] = SPLASH_COLOR_BLACK;
      }
    }
  }
}

static void prv_draw_progress(uint8_t *fb, uint16_t frame) {
  const int16_t cx = DISP_W / 2;
  const int16_t cy = (DISP_H - splash_height) / 2 + splash_height + 20;
  const int16_t bar_x0 = cx - PROGRESS_BAR_WIDTH / 2;
  const int16_t bar_y0 = cy - PROGRESS_BAR_HEIGHT / 2;
  prv_fill_rect(fb, bar_x0, bar_y0, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, SPLASH_COLOR_LGRAY);

  const int32_t max_travel = PROGRESS_BAR_WIDTH - PROGRESS_INDICATOR_WIDTH;
  const int32_t half = PROGRESS_TOTAL_FRAMES / 2;
  const int32_t cycle = frame % PROGRESS_TOTAL_FRAMES;
  const int32_t offset = (cycle < half) ? (cycle * max_travel) / half
                                        : max_travel - ((cycle - half) * max_travel) / half;
  prv_fill_rect(fb, bar_x0 + offset, bar_y0, PROGRESS_INDICATOR_WIDTH, PROGRESS_BAR_HEIGHT,
                SPLASH_COLOR_BLACK);
}

// Renders the splash synchronously between display init and the first shell
// app. The reference's boot takes ~470 ms and shows 3 animated frames + the
// final logo; the port's init is already done here, so it paces the same
// 3-frame sequence rather than racing straight to the launcher.
// ponytail: frame count calibrated to the ref boot duration on this host;
// re-count the ref's boot frames if its init timing changes.
void fw_boot_splash_show(void) {
  size_t size;
  uint16_t stride;
  uint8_t *fb = watchface_framebuffer_bytes(&size, &stride);
  if (!fb || size < (size_t)DISP_W * DISP_H) {
    return;
  }
  for (uint16_t frame = 0; frame < 3; frame++) {
    prv_draw_logo(fb);
    prv_draw_progress(fb, frame);
    fw_display_push_buffer(fb);
    k_sleep(K_MSEC(PROGRESS_FRAME_DELAY_MS));
  }
  prv_draw_logo(fb);
  fw_display_push_buffer(fb);
}
