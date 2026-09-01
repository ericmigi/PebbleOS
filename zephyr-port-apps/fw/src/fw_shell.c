/* SPDX-License-Identifier: Apache-2.0 */

// Real-shell boot flow (FW_REAL_SHELL, qemu_emery): mirror the shipping normal
// shell — boot into the TicToc watchface, SELECT opens the REAL launcher app
// (apps/system/launcher/default), BACK returns to the watchface, and exiting an
// app launched from the launcher returns to the launcher (selection persisted
// by launcher.c itself).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Zephyr and Pebble both declare sign_extend(); same dance as launcher_ui.c.
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend

#include "applib/ui/window.h"
#include "apps/system/launcher/default/launcher.h"
#include "apps/watch/tictoc/tictoc.h"
#include "kernel/events.h"
#include "kernel/pebble_tasks.h"
#include "pbl/drivers/button_id.h"
#include "process_management/app_manager.h"
#include "process_management/pebble_process_md.h"

#include "pbl/services/compositor/compositor.h"
#include "pbl/services/compositor/default/compositor_shutter_transitions.h"

#include "launcher_ui.h"
#include "system_app.h"

void fw_sandbox_display_init(void);
void fw_compositor_request_transition(const CompositorTransition *impl,
                                      uint16_t first_sample_ms);
const struct CompositorTransition *compositor_launcher_app_transition_get(
    bool app_is_destination);

// True while the launcher app owns the screen (from SELECT-open until its root
// window pops back to the watchface).
static bool s_launcher_active;

// ---------------------------------------------------------------------------
// app_manager task context: carries the launcher's args (reset_scroll).
// ---------------------------------------------------------------------------
static LauncherMenuArgs s_launcher_args;
static ProcessContext s_task_context;

ProcessContext *app_manager_get_task_context(void) { return &s_task_context; }

// Shipping (shell/normal/app_idle_timeout.c): the launcher returns to the
// watchface after 30 s without input, via the watchface close transition.
#include "pbl/services/new_timer/new_timer.h"
static TimerID s_idle_timer = TIMER_INVALID_ID;

static void prv_idle_close_launcher(void *data) {
  (void)data;
  if (s_launcher_active && !fw_shell_launch_pending()) {
    fw_window_stack_pop();  // launcher root; before_pop_render requests the shutter
  }
}

static void prv_idle_timeout_expired(void *data) {
  (void)data;
  PebbleEvent event = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = { .callback = prv_idle_close_launcher },
  };
  event_put(&event);
}

void app_idle_timeout_start(void) {
  if (s_idle_timer == TIMER_INVALID_ID) {
    s_idle_timer = new_timer_create();
  }
  new_timer_start(s_idle_timer, 30000, prv_idle_timeout_expired, NULL, 0);
}

void app_idle_timeout_stop(void) {
  if (s_idle_timer != TIMER_INVALID_ID) {
    new_timer_stop(s_idle_timer);
  }
}

// Any button press restarts the countdown while the launcher owns the screen.
void fw_shell_note_activity(void) {
  if (s_launcher_active && s_idle_timer != TIMER_INVALID_ID) {
    new_timer_start(s_idle_timer, 30000, prv_idle_timeout_expired, NULL, 0);
  }
}

// menu_layer.c defers the actual launch until after the last menu frame renders
// by posting a callback to the app task; our apps share the KernelMain pump, so
// it lands on the shared event queue's callback path.
void process_manager_send_callback_event_to_process(PebbleTask task, void (*callback)(void *),
                                                    void *data) {
  (void)task;
  PebbleEvent event = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = { .callback = callback, .data = data },
  };
  event_put(&event);
}


// ---------------------------------------------------------------------------
// Shell flow.
// ---------------------------------------------------------------------------
static void prv_request_launcher(bool reset_scroll) {
  s_launcher_args = (LauncherMenuArgs) { .reset_scroll = reset_scroll };
  s_task_context.args = &s_launcher_args;
  fw_shell_request_launch(launcher_menu_app_get_app_info());
}

