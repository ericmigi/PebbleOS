/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/display/ssd1677.h>

// Shared by grey plane transfers and cursor encoding while holding the display lock.
#define UC8279_TRANSFER_BYTES 6400U
uint8_t *uc8279_x4_transfer_tile(void);

int uc8279_x4_leave_grey(const struct pbl_display_ssd1677 *epd,
                         const struct pbl_display_buffer *buf);
enum uc8279_grey_action {
  UC8279_GREY_SKIP,
  UC8279_GREY_DRAW,
  UC8279_GREY_CLEAR
};

int uc8279_x4_prepare_grey(const struct pbl_display_ssd1677 *epd,
                           const struct pbl_display_buffer *buf, enum uc8279_grey_action *action);
int uc8279_x4_finish_grey(const struct pbl_display_ssd1677 *epd,
                          const struct pbl_display_buffer *buf);
int uc8279_x4_probe(const struct pbl_display_ssd1677 *epd);
int uc8279_x4_init(const struct pbl_display_ssd1677 *epd);
int uc8279_x4_power_off(const struct pbl_display_ssd1677 *epd);
void uc8279_x4_submit(const struct pbl_display_ssd1677 *epd, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1);

// Shared SPI transport and framebuffer-to-panel rotation. A null frame fills the plane white.
int epaper_write_command(const struct pbl_display_ssd1677 *epd, uint8_t command,
                         const uint8_t *data, size_t len);
int epaper_write_frame(const struct pbl_display_ssd1677 *epd, uint8_t command,
                       const uint8_t *frame);

// Write an address-windowed UC8279 plane, excluding the hidden gate lines.
int epaper_write_window(const struct pbl_display_ssd1677 *epd, uint8_t command,
                        const uint8_t *frame, const struct pbl_display_ssd1677_window *window,
                        bool invert);
