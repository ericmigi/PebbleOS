/* SPDX-License-Identifier: Apache-2.0 */
//! pt2 raw-button source: reads the four obelix button GPIOs directly.
//!
//! The pt2 board (boards/coredevices/pt2/pt2.dts) describes the four buttons
//! as a gpio-keys node on gpioa_32_44 pins 2-5 (== obelix hwp_gpio1 pins
//! 34-37: BACK active-high/no-pull, UP/SELECT/DOWN active-low/pull-up). We
//! read those GPIOs directly (no gpio-keys input driver bound) so the debounce
//! + event mapping stay 1:1 with shipping firmware.

#include "button_input.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#include "pbl/logging/logging.h"

// gpio-keys child nodes on the pt2 board, indexed by ButtonId.
#define BTN_SPEC(node) GPIO_DT_SPEC_GET(node, gpios)
static const struct gpio_dt_spec s_buttons[NUM_BUTTONS] = {
    [BUTTON_ID_BACK]   = BTN_SPEC(DT_NODELABEL(btn_back)),
    [BUTTON_ID_UP]     = BTN_SPEC(DT_NODELABEL(btn_up)),
    [BUTTON_ID_SELECT] = BTN_SPEC(DT_NODELABEL(btn_center)),
    [BUTTON_ID_DOWN]   = BTN_SPEC(DT_NODELABEL(btn_down)),
};

int button_raw_init(void) {
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (!gpio_is_ready_dt(&s_buttons[i])) {
      PBL_LOG_ALWAYS("BTN_INIT_FAIL id=%d gpio not ready", i);
      return -1;
    }
    // GPIO_INPUT plus the pull encoded in the devicetree flags (pull-up for
    // UP/SELECT/DOWN, none for BACK) reproduces button_init() in shipping.
    int rc = gpio_pin_configure_dt(&s_buttons[i], GPIO_INPUT);
    if (rc != 0) {
      PBL_LOG_ALWAYS("BTN_INIT_FAIL id=%d rc=%d", i, rc);
      return rc;
    }
  }
  return 0;
}

// Read the four buttons into a raw pressed-bitset. gpio_pin_get_dt() already
// honours GPIO_ACTIVE_LOW from the devicetree, so a set bit always means
// "pressed" regardless of the pin's electrical polarity.
uint32_t button_raw_read(void) {
  uint32_t raw = 0;
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (gpio_pin_get_dt(&s_buttons[i]) > 0) {
      raw |= (1u << i);
    }
  }
  return raw;
}
