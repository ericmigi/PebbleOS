/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

typedef struct GContext GContext;

GContext *kernel_ui_get_graphics_context(void);
GContext *graphics_context_get_current_context(void);
