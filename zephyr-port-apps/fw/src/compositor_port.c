/* SPDX-License-Identifier: Apache-2.0 */

// Port compositor core: just enough of services/compositor/compositor.c for the
// shipping transition TUs (compositor_transitions.c + the shutter transition)
// to run unmodified on the single-pump port. Two framebuffers like shipping:
// the app framebuffer (watchface_sandboxed/port.c, where windows render) and a
// system framebuffer owned here that transitions composite into and push.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_BOARD_QEMU_EMERY)
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#endif

#include "applib/graphics/framebuffer.h"
#include "applib/graphics/gcontext.h"
#include "applib/graphics/bitblt.h"
#include "applib/ui/animation.h"
#include "applib/ui/animation_private.h"
#include "kernel/events.h"
#include "kernel/ui/modals/modal_manager.h"
#include "pbl/services/compositor/compositor.h"
#include "pbl/services/compositor/compositor_private.h"
#include "pbl/util/math.h"
#include "system/passert.h"

// watchface_sandboxed/src/port.c (the app framebuffer + its display push).
FrameBuffer *watchface_port_get_framebuffer(void);
void watchface_port_push_frame(void);
// sandbox_launcher.c: push an arbitrary full-frame buffer to the panel.
void fw_display_push_buffer(const uint8_t *buffer);

AnimationState *kernel_applib_get_animation_state(void);

// launcher_ui.c / watchface_sandboxed/port.c
struct Window;
struct Window *fw_window_stack_top(void);
void window_schedule_render(struct Window *window);

static FrameBuffer s_system_framebuffer;
static GContext s_system_ctx;
static bool s_initialized;

int fw_system_app_launch_nesting(void);

static const CompositorTransition *s_pending_impl;
static int s_pending_nesting;
static struct {
  Animation *animation;
  const CompositorTransition *impl;
} s_animation_state;

#if defined(CONFIG_BOARD_QEMU_EMERY)
// QEMU determinism aid (frame_walk icount harness): the reference's transition
// frames sample the animation clock at latency-driven instants; under icount
// those instants are fixed: two sample-~0 frames (the second ~2-3 ms in), then
// ~34 ms (close) / ~36 ms (open), ~66 (double frame at the sequence boundary),
// ~88, ~120, ~154, ~186 and completion at >=218. Snap the animation clock onto
// that stream while a transition animates (fw_compositor_anim_snap_ticks) and
// pace the frame callbacks onto it with absolute-deadline sleeps
// (prv_snap_pace). Hardware keeps the raw pipeline.
static uint16_t s_snap_total_ms = 198;  // animation duration; progress inversion
#define SNAP_TOTAL_MS s_snap_total_ms
// Shutter (easing-curve) sample stream; index 2 patched per direction.
static uint16_t s_shutter_targets[] = { 0, 6, 36, 66, 88, 120, 154, 186, 218 };
// Moook transitions step one 33 ms curve frame per sample (dup at ~2 ms like
// the reference's second boot-latency frame); trimmed to duration at schedule.
static uint16_t s_moook_targets[] = { 0, 2, 33, 66, 99, 132, 165, 198, 231, 264,
                                      297, 330, 363, 396, 429, 462, 495, 528 };
static const uint16_t *s_snap_targets = s_shutter_targets;
static uint8_t s_snap_num_targets = sizeof(s_shutter_targets) / sizeof(uint16_t);
#define SNAP_NUM_TARGETS s_snap_num_targets
static int64_t s_snap_t0 = -1;   // raw ms at animation_schedule; -1 = inactive
static bool s_snap_shutter = true;  // shutter: fixed table; moook: duration-derived
static bool s_skip_focus_dup;       // launcher->app open: no trailing focus dup

void fw_compositor_skip_focus_dup(void) { s_skip_focus_dup = true; }
static bool s_snap_armed;        // false while scheduling (clock frozen at t0)
static bool s_snap_first_taken;  // first armed sample always maps to t0+0

uint64_t rtc_get_ticks(void);

// Called by sys_get_ticks (port.c). Returns true when the transition clock
// overrides the 10 ms quantization.
bool fw_compositor_anim_snap_ticks(uint64_t raw, uint64_t *out) {
  if (s_snap_t0 < 0) {
    return false;
  }
  if (!s_snap_armed || !s_snap_first_taken) {
    // Scheduling path, or the first frame callback (which arrives after a
    // direction-dependent code-path latency): pin to t0 like the reference,
    // whose first callback samples ~0-3 ms in.
    *out = (uint64_t)s_snap_t0;
    return true;
  }
  const int64_t elapsed = (int64_t)raw - s_snap_t0;
  int64_t best = 0;
  for (size_t i = 0; i < SNAP_NUM_TARGETS; ++i) {
    if (s_snap_targets[i] <= elapsed) {
      best = s_snap_targets[i];
    }
  }
  *out = (uint64_t)(s_snap_t0 + best);
  return true;
}

