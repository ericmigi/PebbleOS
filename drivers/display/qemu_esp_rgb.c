/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "geometry.h"

#include <errno.h>
#include <stdint.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/display/qemu_esp_rgb.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/misc.h>

#define RGB_WIN_SIZE       0x04
#define RGB_UPDATE_FROM    0x08
#define RGB_UPDATE_TO      0x0C
#define RGB_UPDATE_CONTENT 0x10
#define RGB_UPDATE_STATUS  0x14
#define RGB_BPP_VALUE      0x18

#define XY(x, y)          (((uint32_t)(x) << 16) | (uint32_t)(y))
#define UPDATE_STATUS_ENA (1U << 0)
#define BPP_16            16U
#define RGB565_WHITE      0xFFFFU
#define RGB565_BLACK      0x0000U

static const struct pbl_display_qemu_esp_rgb *prv_rgb(const struct pbl_display *disp) {
  return PBL_CONTAINER_OF(disp, const struct pbl_display_qemu_esp_rgb, disp);
}

static uint16_t prv_win_width(const struct pbl_display_qemu_esp_rgb *rgb) {
  return rgb->quarter ? rgb->panel_height : rgb->panel_width;
}

static uint16_t prv_win_height(const struct pbl_display_qemu_esp_rgb *rgb) {
  return rgb->quarter ? rgb->panel_width : rgb->panel_height;
}

// The peripheral keeps a single pending area, so the whole window is always shown
static void prv_refresh(const struct pbl_display_qemu_esp_rgb *rgb) {
  pbl_writel(XY(0U, 0U), rgb->base + RGB_UPDATE_FROM);
  pbl_writel(XY(prv_win_width(rgb), prv_win_height(rgb)), rgb->base + RGB_UPDATE_TO);
  // The CPU address of the frame, as Espressif's own client passes it
  pbl_writel((uint32_t)rgb->vram, rgb->base + RGB_UPDATE_CONTENT);
  pbl_writel(UPDATE_STATUS_ENA, rgb->base + RGB_UPDATE_STATUS);
}

static void prv_fill_white(const struct pbl_display_qemu_esp_rgb *rgb) {
  volatile uint16_t *vram = (volatile uint16_t *)rgb->vram;
  uint32_t pixels = (uint32_t)rgb->panel_width * rgb->panel_height;

  for (uint32_t i = 0U; i < pixels; i++) {
    vram[i] = RGB565_WHITE;
  }
}

static int prv_init(const struct pbl_display *disp) {
  const struct pbl_display_qemu_esp_rgb *rgb = prv_rgb(disp);
  uint32_t size = XY(prv_win_width(rgb), prv_win_height(rgb));

  pbl_writel(BPP_16, rgb->base + RGB_BPP_VALUE);
  pbl_writel(size, rgb->base + RGB_WIN_SIZE);

  // The peripheral clamps the window to what it can show
  if (pbl_readl(rgb->base + RGB_WIN_SIZE) != size ||
      pbl_readl(rgb->base + RGB_BPP_VALUE) != BPP_16) {
    return -ENODEV;
  }

  rgb->state->rotated = false;
  prv_fill_white(rgb);
  prv_refresh(rgb);

  return 0;
}

static int prv_enable(const struct pbl_display *disp) {
  return 0;
}

static int prv_disable(const struct pbl_display *disp) {
  return 0;
}

static int prv_set_rotated(const struct pbl_display *disp, bool rotated) {
  prv_rgb(disp)->state->rotated = rotated;

  return 0;
}

static uint16_t prv_pixel(const struct pbl_display *disp, const uint8_t *row, uint32_t x) {
  if (disp->props.format == PBL_DISPLAY_FORMAT_MONO) {
    return ((row[x / 8U] >> (x % 8U)) & 1U) ? RGB565_WHITE : RGB565_BLACK;
  }

  return ((const uint16_t *)row)[x];
}

static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  const struct pbl_display_qemu_esp_rgb *rgb = prv_rgb(disp);
  const struct display_geometry g = {
    .width = disp->props.width,
    .height = disp->props.height,
    .panel_width = rgb->panel_width,
    .panel_height = rgb->panel_height,
    .scale = rgb->scale,
    .quarter = rgb->quarter,
  };
  const bool flip = rgb->state->rotated;
  const uint32_t win_width = display_geometry_scan_width(&g);
  volatile uint16_t *vram = (volatile uint16_t *)rgb->vram;

  // The window as the controller scans it; only rows y0..y1 change, the border stays white
  for (uint32_t wy = 0U; wy < display_geometry_scan_height(&g); wy++) {
    int32_t across = display_geometry_across(&g, flip, wy);

    if (across < 0) {
      continue;
    }

    for (uint32_t wx = 0U; wx < win_width; wx++) {
      int32_t along = display_geometry_along(&g, flip, wx);
      uint32_t x = (uint32_t)(g.quarter ? across : along);
      uint32_t y = (uint32_t)(g.quarter ? along : across);

      if (along >= 0 && y >= y0 && y <= y1) {
        vram[wy * win_width + wx] = prv_pixel(disp, &buf->data[y * buf->pitch], x);
      }
    }
  }

  prv_refresh(rgb);
  pbl_display_complete(disp, 0);

  return 0;
}

static int prv_clear(const struct pbl_display *disp) {
  const struct pbl_display_qemu_esp_rgb *rgb = prv_rgb(disp);

  prv_fill_white(rgb);
  prv_refresh(rgb);

  return 0;
}

const struct pbl_display_ops pbl_display_qemu_esp_rgb_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_set_rotated,
  .update = prv_update,
  .clear = prv_clear,
};

#include PBL_DT_INSTANCES(qemu_esp_rgb)
