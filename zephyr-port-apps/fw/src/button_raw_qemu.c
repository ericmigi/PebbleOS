/* SPDX-License-Identifier: Apache-2.0 */
//! qemu_emery raw-button source: the pebble,buttons input driver reports
//! INPUT_KEY_BACK/UP/ENTER/DOWN edges (qemu monitor sendkey left/up/right/down);
//! we fold them into a pressed-bitset the shared debouncer samples.

#include "button_input.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>

static volatile uint32_t s_raw_state;

static void prv_input_cb(struct input_event *evt, void *user_data) {
  (void)user_data;

  ButtonId id;
  switch (evt->code) {
    case INPUT_KEY_BACK:
      id = BUTTON_ID_BACK;
      break;
    case INPUT_KEY_UP:
      id = BUTTON_ID_UP;
      break;
    case INPUT_KEY_ENTER:
      id = BUTTON_ID_SELECT;
      break;
    case INPUT_KEY_DOWN:
      id = BUTTON_ID_DOWN;
      break;
    default:
      return;
  }
  if (evt->value) {
    s_raw_state |= (1u << id);
  } else {
    s_raw_state &= ~(1u << id);
  }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(buttons0)), prv_input_cb, NULL);

int button_raw_init(void) {
  return device_is_ready(DEVICE_DT_GET(DT_NODELABEL(buttons0))) ? 0 : -1;
}

uint32_t button_raw_read(void) {
  return s_raw_state;
}
