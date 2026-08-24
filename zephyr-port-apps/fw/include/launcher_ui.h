/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Stand up the real PebbleOS window stack + launcher menu (menu_layer) driven
// by the real click service, and run the KernelMain UI event loop. Consumes
// PEBBLE_BUTTON_DOWN/UP events from the shared event queue (fed by the obelix
// button driver / click service), routes them through click_recognizer -> the
// window's click handlers, and renders the top window to the panel each frame.
void fw_launcher_ui_run(void);
