/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

bool fw_sandbox_launch(void);

// Initialise the panel + applib graphics shell (framebuffer, GContext, fonts,
// app heap) and turn the display on. Safe to call more than once.
void fw_sandbox_display_init(void);
