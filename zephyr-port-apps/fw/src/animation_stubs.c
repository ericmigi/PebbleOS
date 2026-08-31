/* SPDX-License-Identifier: Apache-2.0 */

// Residual glue next to the real applib animation engine (animation.c /
// property_animation.c / animation_interpolate.c + the animation service).

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "applib/app_timer.h"
#include "applib/ui/animation.h"
#include "applib/ui/animation_private.h"
#include "applib/ui/property_animation.h"
#include "applib/ui/property_animation_private.h"
#include "pbl/services/evented_timer.h"
#include "system/passert.h"

// applib_malloc.auto.c is not part of the port build; the engine allocates
// through these generated-symbol hooks. The applib heap lazily inits on first
// use (the engine frees with applib_free, so the allocator must match).
void *applib_malloc(size_t size);
void *_applib_type_malloc_AnimationPrivate(void) {
  return applib_malloc(sizeof(AnimationPrivate));
}

void *_applib_type_malloc_PropertyAnimationPrivate(void) {
  return applib_malloc(sizeof(PropertyAnimationPrivate));
}

void *_applib_type_malloc_AnimationAuxState(void) {
  return applib_malloc(sizeof(AnimationAuxState));
}

struct AnimationLegacy2Scheduler;
void animation_legacy2_private_init_scheduler(struct AnimationLegacy2Scheduler *s) {
  (void)s;
}

// Legacy2 (SDK 2.x) animations never run in the port; the real applib
// animation.c/property_animation.c only reach these for 2.x-SDK processes.
#define LEGACY2_STUB(ret, name, args)   ret name args { PBL_CROAK("legacy2 animation unsupported"); }
struct AnimationLegacy2;
struct PropertyAnimationLegacy2;
struct AnimationLegacy2Implementation;
struct AnimationLegacy2Handlers;
struct Layer;
struct GRect;
LEGACY2_STUB(struct AnimationLegacy2 *, animation_legacy2_create, (void))
LEGACY2_STUB(void, animation_legacy2_destroy, (struct AnimationLegacy2 *a))
LEGACY2_STUB(bool, animation_legacy2_is_scheduled, (struct AnimationLegacy2 *a))
LEGACY2_STUB(void, animation_legacy2_schedule, (struct AnimationLegacy2 *a))
LEGACY2_STUB(void, animation_legacy2_set_curve, (struct AnimationLegacy2 *a, uint8_t curve))
LEGACY2_STUB(void, animation_legacy2_set_custom_curve, (struct AnimationLegacy2 *a, void *f))
LEGACY2_STUB(void, animation_legacy2_set_delay, (struct AnimationLegacy2 *a, uint32_t ms))
LEGACY2_STUB(void, animation_legacy2_set_duration, (struct AnimationLegacy2 *a, uint32_t ms))
LEGACY2_STUB(void, animation_legacy2_set_handlers, (struct AnimationLegacy2 *a,
             struct AnimationLegacy2Handlers *h, void *ctx))
LEGACY2_STUB(void, animation_legacy2_set_implementation, (struct AnimationLegacy2 *a,
             const struct AnimationLegacy2Implementation *impl))
LEGACY2_STUB(void, animation_legacy2_unschedule, (struct AnimationLegacy2 *a))
LEGACY2_STUB(struct PropertyAnimationLegacy2 *, property_animation_legacy2_create,
             (const void *impl, void *subject, void *from, void *to))
LEGACY2_STUB(struct PropertyAnimationLegacy2 *, property_animation_legacy2_create_layer_frame,
             (struct Layer *layer, struct GRect *from, struct GRect *to))
LEGACY2_STUB(void, property_animation_legacy2_init,
             (struct PropertyAnimationLegacy2 *pa, const void *impl, void *subject,
              void *from, void *to))
LEGACY2_STUB(void, property_animation_legacy2_update_gpoint,
             (struct PropertyAnimationLegacy2 *pa, const uint32_t d))
LEGACY2_STUB(void, property_animation_legacy2_update_grect,
             (struct PropertyAnimationLegacy2 *pa, const uint32_t d))
LEGACY2_STUB(void, property_animation_legacy2_update_int16,
             (struct PropertyAnimationLegacy2 *pa, const uint32_t d))

// --- app_timer entry points (evented_timer-backed, unrelated to the engine) --
bool app_timer_reschedule(AppTimer *timer_handle, uint32_t new_timeout_ms) {
  return evented_timer_reschedule((EventedTimerID)(uintptr_t)timer_handle, new_timeout_ms);
}

AppTimer *app_timer_register_repeatable(uint32_t timeout_ms, AppTimerCallback callback,
                                        void *callback_data, bool repeating) {
  return (AppTimer *)(uintptr_t)evented_timer_register(timeout_ms, repeating, callback,
                                                       callback_data);
}
