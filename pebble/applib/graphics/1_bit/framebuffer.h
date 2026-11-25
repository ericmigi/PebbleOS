/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! Framebuffer pixels per display pixel, in each direction
#ifdef CONFIG_FRAMEBUFFER_SCALE2
#define FRAMEBUFFER_SCALE 2
#else
#define FRAMEBUFFER_SCALE 1
#endif

#define FRAMEBUFFER_WORDS_PER_ROW ((PBL_DISPLAY_WIDTH * FRAMEBUFFER_SCALE + 31) / 32)
#define FRAMEBUFFER_SIZE_DWORDS (PBL_DISPLAY_HEIGHT * FRAMEBUFFER_SCALE * FRAMEBUFFER_WORDS_PER_ROW)

#define FRAMEBUFFER_BYTES_PER_ROW (FRAMEBUFFER_WORDS_PER_ROW * 4)
#define FRAMEBUFFER_SIZE_BYTES (PBL_DISPLAY_HEIGHT * FRAMEBUFFER_SCALE * FRAMEBUFFER_BYTES_PER_ROW)

typedef struct FrameBuffer {
  uint32_t buffer[FRAMEBUFFER_SIZE_DWORDS];
  GSize size;
  GRect dirty_rect; //<! Smallest rect covering all dirty pixels.
  bool is_dirty;
} FrameBuffer;

//! Row @p y of the stored pixels: with CONFIG_FRAMEBUFFER_SCALE2, of twice the size
uint32_t *framebuffer_get_line(FrameBuffer *f, uint16_t y);
