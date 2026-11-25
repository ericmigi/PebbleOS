/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "display_sdl_bottom.h"

#include <errno.h>
#include <string.h>

#include <pbl/drivers/display/sdl.h>
#include <pbl/shell/shell.h>

#ifdef CONFIG_DISPLAY_SDL_ROUND
#include <pbl/drivers/panel/ls013b7dd02.h>
#endif

#define SCALE CONFIG_DISPLAY_SDL_PANEL_SCALE

static uint8_t s_fb[CONFIG_DISPLAY_SDL_WIDTH * CONFIG_DISPLAY_SDL_HEIGHT * SCALE * SCALE];
static bool s_enabled;

static void prv_flush(const struct pbl_display *disp) {
  if (s_enabled) {
    display_sdl_bottom_update(s_fb);
  }
}

static int prv_init(const struct pbl_display *disp) {
  const struct pbl_display_props *props = pbl_display_get_props(disp);

  if ((uint32_t)props->width * props->height * SCALE * SCALE > sizeof(s_fb)) {
    return -EINVAL;
  }

  // The window shows the display described here, round mask included, at panel resolution
  display_sdl_bottom_set_geometry(props->width * SCALE, props->height * SCALE,
                                  SCALE == 1 ? props->round_mask : nullptr);
  s_enabled = true;

  return 0;
}

static int prv_enable(const struct pbl_display *disp) {
  s_enabled = true;

  return 0;
}

static int prv_disable(const struct pbl_display *disp) {
  s_enabled = false;

  return 0;
}

static int prv_set_rotated(const struct pbl_display *disp, bool rotated) {
  return 0;
}

// ARGB2222 greys for panel levels 0 (white) to 3 (black)
static const uint8_t s_levels[4] = {0xffU, 0xeaU, 0xd5U, 0xc0U};

static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  const uint16_t width = disp->props.width;
  uint8_t cells[CONFIG_DISPLAY_SDL_WIDTH];
  const bool has_detail =
      (SCALE == 2) && (buf->detail != nullptr) && (disp->props.format == PBL_DISPLAY_FORMAT_MONO);

  for (uint16_t y = y0; y <= y1; y++) {
    const uint8_t *src = &buf->data[y * buf->pitch];
    const bool detail = has_detail && buf->detail->row(buf->detail, y, cells);

    for (uint16_t x = 0U; x < width; x++) {
      uint8_t pixel;

      if (disp->props.format == PBL_DISPLAY_FORMAT_MONO) {
        pixel = (src[x / 8U] & (1U << (x % 8U))) != 0U ? 0xffU : 0xc0U;
      } else {
        pixel = src[x];
      }
      for (uint16_t i = 0U; i < SCALE * SCALE; i++) {
        const uint8_t level = detail ? (cells[x] >> (2U * i)) & 3U : 0U;
        s_fb[(y * SCALE + i / SCALE) * width * SCALE + x * SCALE + i % SCALE] =
            (detail && cells[x] != 0U) ? s_levels[level] : pixel;
      }
    }
  }

  prv_flush(disp);
  pbl_display_complete(disp, 0);

  return 0;
}

static int prv_clear(const struct pbl_display *disp) {
  memset(s_fb, 0, sizeof(s_fb));
  prv_flush(disp);

  return 0;
}

const struct pbl_display_ops pbl_display_sdl_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_set_rotated,
  .update = prv_update,
  .clear = prv_clear,
};

#ifdef CONFIG_DISPLAY_SDL_MONO
#define FORMAT PBL_DISPLAY_FORMAT_MONO
#else
#define FORMAT PBL_DISPLAY_FORMAT_ARGB2222
#endif

#ifdef CONFIG_DISPLAY_SDL_ROUND
#define ROUND_MASK pbl_panel_ls013b7dd02_round_mask
#else
#define ROUND_MASK nullptr
#endif

PBL_DISPLAY_SDL_DEFINE(pbl_display_sdl, "display", CONFIG_DISPLAY_SDL_WIDTH,
                       CONFIG_DISPLAY_SDL_HEIGHT, FORMAT, .round_mask = ROUND_MASK);

static int prv_cmd_shot(const struct pbl_shell *sh, size_t argc, char **argv) {
  if (argc < 2 || display_sdl_bottom_save(argv[1]) != 0) {
    pbl_shell_error(sh, "usage: sdlshot FILE.bmp");
    return -EINVAL;
  }
  return 0;
}

PBL_SHELL_CMD_REGISTER(sdlshot, nullptr, "Save the window's frame as a BMP", prv_cmd_shot);
