/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

// Port-local resource IDs for the launcher's per-row app icons + status-bar BT
// glyph. These blobs are embedded in the FW and served through sys_resource_*
// (watchface_sandboxed/src/port.c); the IDs live above the font/music-icon IDs
// (257-278) so they never collide.
#define FW_RES_ICON_SETTINGS 300U
#define FW_RES_ICON_MUSIC 301U
#define FW_RES_ICON_ALARMS 302U
#define FW_RES_ICON_NOTIFICATIONS 303U
#define FW_RES_ICON_WATCHFACES 304U
#define FW_RES_ICON_GENERIC 305U
#define FW_RES_ICON_BT_DISCONNECTED 306U

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

// Pop the top window (runs disappear/unload handlers, re-applies click config).
void fw_window_stack_pop(void);

// The window currently on top of the shared stack (NULL if empty).
struct Window *fw_window_stack_top(void);

struct PebbleProcessMd;

// Ask the pump to launch a system app at its top level (safe from click/render
// callbacks). fw_shell_on_app_exit() (weak, fw_shell.c) is called when it exits.
void fw_shell_request_launch(const struct PebbleProcessMd *md);
void fw_shell_note_activity(void);

// True while a requested launch has not started yet.
bool fw_shell_launch_pending(void);

// Weak shell hook (fw_shell.c): BACK pops the top window even at depth 1
// (boot-rooted launcher). Default false protects the watchface root.
bool fw_shell_back_should_pop(void);

// Weak compositor hook (compositor_port.c): a launch frame returned; unblocks
// a pending transition whose requester just exited.
void fw_compositor_launch_frame_exited(int nesting);

// Weak backlight hooks (qemu_board.c drives the QEMU RGB backlight channels).
void fw_light_button_pressed(void);
void fw_light_button_released(void);
