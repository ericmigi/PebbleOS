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

// --- animation tween engine (never scheduled in the launcher) ---------------
uint32_t animation_get_duration(Animation *animation, bool include_delay, bool include_play_count) {
  (void)animation; (void)include_delay; (void)include_play_count;
  return 0;
}

bool animation_is_scheduled(Animation *animation) {
  (void)animation;
  return false;
}

Animation *animation_sequence_create(Animation *animation_a, Animation *animation_b,
                                     Animation *animation_c, ...) {
  (void)animation_b; (void)animation_c;
  return animation_a;
}

bool animation_set_auto_destroy(Animation *animation, bool auto_destroy) {
  (void)animation; (void)auto_destroy;
  return true;
}

bool animation_set_elapsed(Animation *animation, uint32_t elapsed_ms) {
  (void)animation; (void)elapsed_ms;
  return true;
}

bool animation_set_delay(Animation *animation, uint32_t delay_ms) {
  (void)animation; (void)delay_ms;
  return true;
}

bool animation_set_curve(Animation *animation, AnimationCurve curve) {
  (void)animation; (void)curve;
  return true;
}

bool animation_set_custom_interpolation(Animation *animation_h,
                                        InterpolateInt64Function interpolate_function) {
  (void)animation_h; (void)interpolate_function;
  return true;
}

bool animation_set_handlers(Animation *animation, AnimationHandlers callbacks, void *context) {
  (void)animation; (void)callbacks; (void)context;
  return true;
}

// --- property animation (never created in the launcher) ---------------------
PropertyAnimation *property_animation_create(const PropertyAnimationImplementation *implementation,
                                             void *subject, void *from_value, void *to_value) {
  (void)implementation; (void)subject; (void)from_value; (void)to_value;
  return NULL;
}

PropertyAnimation *property_animation_create_layer_frame(struct Layer *layer, GRect *from_frame,
                                                         GRect *to_frame) {
  (void)layer; (void)from_frame; (void)to_frame;
  return NULL;
}

Animation *property_animation_get_animation(PropertyAnimation *property_animation) {
  (void)property_animation;
  return NULL;
}

bool property_animation_init(PropertyAnimation *animation_h,
                             const PropertyAnimationImplementation *implementation,
                             void *subject, void *from_value, void *to_value) {
  (void)animation_h; (void)implementation; (void)subject; (void)from_value; (void)to_value;
  return false;
}

bool property_animation_subject(PropertyAnimation *property_animation, void **subject, bool set) {
  (void)property_animation; (void)subject; (void)set;
  return false;
}

bool property_animation_to(PropertyAnimation *property_animation, void *to, size_t size, bool set) {
  (void)property_animation; (void)to; (void)size; (void)set;
  return false;
}

void property_animation_update_gpoint(PropertyAnimation *property_animation,
                                      const uint32_t distance_normalized) {
  (void)property_animation; (void)distance_normalized;
}

// --- interpolation helpers (link-only) --------------------------------------
int16_t interpolate_int16(int32_t normalized, int16_t from, int16_t to) {
  (void)normalized; (void)from;
  return to;
}

int64_t interpolate_moook(int32_t normalized, int64_t from, int64_t to) {
  (void)normalized; (void)from;
  return to;
}

uint32_t interpolate_moook_duration(void) {
  return 0;
}

// --- misc cosmetic bits -----------------------------------------------------
// menu_cell_basic_cell_height() now comes from the real menu_layer_system_cells.c
// (compiled for the settings/watchfaces menus).

void vibes_enqueue_custom_pattern(VibePattern pattern) {
  (void)pattern;
}

void graphics_draw_bitmap_in_rect(GContext *ctx, const GBitmap *bitmap, const GRect *rect) {
  (void)ctx; (void)bitmap; (void)rect;
}

GBitmap *shadow_get_top(void) {
  return NULL;
}

GBitmap *shadow_get_bottom(void) {
  return NULL;
}
