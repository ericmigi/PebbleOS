/* SPDX-License-Identifier: Apache-2.0 */
//! Shared Zephyr bottom half for the four Pebble buttons.
//!
//! The shipping sf32lb52 driver (src/fw/drivers/sf32lb52/debounced_button.c)
//! wakes a 100us GPT on any EXTI edge, samples every 2ms, and accepts a new
//! button state after 20 stable samples (40ms), emitting PEBBLE_BUTTON_DOWN/UP
//! via event_put_isr. We reproduce that debounce here on top of a per-board
//! raw-state source (button_raw_init/button_raw_read: pt2 reads the GPIOs,
//! qemu_emery folds Zephyr input-subsystem key events into a bitset).

#include "button_input.h"

#include <zephyr/kernel.h>

#include "kernel/events.h"
#include "pbl/logging/logging.h"
#include "system/passert.h"
#include <pbl/drivers/button_id.h>

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

// k_timer expiry runs in ISR/sysclock context, matching the shipping GPT ISR;
// event_put_isr() targets the kernel event queue without needing a PebbleTask.
static void prv_sample_timer(struct k_timer *timer) {
  ARG_UNUSED(timer);
  const uint32_t raw = button_raw_read();
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
  if (button_raw_init() != 0) {
    return;
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
