/* SPDX-License-Identifier: Apache-2.0 */
//! Zephyr bottom half for the obelix physical buttons on pt2.
//!
//! The shipping sf32lb52 driver (src/fw/drivers/sf32lb52/debounced_button.c)
//! wakes a 100us GPT on any EXTI edge, samples every 2ms, and accepts a new
//! button state after 20 stable samples (40ms), emitting PEBBLE_BUTTON_DOWN/UP
//! via event_put_isr. We reproduce that debounce here on top of Zephyr GPIO.
//!
//! The pt2 board (boards/coredevices/pt2/pt2.dts) already describes the four
//! buttons as a gpio-keys node on gpioa_32_44 pins 2-5 (== obelix hwp_gpio1
//! pins 34-37: BACK active-high/no-pull, UP/SELECT/DOWN active-low/pull-up).
//! We read those GPIOs directly (no gpio-keys input driver bound) so the
//! debounce + event mapping stay 1:1 with shipping firmware.

#include "button_input.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#include "kernel/events.h"
#include "pbl/logging/logging.h"
#include "system/passert.h"
#include <pbl/drivers/button_id.h>

// gpio-keys child nodes on the pt2 board, indexed by ButtonId.
#define BTN_SPEC(node) GPIO_DT_SPEC_GET(node, gpios)
static const struct gpio_dt_spec s_buttons[NUM_BUTTONS] = {
    [BUTTON_ID_BACK]   = BTN_SPEC(DT_NODELABEL(btn_back)),
    [BUTTON_ID_UP]     = BTN_SPEC(DT_NODELABEL(btn_up)),
    [BUTTON_ID_SELECT] = BTN_SPEC(DT_NODELABEL(btn_center)),
    [BUTTON_ID_DOWN]   = BTN_SPEC(DT_NODELABEL(btn_down)),
};

static ButtonDebouncer s_debouncer;
static struct k_timer s_sample_timer;

// 20 * 2ms == 40ms stable, matching DEBOUNCE_SAMPLES_PER_SECOND math in shipping.
#define BUTTON_SAMPLE_PERIOD K_MSEC(2)

uint32_t button_debounce_step(ButtonDebouncer *d, uint32_t raw_state) {
  // Mirror of prv_timer_handler() in the shipping sf32lb52 debounced_button.c:
  // a button that already matches its debounced state resets its counter; a
  // button that disagrees must stay disagreeing for BUTTON_NUM_DEBOUNCE_SAMPLES
  // consecutive samples before the debounced state flips.
  uint32_t changed = 0;
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    const bool debounced = (d->debounced_state >> i) & 1u;
    const bool is_pressed = (raw_state >> i) & 1u;

    if (is_pressed == debounced) {
      d->timers[i] = 0;
      continue;
    }

    if (++d->timers[i] >= BUTTON_NUM_DEBOUNCE_SAMPLES) {
      d->timers[i] = 0;
      d->debounced_state ^= (1u << i);
      changed |= (1u << i);
    }
  }
  return changed;
}

// Read the four buttons into a raw pressed-bitset. gpio_pin_get_dt() already
// honours GPIO_ACTIVE_LOW from the devicetree, so a set bit always means
// "pressed" regardless of the pin's electrical polarity.
static uint32_t prv_read_raw(void) {
  uint32_t raw = 0;
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (gpio_pin_get_dt(&s_buttons[i]) > 0) {
      raw |= (1u << i);
    }
  }
  return raw;
}

// k_timer expiry runs in ISR/sysclock context, matching the shipping GPT ISR;
// event_put_isr() targets the kernel event queue without needing a PebbleTask.
static void prv_sample_timer(struct k_timer *timer) {
  ARG_UNUSED(timer);
  const uint32_t raw = prv_read_raw();
  const uint32_t changed = button_debounce_step(&s_debouncer, raw);

  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (!(changed & (1u << i))) {
      continue;
    }
    const bool is_pressed = (s_debouncer.debounced_state >> i) & 1u;
    PebbleEvent e = {
        .type = is_pressed ? PEBBLE_BUTTON_DOWN_EVENT : PEBBLE_BUTTON_UP_EVENT,
        .button.button_id = i,
    };
    event_put_isr(&e);
  }
}

void button_zephyr_init(void) {
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (!gpio_is_ready_dt(&s_buttons[i])) {
      PBL_LOG_ALWAYS("BTN_INIT_FAIL id=%d gpio not ready", i);
      return;
    }
    // GPIO_INPUT plus the pull encoded in the devicetree flags (pull-up for
    // UP/SELECT/DOWN, none for BACK) reproduces button_init() in shipping.
    int rc = gpio_pin_configure_dt(&s_buttons[i], GPIO_INPUT);
    if (rc != 0) {
      PBL_LOG_ALWAYS("BTN_INIT_FAIL id=%d rc=%d", i, rc);
      return;
    }
  }

  // ponytail: free-running 2ms sampler. Shipping gates the timer with EXTI
  // wake + idle-stop to save power; for bring-up we poll continuously. Add the
  // EXTI/idle-stop gating when button standby current matters.
  k_timer_init(&s_sample_timer, prv_sample_timer, NULL);
  k_timer_start(&s_sample_timer, BUTTON_SAMPLE_PERIOD, BUTTON_SAMPLE_PERIOD);
  PBL_LOG_ALWAYS("BTN_INIT_OK");
}

void button_input_selfcheck(void) {
  // Drive a known raw-input sequence through the pure debounce filter and
  // assert the emitted edges. Proves the 40ms stability rule and the
  // change-bit -> DOWN/UP mapping without touching hardware.
  ButtonDebouncer d = {0};
  const uint32_t select = (1u << BUTTON_ID_SELECT);

  // A press shorter than the debounce window must be rejected entirely.
  for (int i = 0; i < BUTTON_NUM_DEBOUNCE_SAMPLES - 1; ++i) {
    PBL_ASSERTN(button_debounce_step(&d, select) == 0);
  }
  PBL_ASSERTN(button_debounce_step(&d, 0) == 0);       // bounce back before accept
  PBL_ASSERTN(d.debounced_state == 0);

  // A press held for the full window flips exactly on the Nth stable sample.
  for (int i = 0; i < BUTTON_NUM_DEBOUNCE_SAMPLES - 1; ++i) {
    PBL_ASSERTN(button_debounce_step(&d, select) == 0);
  }
  PBL_ASSERTN(button_debounce_step(&d, select) == select);  // DOWN edge
  PBL_ASSERTN(d.debounced_state == select);

  // Release, likewise, only after the full stable window.
  for (int i = 0; i < BUTTON_NUM_DEBOUNCE_SAMPLES - 1; ++i) {
    PBL_ASSERTN(button_debounce_step(&d, 0) == 0);
  }
  PBL_ASSERTN(button_debounce_step(&d, 0) == select);       // UP edge
  PBL_ASSERTN(d.debounced_state == 0);

  PBL_LOG_ALWAYS("BTN_SELFCHECK_OK");
}
