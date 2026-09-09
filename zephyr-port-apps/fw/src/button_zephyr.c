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
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

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
// Console-RX click injection (hardware only; qemu has sendkey): b/u/s/d press
// a button for 120 ms, B/U/S/D for 800 ms. The virtual press is OR'd into the
// sampler so it takes the same debounce + event path as a physical press,
// like the shipping `click` console command.
#if !defined(CONFIG_BOARD_QEMU_EMERY)
static volatile uint32_t s_inject_mask;
static volatile uint16_t s_inject_samples;
static K_THREAD_STACK_DEFINE(s_inject_stack, 1024);
static struct k_thread s_inject_thread;

static void prv_inject_thread(void *a, void *b, void *c) {
  ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
  const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
  while (true) {
    unsigned char ch;
    if (uart_poll_in(console, &ch) != 0) {
      k_sleep(K_MSEC(20));
      continue;
    }
    int id = -1;
    switch (ch | 0x20) {
      case 'b': id = BUTTON_ID_BACK; break;
      case 'u': id = BUTTON_ID_UP; break;
      case 's': id = BUTTON_ID_SELECT; break;
      case 'd': id = BUTTON_ID_DOWN; break;
      default: break;
    }
    if (id < 0) {
      continue;
    }
    const bool is_long = (ch & 0x20) == 0;
    // Wait for any earlier press to release so keys sent back-to-back stay distinct.
    while (s_inject_samples) {
      k_sleep(K_MSEC(2));
    }
    k_sleep(K_MSEC(100));
    s_inject_mask = 1u << id;
    s_inject_samples = is_long ? 400 : 60;
  }
}

static uint32_t prv_inject_apply(uint32_t raw) {
  if (s_inject_samples) {
    raw |= s_inject_mask;
    if (--s_inject_samples == 0) {
      s_inject_mask = 0;
    }
  }
  return raw;
}

static void prv_inject_init(void) {
  k_thread_create(&s_inject_thread, s_inject_stack, K_THREAD_STACK_SIZEOF(s_inject_stack),
                  prv_inject_thread, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
  k_thread_name_set(&s_inject_thread, "btn_inject");
}
#else
static uint32_t prv_inject_apply(uint32_t raw) { return raw; }
static void prv_inject_init(void) {}
#endif

static void prv_sample_timer(struct k_timer *timer) {
  ARG_UNUSED(timer);
  const uint32_t raw = prv_inject_apply(button_raw_read());
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
  prv_inject_init();
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
