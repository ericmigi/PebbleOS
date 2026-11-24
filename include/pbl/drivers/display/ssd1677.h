/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/display.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/spi.h>
#include <pbl/kernel/timer.h>

/**
 * @defgroup drivers_display_ssd1677 Solomon SSD1677
 * @ingroup drivers_display
 * @brief @ref drivers_display driver for the SSD1677 e-paper controller, an SPI device with a
 * data/command line.
 *
 * The display takes @ref PBL_DISPLAY_FORMAT_MONO framebuffers of the panel's size or smaller,
 * scaled up by a whole number and centred on a white panel, turned by the mounting rotation; the
 * geometry is that of the QEMU ESP RGB display. The driver keeps a copy of the last frame. An
 * update writes the controller RAM the changed rows cover and starts a refresh: a fast one, which
 * compares the new plane with the old, or every full-refresh-interval-th and the first after init
 * a half one, the stock firmware's clean refresh, with the new frame in both planes. The fast
 * refresh is the panel's own OTP waveform, or with fast-frames set the same waveform with a main
 * phase of that many frames, loaded into the controller. Updates complete from the BUSY interrupt
 * at the end of the refresh, or with @c -ETIMEDOUT if it does not end; a controller still busy at
 * the next update is reset. The panel stays powered after a fast refresh until the display is
 * disabled.
 *
 * @{
 */

/** @cond INTERNAL_HIDDEN */
// Controller RAM: bytes x0..x1 of rows y0..y1, in the controller's own scan
struct pbl_display_ssd1677_window {
  uint16_t x0;
  uint16_t x1;
  uint16_t y0;
  uint16_t y1;
};

struct pbl_display_ssd1677_state {
  const struct pbl_display *disp;
  struct pbl_irq_spec busy_irq;
  struct pbl_timer timer;
  volatile bool refreshing;
  bool flip;
  bool need_half;
  // Clock and analog on
  bool powered;
  // The new plane is a frame behind in stale
  bool stale_valid;
  uint16_t updates;
  struct pbl_display_ssd1677_window stale;
};
/** @endcond */

/** @brief SSD1677 display. */
struct pbl_display_ssd1677 {
  /** Display. */
  struct pbl_display disp;
  /** Driver runtime state. */
  struct pbl_display_ssd1677_state *state;
  /** SPI device. */
  struct pbl_spi_dev spi;
  /** Data/command output, high for data. */
  struct pbl_gpio dc;
  /** Reset output. */
  struct pbl_gpio reset;
  /** BUSY input. */
  struct pbl_gpio busy;
  /** Copy of the last frame. */
  uint8_t *shadow;
  /** Framebuffer coordinate along each controller row, by column, or -1. */
  int16_t *along;
  /** One controller row. */
  uint8_t *row;
  /** Panel width as mounted, in pixels. */
  uint16_t panel_width;
  /** Panel height as mounted, in pixels. */
  uint16_t panel_height;
  /** Panel pixels per framebuffer pixel, in each direction. */
  uint8_t scale;
  /** Panel mounted a quarter turn. */
  bool quarter;
  /** Updates from one full refresh to the next, 0 for none. */
  uint16_t full_interval;
  /** Main phase of the fast waveform, in frames, 0 for the panel's own. */
  uint8_t fast_frames;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_display_ops pbl_display_ssd1677_ops;

#define SSD1677_SCAN_WIDTH(_pw, _ph, _rot) (((_rot) % 180) ? (_ph) : (_pw))
// Controller rows sent per SPI transfer
#define SSD1677_ROWS_PER_WRITE 8
/** @endcond */

/**
 * @brief Define an SSD1677 display.
 *
 * @param sym Symbol of the display.
 * @param _name Name.
 * @param _bus SPI bus.
 * @param _cs Chip select number.
 * @param _max_hz Clock frequency, in Hz.
 * @param _mode SPI mode flags, @c PBL_SPI_*.
 * @param _dc Data/command output, from PBL_GPIO().
 * @param _reset Reset output, from PBL_GPIO().
 * @param _busy BUSY input, from PBL_GPIO().
 * @param _width Framebuffer width, in pixels.
 * @param _height Framebuffer height, in pixels.
 * @param _panel_width Panel width as mounted, in pixels.
 * @param _panel_height Panel height as mounted, in pixels.
 * @param _scale Panel pixels per framebuffer pixel, in each direction.
 * @param _rotation Panel mounting rotation, counter-clockwise: 0, 90, 180 or 270 degrees.
 * @param _full_interval Updates from one full refresh to the next, 0 for none.
 * @param _fast_frames Main phase of the fast waveform, in frames, 0 for the panel's own.
 */
#define PBL_DISPLAY_SSD1677_DEFINE(sym, _name, _bus, _cs, _max_hz, _mode, _dc, _reset, _busy,  \
                                   _width, _height, _panel_width, _panel_height, _scale,       \
                                   _rotation, _full_interval, _fast_frames)                    \
  static_assert((_rotation) % 90 == 0 && (_rotation) < 360, "rotation of 0, 90, 180 or 270");  \
  static_assert((_scale) >= 1 && (_width) * (_scale) <= (_panel_width) &&                      \
                    (_height) * (_scale) <= (_panel_height),                                   \
                "scaled framebuffer larger than the panel");                                   \
  static_assert(SSD1677_SCAN_WIDTH(_panel_width, _panel_height, _rotation) % 8 == 0,           \
                "controller rows of whole bytes");                                             \
  static_assert((_fast_frames) <= 255, "fast waveform main phase of up to 255 frames");        \
  static struct pbl_display_ssd1677_state sym##_state;                                         \
  static uint8_t sym##_shadow[((_width) + 7) / 8 * (_height)];                                 \
  static int16_t sym##_along[SSD1677_SCAN_WIDTH(_panel_width, _panel_height, _rotation)];      \
  static uint8_t sym##_row[SSD1677_SCAN_WIDTH(_panel_width, _panel_height, _rotation) / 8 *    \
                           SSD1677_ROWS_PER_WRITE];                                            \
  PBL_DISPLAY_STATE_DEFINE(sym);                                                               \
  PBL_DEVICE_INSTANCE(sym, struct pbl_display_ssd1677, struct pbl_display, disp) {             \
    .disp = PBL_DISPLAY_INIT(sym, _name, nullptr, &pbl_display_ssd1677_ops, _width, _height,   \
                             PBL_DISPLAY_FORMAT_MONO, (_rotation) >= 180),                     \
    .state = &sym##_state, .spi = PBL_SPI_DEV(_bus, _cs, _max_hz, _mode), .dc = _dc,           \
    .reset = _reset, .busy = _busy, .shadow = sym##_shadow, .along = sym##_along,              \
    .row = sym##_row, .panel_width = (_panel_width), .panel_height = (_panel_height),          \
    .scale = (_scale), .quarter = ((_rotation) % 180) != 0, .full_interval = (_full_interval), \
    .fast_frames = (_fast_frames),                                                             \
  }

/** @} */
