/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "uc8279_x4.h"

// The panel worker owns the packed last monochrome page and cumulative changed-bit map.
void uc8279_cursor_remember(const struct pbl_display_buffer *buf, bool keep);
bool uc8279_cursor_preview(const struct pbl_display_buffer *buf);
int uc8279_cursor_seed(const struct pbl_display_ssd1677 *epd, const struct pbl_display_buffer *buf);
void uc8279_cursor_reset(void);