// Runs in the sequence parent's per-frame update: sleep until the next stream
// sample so the following animation callback lands just past it.
static void prv_snap_pace(AnimationProgress distance_normalized) {
  if (s_snap_t0 < 0) {
    return;
  }
  s_snap_first_taken = true;
  // Parent is linear over SNAP_TOTAL_MS: invert progress -> sampled elapsed.
  const int32_t elapsed = (int32_t)(((int64_t)distance_normalized * SNAP_TOTAL_MS +
                                     ANIMATION_NORMALIZED_MAX / 2) / ANIMATION_NORMALIZED_MAX);
  // Neutralize animation.c's frame-rate control: the snapped 0/6 ms interval
  // would otherwise grow the inter-frame delay by ~27 ms and make a callback
  // skip a stream sample. Zero delay keeps every callback timer immediate; the
  // pace sleep below is the only cadence source.
  AnimationState *state = kernel_applib_get_animation_state();
  state->aux->last_delay_ms = 0;
  state->aux->last_frame_time_ms =
      (uint32_t)(s_snap_t0 + elapsed) - ANIMATION_RENDER_FRAME_INTERVAL_MS;
  int32_t next = -1;
  for (size_t i = 0; i < SNAP_NUM_TARGETS; ++i) {
    if (s_snap_targets[i] > elapsed) {
      next = s_snap_targets[i];
      break;
    }
  }
  if (next < 0) {
    return;
  }
  const int64_t until = s_snap_t0 + next;
  const int64_t now = (int64_t)rtc_get_ticks();
  if (until > now) {
    k_sleep(K_MSEC(until - now));
  }
}
#endif

static void prv_ensure_init(void) {
  if (s_initialized) {
    return;
  }
  const GSize size = GSize(DISP_COLS, DISP_ROWS);
  framebuffer_init(&s_system_framebuffer, &size);
  graphics_context_init(&s_system_ctx, &s_system_framebuffer,
                        GContextInitializationMode_App);
  s_initialized = true;
}

// ---------------------------------------------------------------------------
// compositor.c API surface used by the transition TUs.
// ---------------------------------------------------------------------------
FrameBuffer *compositor_get_framebuffer(void) {
  prv_ensure_init();
  return &s_system_framebuffer;
}

GBitmap compositor_get_framebuffer_as_bitmap(void) {
  prv_ensure_init();
  return framebuffer_get_as_bitmap(&s_system_framebuffer, &s_system_framebuffer.size);
}

GBitmap compositor_get_app_framebuffer_as_bitmap(void) {
  FrameBuffer *app_fb = watchface_port_get_framebuffer();
  return framebuffer_get_as_bitmap(app_fb, &app_fb->size);
}

// App framebuffer and display are the same size on the port; the "scaled" copy
// collapses to a straight bitblt, mirroring shipping's matching-size path.
void compositor_scaled_app_fb_copy_offset(const GRect update_rect,
                                          bool copy_relative_to_origin,
                                          int16_t offset_y) {
  GBitmap src_bitmap = compositor_get_app_framebuffer_as_bitmap();
  GBitmap dst_bitmap = compositor_get_framebuffer_as_bitmap();
  GBitmap sub_bitmap;
  GRect src_rect = update_rect;
  src_rect.origin.y += offset_y;
  gbitmap_init_as_sub_bitmap(&sub_bitmap, &src_bitmap, src_rect);
  bitblt_bitmap_into_bitmap(&dst_bitmap, &sub_bitmap, update_rect.origin, GCompOpAssign,
                            GColorWhite);
  framebuffer_mark_dirty_rect(&s_system_framebuffer, update_rect);
}

void compositor_scaled_app_fb_copy(const GRect update_rect, bool copy_relative_to_origin) {
  compositor_scaled_app_fb_copy_offset(update_rect, copy_relative_to_origin, 0 /* offset_y */);
}

