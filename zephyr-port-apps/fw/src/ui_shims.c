/* SPDX-License-Identifier: Apache-2.0 */

// Link satisfiers for the shipping applib UI files reused by the launcher
// (menu_layer.c / scroll_layer.c). They cover the animation tween engine, the
// scroll content-indicator, scroll shadows and vibration — none of which are
// exercised by button navigation with animated=false selection changes. The
// real layout, selection, scroll clamping and click logic all stay in the
// shipping code; a regular (non-center-focused) menu never reaches any of the
// property_animation paths below (verified in prv_apply_selection_change()).
//
// ponytail: stubbed tween/indicator/shadow/vibe subsystems. Port the real
// animation service (animation.c + property_animation.c + a frame timer) only
// if the slide/scroll/highlight animations are wanted on hardware.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "applib/graphics/gtypes.h"
#include "applib/graphics/graphics_bitmap.h"
#include "applib/ui/animation.h"
#include "applib/ui/animation_interpolate.h"
#include "applib/ui/content_indicator.h"
#include "applib/ui/content_indicator_private.h"
#include "applib/ui/menu_cell_layer.h"
#include "applib/ui/property_animation.h"
#include "applib/ui/shadows.h"
#include "applib/ui/vibes.h"

// --- scroll content indicator (the little up/down arrows) -------------------
ContentIndicator *content_indicator_get_for_scroll_layer(ScrollLayer *scroll_layer) {
  (void)scroll_layer;
  return NULL;
}

ContentIndicator *content_indicator_get_or_create_for_scroll_layer(ScrollLayer *scroll_layer) {
  (void)scroll_layer;
  return NULL;
}

void content_indicator_destroy_for_scroll_layer(ScrollLayer *scroll_layer) {
  (void)scroll_layer;
}

void content_indicator_set_content_available(ContentIndicator *content_indicator,
                                             ContentIndicatorDirection direction, bool available) {
  (void)content_indicator;
  (void)direction;
  (void)available;
}

// Real applib animation engine (animation.c / property_animation.c /
// animation_interpolate.c) is linked now; the inert tween shims are gone.
// --- misc cosmetic bits -----------------------------------------------------
// menu_cell_basic_cell_height() and the other menu-cell draw helpers come from
// the real menu_layer_system_cells.c.

void vibes_enqueue_custom_pattern(VibePattern pattern) {
  (void)pattern;
}

// graphics_draw_bitmap_in_rect() now comes from the real applib graphics_bitmap.c
// (added to the build) so the launcher can draw app icons + the status-bar BT
// glyph. It was previously a no-op because icons were cosmetic.

GBitmap *shadow_get_top(void) {
  return NULL;
}

GBitmap *shadow_get_bottom(void) {
  return NULL;
}
