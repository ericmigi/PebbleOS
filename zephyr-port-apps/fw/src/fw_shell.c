/* SPDX-License-Identifier: Apache-2.0 */

// Real-shell boot flow (FW_REAL_SHELL, qemu_emery): mirror the shipping normal
// shell — boot into the TicToc watchface, SELECT opens the REAL launcher app
// (apps/system/launcher/default), BACK returns to the watchface, and exiting an
// app launched from the launcher returns to the launcher (selection persisted
// by launcher.c itself).

#include <stdbool.h>
#include <stddef.h>

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

#include "launcher_ui.h"
#include "system_app.h"

void fw_sandbox_display_init(void);

// ---------------------------------------------------------------------------
// app_manager task context: carries the launcher's args (reset_scroll).
// ---------------------------------------------------------------------------
static LauncherMenuArgs s_launcher_args;
static ProcessContext s_task_context;

ProcessContext *app_manager_get_task_context(void) { return &s_task_context; }

// The launcher starts its idle timeout in shipping; no-op here.
void app_idle_timeout_start(void) {}

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
    prv_request_launcher(true /* reset_scroll */);
  }
  return true;
}

// Exiting an app that was launched from the launcher returns to the launcher
// (shipping relaunches it with its persisted selection). Exiting the launcher
// itself, or a watchface, falls back to the watchface pump.
void fw_shell_on_app_exit(const PebbleProcessMd *md) {
  if (md == launcher_menu_app_get_app_info() ||
      md->process_type == ProcessTypeWatchface ||
      fw_shell_launch_pending()) {
    return;
  }
  prv_request_launcher(false /* reset_scroll */);
}

void fw_launcher_ui_run(void) {
  extern void pebble_zephyr_core_event_loop_init(void);
  pebble_zephyr_core_event_loop_init();
  fw_sandbox_display_init();

  while (true) {
    // The watchface is the shell's root app; its app_event_loop pumps the UI
    // and processes launcher/app launches. It only returns if its window is
    // somehow popped — relaunch it.
    fw_system_app_launch(tictoc_get_app_info());
  }
}
