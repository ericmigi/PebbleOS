/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/display.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/spi.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/timer.h>
#include <pbl/kernel/work.h>

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
 * the next update is reset.
 *
 * With fast-differential, the fast waveform drives only the pixels that change, for its main phase
 * alone. Once no update has come for idle-ms, the driver can top up every pixel of the rows that
 * changed since the last top up or clean refresh, towards its colour, for topup-frames; run
 * the clean refresh that full-refresh-on-idle defers; and, after power-off-idle-ms, turn the
 * panel's analog supplies off. An update that comes meanwhile waits for that refresh to end.
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

// The fast waveform, from the devicetree: one group of phases A-D, a voltage pattern per pixel
// class, the group's frame rate and the source voltages; and the top up's main phase
struct pbl_display_ssd1677_wave {
  uint8_t phases[4];
  uint8_t vs[4];
  uint8_t frame_rate;
  uint8_t source[3];
  uint8_t topup_frames;
};

struct pbl_display_ssd1677_state {
  const struct pbl_display *disp;
  struct pbl_mutex lock;
  struct pbl_delayed_work idle;
  struct pbl_display_ssd1677_wave wave;
  struct pbl_irq_spec busy_irq;
  struct pbl_timer timer;
  volatile bool refreshing;
  // The refresh in flight is the driver's own, which completes no update
  bool own;
  bool flip;
  bool need_half;
  // Clock and analog on
  bool powered;
  // The planes may differ in stale
  bool stale_valid;
  // Rows changed by fast refreshes since the last top up or clean refresh, in dirty
  bool dirty_valid;
  uint16_t updates;
  struct pbl_display_ssd1677_window stale;
  struct pbl_display_ssd1677_window dirty;
  uint32_t idle_since_ms;
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
  /** Controller rows, for a transfer. */
  uint8_t *row;
  /** The pixels along one controller row, one byte each. */
  uint8_t *line;
  /** Pixels fast refreshes changed since the last top up; nullptr without top ups. */
  uint8_t *touched;
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
  /** Fast waveform that drives only the pixels that change. */
  bool fast_differential;
  /** Frame rate code of the fast waveform. */
  uint8_t fast_frame_rate;
  /** VSH1, VSH2 and VSL codes of the fast waveform. */
  uint8_t source[3];
  /** Main phase of the idle top up, in frames, 0 for none. */
  uint8_t topup_frames;
  /** Clean refreshes wait for the panel to be idle. */
  bool full_on_idle;
  /** Time without updates before idle work, in ms. */
  uint16_t idle_ms;
  /** Time without refreshes before the analog supplies go off, in ms, 0 for never. */
  uint32_t power_off_ms;
};

/**
 * @brief Run the panel's 4-level overlay over the image shown (experimental).
 *
 * The image shown must have every grey pixel black, from a B/W refresh. The frames are in the
 * display's framebuffer format and size; a pixel gets light grey where old is 1 and new 0, dark
 * grey where both are 1, and stays as it is where both are 0. Blocks until the refresh ends; the
 * next update is a clean one.
 *
 * @param disp SSD1677 display.
 * @param old_frame Frame for the old plane.
 * @param new_frame Frame for the new plane.
 * @param vcom VCOM code (command 0x2C): 0x30 is -1.2 V, as the stock firmware's overlay; 0x10
 *             -0.4 V, as the fast waveform.
 * @return 0 on success, or a negative errno.
 */
int pbl_display_ssd1677_grey(const struct pbl_display *disp, const uint8_t *old_frame,
                             const uint8_t *new_frame, uint8_t vcom);

/**
 * @brief Set the 4-level overlay's waveform, for tuning (experimental).
 *
 * @param light Frames towards white for light grey, 0 for the stock table.
 * @param dark Frames towards white for dark grey.
 * @param frame_rate Frame rate code of every group.
 * @param tail Undriven phases after the longer drive.
 */
