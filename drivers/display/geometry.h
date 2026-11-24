/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * A framebuffer scaled up by a whole number and centred on a panel, as mounted, and the panel's
 * own scan: its controller's rows, turned a quarter from the mounted panel when it is mounted a
 * quarter turn. Maps the controller's pixels back to the framebuffer's, so a driver walks its own
 * rows in order. Mounted a quarter turn, controller row n is panel column n (the mounted panel's x)
 * and controller column n is panel row height - 1 - n; flip turns the image a further 180 degrees.
 */
struct display_geometry {
  // Framebuffer size
  uint16_t width;
  uint16_t height;
  // Panel size as mounted
  uint16_t panel_width;
  uint16_t panel_height;
  // Panel pixels per framebuffer pixel, in each direction
  uint8_t scale;
  // Mounted a quarter turn: the controller's rows are the panel's columns
  bool quarter;
};

static inline uint16_t display_geometry_scan_width(const struct display_geometry *g) {
  return g->quarter ? g->panel_height : g->panel_width;
}

static inline uint16_t display_geometry_scan_height(const struct display_geometry *g) {
  return g->quarter ? g->panel_width : g->panel_height;
}

// Framebuffer coordinate at panel coordinate n of a panel_len axis, or -1 on the white border
static inline int32_t display_geometry_axis(uint32_t n, uint32_t panel_len, bool reverse,
                                            uint32_t fb_len, uint32_t scale) {
  uint32_t m = reverse ? panel_len - 1U - n : n;
  uint32_t off = (panel_len - fb_len * scale) / 2U;

  if (m < off || m - off >= fb_len * scale) {
    return -1;
  }

  return (int32_t)((m - off) / scale);
}

// Framebuffer coordinate along controller rows at column cx: y if mounted a quarter turn, else x
static inline int32_t display_geometry_along(const struct display_geometry *g, bool flip,
                                             uint32_t cx) {
  return g->quarter ? display_geometry_axis(cx, g->panel_height, !flip, g->height, g->scale)
                    : display_geometry_axis(cx, g->panel_width, flip, g->width, g->scale);
}

// Framebuffer coordinate across controller rows at row cy: x if mounted a quarter turn, else y
static inline int32_t display_geometry_across(const struct display_geometry *g, bool flip,
                                              uint32_t cy) {
  return g->quarter ? display_geometry_axis(cy, g->panel_width, flip, g->width, g->scale)
                    : display_geometry_axis(cy, g->panel_height, flip, g->height, g->scale);
}
