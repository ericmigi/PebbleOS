/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/display.h>

/**
 * @defgroup drivers_display_qemu_esp_rgb QEMU ESP RGB display
 * @ingroup drivers_display
 * @brief @ref drivers_display driver for the RGB framebuffer of Espressif's QEMU machines.
 *
 * The peripheral shows a window of up to 800x600 RGB565 pixels from its video memory. The driver
 * emulates a panel of its own size in that window, as the panel's controller scans it: turned by
 * the mounting rotation, so the window of a panel mounted a quarter turn is sideways. The display
 * takes 1-bit or RGB565 framebuffers, of the panel's size or smaller and scaled up by a whole
 * number, centred on a white panel. Rows are expanded into the video memory and the whole window
 * is refreshed; updates complete before pbl_display_update() returns.
 * @{
 */

/** @cond INTERNAL_HIDDEN */
struct pbl_display_qemu_esp_rgb_state {
  bool rotated;
};
/** @endcond */

/** @brief QEMU ESP RGB display. */
struct pbl_display_qemu_esp_rgb {
  /** Display. */
  struct pbl_display disp;
  /** Driver runtime state. */
  struct pbl_display_qemu_esp_rgb_state *state;
  /** Register base address. */
  uintptr_t base;
  /** Video memory base address. */
  uintptr_t vram;
  /** Panel width as mounted, in pixels. */
  uint16_t panel_width;
  /** Panel height as mounted, in pixels. */
  uint16_t panel_height;
  /** Panel pixels per framebuffer pixel, in each direction. */
  uint8_t scale;
  /** Panel mounted a quarter turn: the window is panel_height wide. */
  bool quarter;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_display_ops pbl_display_qemu_esp_rgb_ops;
/** @endcond */

/**
 * @brief Define a QEMU ESP RGB display.
 *
 * @param sym Symbol of the display.
 * @param _name Name.
 * @param _base Register base address.
 * @param _vram Video memory base address.
 * @param _vram_size Video memory size, in bytes.
 * @param _width Framebuffer width, in pixels.
 * @param _height Framebuffer height, in pixels.
 * @param _panel_width Panel width as mounted, in pixels.
 * @param _panel_height Panel height as mounted, in pixels.
 * @param _scale Panel pixels per framebuffer pixel, in each direction.
 * @param _bpp Bits per pixel: 1 for monochrome, 16 for RGB565.
 * @param _rotation Panel mounting rotation, counter-clockwise: 0, 90, 180 or 270 degrees.
 */
#define PBL_DISPLAY_QEMU_ESP_RGB_DEFINE(sym, _name, _base, _vram, _vram_size, _width, _height,    \
                                        _panel_width, _panel_height, _scale, _bpp, _rotation)     \
  static_assert((_bpp) == 1 || (_bpp) == 16, "1 or 16 bits per pixel");                           \
  static_assert((_rotation) % 90 == 0 && (_rotation) < 360, "rotation of 0, 90, 180 or 270");     \
  static_assert((_scale) >= 1 && (_width) * (_scale) <= (_panel_width) &&                         \
                    (_height) * (_scale) <= (_panel_height),                                      \
                "scaled framebuffer larger than the panel");                                      \
  static_assert(((_rotation) % 180 ? (_panel_height) : (_panel_width)) <= 800 &&                  \
                    ((_rotation) % 180 ? (_panel_width) : (_panel_height)) <= 600,                \
                "window of at most 800x600");                                                     \
  static_assert((_panel_width) * (_panel_height) * 2 <= (_vram_size),                             \
                "panel larger than the video memory");                                            \
  static struct pbl_display_qemu_esp_rgb_state sym##_state;                                       \
  PBL_DISPLAY_STATE_DEFINE(sym);                                                                  \
  PBL_DEVICE_INSTANCE(sym, struct pbl_display_qemu_esp_rgb, struct pbl_display, disp) {           \
    .disp = PBL_DISPLAY_INIT(sym, _name, nullptr, &pbl_display_qemu_esp_rgb_ops, _width, _height, \
                             ((_bpp) == 1) ? PBL_DISPLAY_FORMAT_MONO : PBL_DISPLAY_FORMAT_RGB565, \
                             (_rotation) >= 180),                                                 \
    .state = &sym##_state, .base = (_base), .vram = (_vram), .panel_width = (_panel_width),       \
    .panel_height = (_panel_height), .scale = (_scale), .quarter = ((_rotation) % 180) != 0,      \
  }

/** @} */
