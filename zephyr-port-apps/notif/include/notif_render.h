/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

// Shared PebbleOS notification render path: build the notification card via the
// real layout_create -> layer_render_tree pipeline and push the framebuffer to
// the JDI panel. Used by the standalone notif app and folded into the BLE app so
// a live CoreApp notification lands on the same display.
//
// This header stays free of PebbleOS timeline types on purpose: callers live in
// the BLE include environment (main.c, ppog_min.c) which forcibly includes
// nimble/zephyr compat headers that clash with the pebble graphics headers.
// TimelineItem handling stays inside render.c (the notif include environment).

// Idempotent: framebuffer + graphics context + fonts + generic icon.
void notif_render_init(void);

// Render a hardcoded synthetic notification (Phase 1 bring-up: proves the render
// path draws a card without any live delivery). Left up indefinitely.
void notif_render_demo(void);

// Phase 2: parse a blob_db notification value (a serialized TimelineItem:
// SerializedTimelineItemHeader followed by its payload) and render it. Returns 0
// on success. Takes raw bytes so BLE-side callers need no pebble types.
int notif_render_blob_db_value(const uint8_t *value, uint16_t value_len);
