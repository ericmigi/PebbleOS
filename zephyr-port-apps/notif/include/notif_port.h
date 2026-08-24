/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "applib/graphics/gcontext.h"

void notif_port_init(GContext *context, FrameBuffer *framebuffer);
void notif_port_fonts_init(void);
uint8_t *notif_port_framebuffer_bytes(size_t *size_out, uint16_t *stride_out);