// A watchface never configures clicks; the shell owns its buttons. SELECT opens
// the launcher, UP/DOWN are the (unported) timeline.
bool fw_shell_handle_button_down(ButtonId button_id) {
  Window *top = fw_window_stack_top();
  if (!top || top->click_config_provider != NULL) {
    return false;
  }
  if (button_id == BUTTON_ID_SELECT && !fw_shell_launch_pending()) {
    // Shipping (shell.c): watchface -> launcher opens with the white shutter
    // sliding right; captured now, started once the launcher renders.
    fw_compositor_request_transition(
        compositor_shutter_transition_get(CompositorTransitionDirectionRight, GColorWhite),
        36 /* ref first anim sample, open (see compositor_port.c) */);
    s_launcher_active = true;
    prv_request_launcher(true /* reset_scroll */);
  }
  return true;
}

// Exiting an app that was launched from the launcher returns to the launcher
// (shipping relaunches it with its persisted selection). Exiting the launcher
// itself, or a watchface, falls back to the watchface pump.
// BACK out of the launcher to the watchface closes with the shutter sliding
// left (shipping shell.c, launcher -> watchface).
void fw_shell_before_pop_render(Window *window, int new_depth) {
  (void)window;
  // new_depth 1: launcher opened over the watchface. new_depth 0: the
  // boot-rooted launcher closing to the (about to launch) watchface.
  if (s_launcher_active && new_depth <= 1) {
    s_launcher_active = false;
    app_idle_timeout_stop();
    // SELECT-launch also pops the launcher; the launcher-app transition is
    // already pending then — don't overwrite it with the watchface shutter.
    if (!fw_shell_launch_pending()) {
      fw_compositor_request_transition(
          compositor_shutter_transition_get(CompositorTransitionDirectionLeft, GColorWhite),
          34 /* ref first anim sample, close */);
    }
  }
}

// The boot-rooted launcher sits alone at depth 1; BACK must still close it
// (the pump only auto-pops at depth > 1 to protect the watchface root).
bool fw_shell_back_should_pop(void) { return s_launcher_active; }

void fw_shell_on_app_exit(const PebbleProcessMd *md) {
  if (md == launcher_menu_app_get_app_info() ||
      md->process_type == ProcessTypeWatchface ||
      fw_shell_launch_pending()) {
    return;
  }
  // Shipping: app -> launcher reverses the launcher-app moook slide.
  fw_compositor_request_transition(
      (const CompositorTransition *)compositor_launcher_app_transition_get(
          false /* app_is_destination */), 0);
  prv_request_launcher(false /* reset_scroll */);
}

#if defined(CONFIG_BOARD_QEMU_EMERY)
// QEMU parity aid: the reference's per-event app render loop flushes the boot
// launcher 3 extra (pixel-identical) times ~25 ms apart (will-focus/did-focus/
// service events each end in a render pass); the single-pump port coalesces to
// one. Queue callback events that each force one more render pass.
static void prv_boot_dup_render(void *unused) {
  (void)unused;
  extern void window_schedule_render(Window *window);
  window_schedule_render(fw_window_stack_top());
}
#endif

void fw_launcher_ui_run(void) {
  extern void pebble_zephyr_core_event_loop_init(void);
  extern void fw_boot_splash_show(void);
  pebble_zephyr_core_event_loop_init();
  fw_sandbox_display_init();
  fw_boot_splash_show();

#if defined(CONFIG_BOARD_QEMU_EMERY)
  for (int i = 0; i < 3; ++i) {
    PebbleEvent event = {
      .type = PEBBLE_CALLBACK_EVENT,
      .callback = { .callback = prv_boot_dup_render },
    };
    event_put(&event);
  }
#endif

  // Shipping (system_app_state_machine) roots the boot in the launcher; BACK
  // closes it to the watchface with the shutter-left transition.
  s_launcher_args = (LauncherMenuArgs) { .reset_scroll = true };
  s_task_context.args = &s_launcher_args;
  s_launcher_active = true;
  fw_system_app_launch(launcher_menu_app_get_app_info());

  while (true) {
    // The watchface is the shell's root app; its app_event_loop pumps the UI
    // and processes launcher/app launches. It only returns if its window is
    // somehow popped — relaunch it.
    fw_system_app_launch(tictoc_get_app_info());
  }
}
