/* SPDX-License-Identifier: Apache-2.0 */

// Minimal service stubs for the ported privileged system apps (Music, Alarms).
// These let the real app code launch and render its real UI without pulling the
// full service closure (music now-playing, phone image-fetch, prefs, i18n,
// theming, vibes, accel). Each returns an inert / empty state.
//
// ponytail: these are "no live data" ceilings, not fakes of the UI. Upgrade path
// per group is called out inline; the real services live under src/fw/services
// and pbl/services and can be ported incrementally, replacing one stub group at
// a time.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <time.h>

#include "applib/accel_service.h"
#include "applib/app_launch_reason.h"
#include "applib/fonts/fonts.h"
#include "applib/platform.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/window.h"
#include "applib/ui/window_stack.h"
#include "pbl/services/clock.h"
#include "pbl/services/i18n/i18n.h"
#include "pbl/services/imaging.h"
#include "pbl/services/music.h"
#include "pbl/services/vibes/vibe_score.h"
#include "process_management/process_manager.h"
#include "shell/prefs.h"
#include "shell/system_theme.h"

#include "kernel/pbl_malloc.h"

// Provided by fw/src/port.c.
time_t rtc_get_time(void);

// ---------------------------------------------------------------------------
// shell prefs: no persisted prefs DB in the port; return conservative defaults.
// Album art off keeps the Music app on its text/stock layout (no imaging).
// ponytail: back these with the real watch_app_prefs_db.
// ---------------------------------------------------------------------------
bool shell_prefs_get_music_show_album_art(void) { return false; }
bool shell_prefs_get_music_show_progress_bar(void) { return false; }
bool shell_prefs_get_music_show_volume_controls(void) { return true; }

// ---------------------------------------------------------------------------
// accel tap service: no accelerometer wired in the port. Subscribing is a no-op
// (the Music app only uses taps to briefly reveal the progress bar).
// ponytail: wire the real accel_service for tap-to-reveal.
// ---------------------------------------------------------------------------
void accel_tap_service_subscribe(AccelTapHandler handler) { (void)handler; }
void accel_tap_service_unsubscribe(void) {}

// ---------------------------------------------------------------------------
// vibes: no haptics service. Creating a score returns NULL and do_vibe is a
// no-op (both are null-safe in the app).
// ponytail: port services/vibes for haptic feedback.
// ---------------------------------------------------------------------------
VibeScore *vibe_score_create_with_resource(uint32_t resource_id) {
  (void)resource_id;
  return NULL;
}

void vibe_score_do_vibe(VibeScore *score) { (void)score; }

// ---------------------------------------------------------------------------
// imaging (phone album-art fetch): report unsupported so no art is requested.
// ponytail: port pbl/services/imaging + the comm endpoint for real album art.
// ---------------------------------------------------------------------------
bool imaging_is_type_supported(ImagingImageType image_type) {
  (void)image_type;
  return false;
}

bool imaging_request_album_art(uint8_t token, ImagingFormat format, uint16_t width, uint16_t height,
                               const char *title, const char *artist) {
  (void)token;
  (void)format;
  (void)width;
  (void)height;
  (void)title;
  (void)artist;
  return false;
}

// ---------------------------------------------------------------------------
// Music service: no connected phone media session. Report "needs user to start
// playback on phone" so the Music app renders its real no-music window.
// ponytail: port pbl/services/music (now-playing state over PP media endpoint).
// ---------------------------------------------------------------------------
void music_get_now_playing(char *title, char *artist, char *album) {
  if (title) {
    title[0] = '\0';
  }
  if (artist) {
    artist[0] = '\0';
  }
  if (album) {
    album[0] = '\0';
  }
}

bool music_has_now_playing(void) { return false; }

bool music_get_player_name(char *player_name_out) {
  if (player_name_out) {
    player_name_out[0] = '\0';
  }
  return false;
}

uint32_t music_get_ms_since_pos_last_updated(void) { return 0; }

void music_get_pos(uint32_t *track_pos_ms, uint32_t *track_length_ms) {
  if (track_pos_ms) {
    *track_pos_ms = 0;
  }
  if (track_length_ms) {
    *track_length_ms = 0;
  }
}

int32_t music_get_playback_rate_percent(void) { return 0; }

uint8_t music_get_volume_percent(void) { return 0; }

MusicPlayState music_get_playback_state(void) { return MusicPlayStateInvalid; }

bool music_is_playback_state_reporting_supported(void) { return false; }

bool music_is_progress_reporting_supported(void) { return false; }

bool music_is_volume_reporting_supported(void) { return false; }

void music_command_send(MusicCommand command) { (void)command; }

bool music_is_command_supported(MusicCommand command) {
  (void)command;
  return false;
}

bool music_skip_seeks_within_track(void) { return false; }

bool music_needs_user_to_start_playback_on_phone(void) { return true; }

void music_request_reduced_latency(bool reduced_latency) { (void)reduced_latency; }

void music_request_low_latency_for_period(uint32_t period_seconds) { (void)period_seconds; }

const char *music_get_connected_server_debug_name(void) { return ""; }

uint8_t music_get_now_playing_generation(void) { return 0; }

bool music_album_art_is_current(void) { return false; }

const struct GBitmap *music_album_art_lock(void) { return NULL; }