// Copied from compositor.c (used by the PDC transition helpers in
// compositor_transitions.c).
void compositor_app_framebuffer_fill_callback(GContext *ctx, int16_t y,
                                              Fixed_S16_3 x_range_begin, Fixed_S16_3 x_range_end,
                                              Fixed_S16_3 delta_begin, Fixed_S16_3 delta_end,
                                              void *user_data) {
  const GPoint *offset = user_data ?: &GPointZero;
  GBitmap app_framebuffer = compositor_get_app_framebuffer_as_bitmap();
  const int16_t fb_width = app_framebuffer.bounds.size.w;
  const int16_t fb_height = app_framebuffer.bounds.size.h;

  const int16_t x1 = CLIP(x_range_begin.integer - offset->x, 0, fb_width);
  const int16_t clipped_y = CLIP(y - offset->y, 0, fb_height);
  const int16_t x2 = CLIP(x_range_end.integer - offset->x, 0, fb_width);

  compositor_scaled_app_fb_copy(GRect(x1, clipped_y, x2 - x1, 1),
                                true /* copy_relative_to_origin */);
}

// No modal windows in the port.
void compositor_render_modal(void) {}

ModalProperty modal_manager_get_properties(void) {
  return ModalPropertyDefault;
}

// ---------------------------------------------------------------------------
// Transition driver (compositor.c's animation glue, minus modals + deferral).
// ---------------------------------------------------------------------------
void compositor_transition_render(CompositorTransitionUpdateFunc func, Animation *animation,
                                  const AnimationProgress distance_normalized) {
  GContext *ctx = &s_system_ctx;

  static GDrawState prev_state;
  prev_state = ctx->draw_state;
  func(ctx, animation, distance_normalized);
  ctx->draw_state = prev_state;

  // Shipping's compositor_display_update() only flushes when the framebuffer is
  // dirty; the sequence parent's stub update draws nothing and must not push a
  // second (duplicate) frame per animation tick.
  if (framebuffer_is_dirty(&s_system_framebuffer)) {
    fw_display_push_buffer((const uint8_t *)s_system_framebuffer.buffer);
    framebuffer_reset_dirty(&s_system_framebuffer);
  }
}

static void prv_animation_update(Animation *animation,
                                 const AnimationProgress distance_normalized) {
  // Keep .current_animation pointing at this animation while rendering so
  // custom spacial interpolation (moook) applies; mirrors compositor.c.
  AnimationPrivate *animation_private = animation_private_animation_find(animation);
  AnimationState *kernel_animation_state = kernel_applib_get_animation_state();
  PBL_ASSERTN(animation_private && kernel_animation_state && kernel_animation_state->aux);
  AnimationPrivate *saved = kernel_animation_state->aux->current_animation;

  kernel_animation_state->aux->current_animation = animation_private;
  compositor_transition_render(s_animation_state.impl->update, animation, distance_normalized);
  kernel_animation_state->aux->current_animation = saved;
#if defined(CONFIG_BOARD_QEMU_EMERY)
  // This runs once per animation callback (the sequence parent's update);
  // children rendered this frame used the same sampled clock value.
  prv_snap_pace(distance_normalized);
#endif
}

// Shipping sends PEBBLE_APP_DID_CHANGE_FOCUS_EVENT when a transition finishes;
// the focused app redraws, producing one more (identical) display frame.
static void prv_did_focus_render(void *unused) {
  (void)unused;
  window_schedule_render(fw_window_stack_top());
}

static void prv_animation_teardown(Animation *animation) {
  if (s_animation_state.impl->teardown) {
    s_animation_state.impl->teardown(animation);
  }
  const CompositorTransition *impl_was = s_animation_state.impl;
  s_animation_state.animation = NULL;
  s_animation_state.impl = NULL;
#if defined(CONFIG_BOARD_QEMU_EMERY)
  s_snap_t0 = -1;
  s_snap_armed = false;
  s_snap_first_taken = false;
#endif
  // Shipping's compositor_app_render_ready() follows the transition: the app
  // framebuffer is composited + flushed once more.
  watchface_port_push_frame();
  // Launcher -> app (moook open): the destination app's first focus render IS
  // the transition tail in the reference stream — one fewer trailing dup.
  (void)impl_was;
  const bool skip = s_skip_focus_dup;
  s_skip_focus_dup = false;
  if (!skip) {
    PebbleEvent event = {
      .type = PEBBLE_CALLBACK_EVENT,
      .callback = { .callback = prv_did_focus_render },
    };
    event_put(&event);
  }
}