void pbl_display_ssd1677_grey_tune(uint8_t light, uint8_t dark, uint8_t frame_rate, uint8_t tail);

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
 * @param _fast_differential Fast waveform that drives only the pixels that change.
 * @param _fast_frame_rate Frame rate code of the fast waveform.
 * @param _vsh1 VSH1 code of the fast waveform.
 * @param _vsh2 VSH2 code of the fast waveform.
 * @param _vsl VSL code of the fast waveform.
 * @param _topup_frames Main phase of the idle top up, in frames, 0 for none.
 * @param _full_on_idle Clean refreshes wait for the panel to be idle.
 * @param _idle_ms Time without updates before idle work, in ms.
 * @param _power_off_ms Time without refreshes before the analog supplies go off, in ms, 0 for
 *                      never.
 */
#define PBL_DISPLAY_SSD1677_DEFINE(                                                               \
    sym, _name, _bus, _cs, _max_hz, _mode, _dc, _reset, _busy, _width, _height, _panel_width,     \
    _panel_height, _scale, _rotation, _full_interval, _fast_frames, _fast_differential,           \
    _fast_frame_rate, _vsh1, _vsh2, _vsl, _topup_frames, _full_on_idle, _idle_ms, _power_off_ms)  \
  static_assert((_rotation) % 90 == 0 && (_rotation) < 360, "rotation of 0, 90, 180 or 270");     \
  static_assert((_scale) >= 1 && (_width) * (_scale) <= (_panel_width) &&                         \
                    (_height) * (_scale) <= (_panel_height),                                      \
                "scaled framebuffer larger than the panel");                                      \
  static_assert(SSD1677_SCAN_WIDTH(_panel_width, _panel_height, _rotation) % 8 == 0,              \
                "controller rows of whole bytes");                                                \
  static_assert((_fast_frames) <= 255 && (_topup_frames) <= 255, "phases of up to 255 frames");   \
  static struct pbl_display_ssd1677_state sym##_state;                                            \
  static uint8_t sym##_shadow[((_width) + 7) / 8 * (_height)];                                    \
  static int16_t sym##_along[SSD1677_SCAN_WIDTH(_panel_width, _panel_height, _rotation)];         \
  static uint8_t sym##_touched[(_topup_frames) != 0 ? ((_width) + 7) / 8 * (_height) : 1];        \
  static uint8_t sym##_line[(_width) > (_height) ? (_width) : (_height)];                         \
  static uint8_t sym##_row[SSD1677_SCAN_WIDTH(_panel_width, _panel_height, _rotation) / 8 *       \
                           SSD1677_ROWS_PER_WRITE];                                               \
  PBL_DISPLAY_STATE_DEFINE(sym);                                                                  \
  PBL_DEVICE_INSTANCE(sym, struct pbl_display_ssd1677, struct pbl_display, disp) {                \
    .disp = PBL_DISPLAY_INIT(sym, _name, nullptr, &pbl_display_ssd1677_ops, _width, _height,      \
                             PBL_DISPLAY_FORMAT_MONO, (_rotation) >= 180),                        \
    .state = &sym##_state, .spi = PBL_SPI_DEV(_bus, _cs, _max_hz, _mode), .dc = _dc,              \
    .reset = _reset, .busy = _busy, .shadow = sym##_shadow, .along = sym##_along,                 \
    .row = sym##_row, .line = sym##_line,                                                         \
    .touched = (_topup_frames) != 0 ? sym##_touched : nullptr, .panel_width = (_panel_width),     \
    .panel_height = (_panel_height), .scale = (_scale), .quarter = ((_rotation) % 180) != 0,      \
    .full_interval = (_full_interval), .fast_frames = (_fast_frames),                             \
    .fast_differential = (_fast_differential) != 0, .fast_frame_rate = (_fast_frame_rate),        \
    .source = {(_vsh1), (_vsh2), (_vsl)}, .topup_frames = (_topup_frames),                        \
    .full_on_idle = (_full_on_idle) != 0, .idle_ms = (_idle_ms), .power_off_ms = (_power_off_ms), \
  }

/** @} */
