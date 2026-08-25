/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Stand up the real PebbleOS window stack + launcher menu (menu_layer) driven
// by the real click service, and run the KernelMain UI event loop. Consumes
// PEBBLE_BUTTON_DOWN/UP events from the shared event queue (fed by the obelix
// button driver / click service), routes them through click_recognizer -> the
// window's click handlers, and renders the top window to the panel each frame.
void fw_launcher_ui_run(void);

struct Window;

// One iteration of the shared KernelMain UI pump (event take -> BACK/click/
// callback dispatch -> event-service -> render top window). Used by the launcher
// loop and by the system-app launch core (system_app.c) so launched apps run on
// the same loop.
void fw_ui_pump_once(void);

// Push a window onto the shared launcher window stack (renders it, applies its
// click config). Used by app_event_loop() to hand a system app's window to the
// shared pump.
void fw_window_stack_push(struct Window *window);

// Current window-stack depth (1 = launcher root only). app_event_loop() uses it
// to detect when its window has been popped (BACK).
int fw_window_stack_depth(void);
