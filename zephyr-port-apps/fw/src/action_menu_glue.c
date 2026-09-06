/* SPDX-License-Identifier: Apache-2.0 */

// WindowStack bridge for the shipping action_menu (SELECT on a notification).
// action_menu_window.c opens onto a WindowStack (app_state_get_window_stack) and
// pushes/removes/inserts through window_stack_*; the port has no WindowStack
// object, just launcher_ui.c's shared fw_window_stack. WindowStack is opaque, so
// app_state_get_window_stack returns an inert non-NULL sentinel and the
// window_stack_* entrypoints ignore it and drive the shared stack directly.

#include "applib/ui/window.h"
#include "applib/ui/window_stack.h"

extern void fw_window_stack_push(Window *window);

static char s_window_stack_sentinel;

WindowStack *app_state_get_window_stack(void) {
  return (WindowStack *)&s_window_stack_sentinel;
}

void window_stack_insert_next(WindowStack *window_stack, Window *window) {
  (void)window_stack;
  fw_window_stack_push(window);
}

void window_set_fullscreen(Window *window, bool enabled) {
  window->is_fullscreen = enabled;
}
