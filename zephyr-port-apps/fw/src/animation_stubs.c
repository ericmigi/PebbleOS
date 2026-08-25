/* SPDX-License-Identifier: Apache-2.0 */

// Gap-fillers for the ported system apps. The port already ships an inert
// animation shim layer (fw/src/ui_shims.c + watchface_sandboxed/src/port.c) and
// an evented-timer-backed app_timer (fw/src/input_service.c). The Music app uses
// a few entry points those shims don't yet cover; add only those here so we don't
// double-define the existing ones.
//
// ponytail: animations resolve instantly (no tween) exactly like the rest of the
// port shim layer. app_timer_reschedule is real (wraps evented_timer). Upgrade
// path is the same as ui_shims.c: real applib animation.c once system apps run on
// a PebbleTask_App with an initialized AnimationState + frame timer.

#include <stdbool.h>
#include <stdint.h>

#include "applib/app_timer.h"
#include "applib/ui/animation.h"
#include "applib/ui/property_animation.h"
#include "pbl/services/evented_timer.h"

// --- animation entry points not in ui_shims.c / port.c ----------------------
Animation *animation_spawn_create(Animation *animation_a, Animation *animation_b,
                                  Animation *animation_c, ...) {
  (void)animation_a;
  (void)animation_b;
  (void)animation_c;
  return NULL;
}

bool animation_set_play_count(Animation *animation, uint32_t play_count) {
  (void)animation;
  (void)play_count;
  return false;
}

PropertyAnimation *property_animation_create_bounds_origin(struct Layer *layer, GPoint *from,
                                                           GPoint *to) {
  (void)layer;
  (void)from;
  (void)to;
  return NULL;
}

void property_animation_update_grect(PropertyAnimation *property_animation,
                                     const uint32_t distance_normalized) {
  (void)property_animation;
  (void)distance_normalized;
}

// --- app_timer_reschedule (port app_timer wraps evented_timer; register/cancel
// live in input_service.c, reschedule was not needed until now) --------------
bool app_timer_reschedule(AppTimer *timer_handle, uint32_t new_timeout_ms) {
  return evented_timer_reschedule((EventedTimerID)(uintptr_t)timer_handle, new_timeout_ms);
}