void music_album_art_unlock(void) {}

// ---------------------------------------------------------------------------
// app_window_stack extras beyond the port's single-top-window model
// (app_window_stack_push / get_top_window live in watchface_sandboxed/port.c).
// The apps use these on live transitions (music: no-music <-> now-playing;
// alarms: editor <-> list) that never fire with the empty stubs above, so a
// best-effort implementation against the shared fw window stack is enough to
// launch + render the initial window.
// ponytail: model a real per-app window stack when multi-window nav matters
// (P3, alongside the PebbleTask_App upgrade in system_app.c).
// ---------------------------------------------------------------------------
void app_window_stack_insert_next(Window *window) { (void)window; }

void app_window_stack_pop_all(const bool animated) { (void)animated; }

Window *app_window_stack_pop(bool animated) {
  (void)animated;
  return NULL;
}

bool app_window_stack_remove(Window *window, bool animated) {
  (void)window;
  (void)animated;
  return false;
}

// ---------------------------------------------------------------------------
// window.c setters the Music app touches that the port's slim window layer
// (system_app.c / launcher_ui.c) does not implement. Cosmetic / input-routing
// hints with no effect in the port's single-window pump.
// ponytail: fold into a real applib/ui/window.c port if status-bar icons or
// touch-tap gating are needed.
// ---------------------------------------------------------------------------
void window_set_touch_tap_requires_action_bar(Window *window, bool requires_action_bar) {
  (void)window;
  (void)requires_action_bar;
}

void window_set_status_bar_icon(Window *window, const GBitmap *icon) {
  (void)window;
  (void)icon;
}

// ---------------------------------------------------------------------------
// Foundation shims the ported applib UI (action bar, status bar, app timer)
// pulls in that the fw scaffold did not yet need. All constant / best-effort.
// ---------------------------------------------------------------------------

// Single-process privileged port: always the build's platform.
// (process_manager_compiled_with_legacy2_sdk already lives in the port's
// watchface_sandboxed/port.c.)
PlatformType process_manager_current_platform(void) { return PBL_PLATFORM_TYPE_CURRENT; }

// No userspace boundary in the kernel-context port; a failed syscall check is a
// firmware bug, so trap loudly.
void syscall_failed(void) {
  __builtin_trap();
}

// Sub-second time: the port has no fractional RTC, so report 0 ms.
// ponytail: read the RTC sub-second counter if smooth animations need it.
uint16_t time_ms(time_t *tloc, uint16_t *out_ms) {
  if (tloc) {
    *tloc = rtc_get_time();
  }
  if (out_ms) {
    *out_ms = 0;
  }
  return 0;
}

// The status-bar clock. Formats current UTC wall-clock (matching the rest of the
// port, which renders UTC) as H:MM / HH:MM per 24h style.
void clock_copy_time_string(char *buffer, uint8_t size) {
  if (!buffer || size == 0) {
    return;
  }
  time_t now = rtc_get_time();
  struct tm time_tm;
  gmtime_r(&now, &time_tm);
  int hour = time_tm.tm_hour;
  if (!clock_is_24h_style()) {
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  snprintf(buffer, size, "%d:%02d", hour, time_tm.tm_min);
}

// No window-transition animation service in the port; the status bar never sits
// under an animating fixed status bar.
bool window_stack_is_animating_with_fixed_status_bar(WindowStack *window_stack) {
  (void)window_stack;
  return false;
}

// The port models a single visible window (launcher_ui.c), not a per-app
// WindowStack object. The one consumer (status bar) only passes the result to
// window_stack_is_animating_with_fixed_status_bar above, which ignores it.
WindowStack *app_state_get_window_stack(void) { return NULL; }

// Checked task-heap alloc (the port backs task_malloc with the app/kernel heap).
void *task_malloc_check(size_t bytes) {
  void *memory = task_malloc(bytes);
  __builtin_expect(memory != NULL || bytes == 0, 1);
  return memory;
}

// Format explicit hours:minutes honoring 24h style (Alarms list rows).
size_t clock_format_time(char *buffer, uint8_t size, int16_t hours, int16_t minutes,
                         bool add_space) {
  if (!buffer || size == 0) {
    return 0;
  }
  if (clock_is_24h_style()) {
    return (size_t)snprintf(buffer, size, "%d:%02d", hours, minutes);
  }
  int h12 = hours % 12;
  if (h12 == 0) {
    h12 = 12;
  }
  const char *suffix = (hours < 12) ? "AM" : "PM";
  return (size_t)snprintf(buffer, size, "%d:%02d%s%s", h12, minutes, add_space ? " " : "", suffix);
}

// App-launch reason: the port launches system apps from the launcher menu only.
// ponytail: wire real launch-reason plumbing if timeline-action launches matter.
AppLaunchReason app_launch_reason(void) { return APP_LAUNCH_USER; }

uint32_t app_launch_get_args(void) { return 0; }

// Would push a "turn on tracking" dialog; no-op in the port (interaction path).
void health_tracking_ui_feature_show_disabled(void) {}

// The port tracks a single visible window (launcher_ui.c); good enough to answer
// "is this window somewhere in the stack" for the alarm editor's insert logic.
bool app_window_stack_contains_window(Window *window) {
  return window && window == app_window_stack_get_top_window();
}
