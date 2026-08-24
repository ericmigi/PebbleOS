/* SPDX-License-Identifier: Apache-2.0 */
//! Top half of the button bring-up: drives the real PebbleOS click_recognizer
//! (src/fw/applib/ui/click.c) from debounced PEBBLE_BUTTON_DOWN/UP events and
//! logs the recognised gestures over UART.
//!
//! This is the standalone equivalent of what app_window_click_glue does per
//! window in shipping firmware: we own one ClickManager and feed its
//! recognizers directly. Runs entirely on the KernelMain task, so click.c's
//! app_timer callbacks (long/multi/repeat) dispatch on the same task via the
//! event loop, exactly as they do inside an app process.

#include "button_input.h"

#include "applib/app_timer.h"
#include "applib/ui/click.h"
#include "applib/ui/click_internal.h"
#include "kernel/events.h"
#include "pbl/logging/logging.h"
#include "pbl/services/evented_timer.h"
#include <pbl/drivers/button_id.h>

// app_timer for the kernel task: the real applib/app_timer.c is a syscall
// wrapper pulling in app_logging/syscall glue we don't need here. click.c only
// uses register/cancel, and everything runs on KernelMain, so wrap
// evented_timer directly (identical behaviour in privileged/kernel context).
AppTimer *app_timer_register(uint32_t timeout_ms, AppTimerCallback cb, void *data) {
  return (AppTimer *)(uintptr_t)evented_timer_register(timeout_ms, false, cb, data);
}

void app_timer_cancel(AppTimer *timer) {
  evented_timer_cancel((EventedTimerID)(uintptr_t)timer);
}

static ClickManager s_click_manager;

static const char *const s_button_names[NUM_BUTTONS] = {
    [BUTTON_ID_BACK] = "BACK",
    [BUTTON_ID_UP] = "UP",
    [BUTTON_ID_SELECT] = "SELECT",
    [BUTTON_ID_DOWN] = "DOWN",
};

// click.c reaches for the app's click manager only in its serial command
// (command_put_button_event). Point it at ours so the real file links.
ClickManager *app_state_get_click_manager(void) { return &s_click_manager; }

static const char *prv_name(ClickRecognizerRef ref) {
  return s_button_names[click_recognizer_get_button_id(ref)];
}

static void prv_single_click(ClickRecognizerRef ref, void *context) {
  ARG_UNUSED(context);
  PBL_LOG_ALWAYS("CLICK %s single%s", prv_name(ref),
                 click_recognizer_is_repeating(ref) ? " (repeat)" : "");
}

static void prv_multi_click(ClickRecognizerRef ref, void *context) {
  ARG_UNUSED(context);
  PBL_LOG_ALWAYS("CLICK %s multi x%u", prv_name(ref),
                 click_number_of_clicks_counted(ref));
}

static void prv_long_click(ClickRecognizerRef ref, void *context) {
  ARG_UNUSED(context);
  PBL_LOG_ALWAYS("CLICK %s long down", prv_name(ref));
}

static void prv_long_release(ClickRecognizerRef ref, void *context) {
  ARG_UNUSED(context);
  PBL_LOG_ALWAYS("CLICK %s long up", prv_name(ref));
}

// Populate one recognizer's ClickConfig, the same "template" a
// ClickConfigProvider would fill in via the window_*_subscribe helpers.
static void prv_configure(ButtonId id, const ClickConfig *cfg) {
  s_click_manager.recognizers[id].config = *cfg;
  click_recognizer_reset(&s_click_manager.recognizers[id]);
}

void input_service_init(void) {
  // evented_timer backs app_timer, which click.c uses for long/multi/repeat.
  evented_timer_init();
  click_manager_init(&s_click_manager);

  // UP / DOWN: single click with hold-to-repeat every 300ms.
  const ClickConfig updown = {
      .click = {.handler = prv_single_click, .repeat_interval_ms = 300},
  };
  prv_configure(BUTTON_ID_UP, &updown);
  prv_configure(BUTTON_ID_DOWN, &updown);

  // BACK: plain single click.
  const ClickConfig back = {.click = {.handler = prv_single_click}};
  prv_configure(BUTTON_ID_BACK, &back);

  // SELECT: single + double (multi) + long press, to exercise every path.
  const ClickConfig select = {
      .click = {.handler = prv_single_click},
      .multi_click = {.min = 2, .max = 2, .last_click_only = true,
                      .handler = prv_multi_click, .timeout = 300},
      .long_click = {.delay_ms = 500, .handler = prv_long_click,
                     .release_handler = prv_long_release},
  };
  prv_configure(BUTTON_ID_SELECT, &select);

  PBL_LOG_ALWAYS("INPUT_SVC_OK");
}

void input_service_handle_button_event(PebbleEvent *e) {
  const ButtonId id = e->button.button_id;
  if (id >= NUM_BUTTONS) {
    return;
  }
  if (e->type == PEBBLE_BUTTON_DOWN_EVENT) {
    PBL_LOG_ALWAYS("BTN %s DOWN", s_button_names[id]);
    click_recognizer_handle_button_down(&s_click_manager.recognizers[id]);
  } else {
    PBL_LOG_ALWAYS("BTN %s UP", s_button_names[id]);
    click_recognizer_handle_button_up(&s_click_manager.recognizers[id]);
  }
}