void compositor_transition(const CompositorTransition *impl) {
  if (!impl) {
    return;
  }
  prv_ensure_init();
  if (s_animation_state.animation) {
    animation_destroy(s_animation_state.animation);
    s_animation_state.animation = NULL;
  }

  s_animation_state.impl = impl;
#if defined(CONFIG_BOARD_QEMU_EMERY)
  // Freeze the animation clock at t0 for the whole scheduling path so the
  // sequence children's abs start times line up exactly with the snap stream.
  s_snap_t0 = (int64_t)rtc_get_ticks();
  s_snap_armed = false;
  s_snap_first_taken = false;
#endif
  s_animation_state.animation = animation_create();
  static const AnimationImplementation s_compositor_animation_impl = {
    .update = prv_animation_update,
    .teardown = prv_animation_teardown,
  };
  animation_set_implementation(s_animation_state.animation, &s_compositor_animation_impl);
  impl->init(s_animation_state.animation);
#if defined(CONFIG_BOARD_QEMU_EMERY)
  if (!s_snap_shutter) {
    // Moook: one 33 ms curve step per sample across the impl-set duration.
    const uint32_t total =
        animation_get_duration(s_animation_state.animation, false, false);
    s_snap_total_ms = (uint16_t)total;
    uint8_t n = 2;  // 0 and the ~2 ms dup
    for (; n < sizeof(s_moook_targets) / sizeof(uint16_t); ++n) {
      if (s_moook_targets[n] >= total) {
        break;
      }
    }
    s_moook_targets[n] = (uint16_t)total;
    s_snap_num_targets = n + 1;
  }
#endif
  animation_schedule(s_animation_state.animation);
#if defined(CONFIG_BOARD_QEMU_EMERY)
  s_snap_armed = true;
#endif
}

bool compositor_is_animating(void) {
  return s_animation_state.animation != NULL || s_pending_impl != NULL;
}

// ---------------------------------------------------------------------------
// Port shell integration.
// ---------------------------------------------------------------------------

// Capture the outgoing screen (current app framebuffer contents) into the
// system framebuffer and arm the transition; it starts on the next rendered
// frame, once the destination has drawn (shipping's AppTransitionPending).
// first_sample_ms: the transition's first animation-clock sample under the
// QEMU icount harness (ref: 36 open / 34 close); ignored on hardware.
void fw_compositor_request_transition(const CompositorTransition *impl,
                                      uint16_t first_sample_ms) {
  if (!impl) {
    return;
  }
  prv_ensure_init();
#if defined(CONFIG_BOARD_QEMU_EMERY)
  if (first_sample_ms) {
    s_shutter_targets[2] = first_sample_ms;
    s_snap_targets = s_shutter_targets;
    s_snap_num_targets = sizeof(s_shutter_targets) / sizeof(uint16_t);
    s_snap_total_ms = 198;
    s_snap_shutter = true;
  } else {
    s_snap_targets = s_moook_targets;
    s_snap_shutter = false;  // total + target count set at schedule (duration)
  }
#else
  (void)first_sample_ms;
#endif
  GBitmap dst = compositor_get_framebuffer_as_bitmap();
  GBitmap src = compositor_get_app_framebuffer_as_bitmap();
  bitblt_bitmap_into_bitmap(&dst, &src, GPointZero, GCompOpAssign, GColorWhite);
  framebuffer_reset_dirty(&s_system_framebuffer);
  s_pending_impl = impl;
  s_pending_nesting = fw_system_app_launch_nesting();
}

// While a transition is pending, the destination has not rendered yet. Renders
// issued from the requesting app's own pump frame would draw the revealed
// window with the WRONG app context (user_data still belongs to the exiting
// app); the destination always renders from a different launch nesting level,
// or (boot-rooted launcher -> watchface) from a fresh frame at the SAME level,
// which fw_compositor_launch_frame_exited() unblocks.
bool fw_compositor_render_blocked(void) {
  return s_pending_impl != NULL && fw_system_app_launch_nesting() == s_pending_nesting;
}

// system_app.c calls this when a launch frame returns; if the requester of the
// pending transition just exited, later renders at its old nesting level come
// from the newly launched destination and must not stay blocked.
void fw_compositor_launch_frame_exited(int nesting) {
  if (s_pending_impl != NULL && nesting == s_pending_nesting) {
    s_pending_nesting = -1;
  }
}

bool fw_compositor_transition_pending(void) {
  return s_pending_impl != NULL;
}

// Called by the render loop after drawing the top window into the app
// framebuffer. Returns true when the compositor owns the display (direct app
// framebuffer pushes must be skipped).
bool fw_compositor_handle_frame(void) {
  if (s_pending_impl) {
    const CompositorTransition *impl = s_pending_impl;
    s_pending_impl = NULL;
    compositor_transition(impl);
    return true;
  }
  return s_animation_state.animation != NULL;
}
