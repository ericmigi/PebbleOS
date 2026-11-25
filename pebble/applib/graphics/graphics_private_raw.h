/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "gtypes.h"

extern const GDrawRawImplementation g_default_draw_implementation;

void graphics_private_raw_blend_color_factor(const GContext *ctx, GColor *dst_color,
                                             unsigned int data_offset, GColor src_color, int x,
                                             uint8_t factor);

#ifdef CONFIG_FRAMEBUFFER_SCALE2
//! Fills @p rect (clipped, in display pixels) of a 1-bit framebuffer at twice its size with
//! @p color, dithered as the row fills are
void graphics_private_fill_rect_scaled2(GBitmap *framebuffer, GRect rect, GColor color);
#endif
