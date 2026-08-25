/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

bool fw_sandbox_launch(void);
void fw_sandbox_exit(void);

// Initialise the panel + applib graphics shell (framebuffer, GContext, fonts,
// app heap) and turn the display on. Safe to call more than once.
void fw_sandbox_display_init(void);

// Emit the current framebuffer over the console UART, framed (FB_BEGIN/FB/
// FB_END) with a CRC-32 so the host tool can reconstruct a PNG. No-op until
// the display is initialised.
void fw_fb_dump_uart(void);
