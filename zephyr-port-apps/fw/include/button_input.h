/* SPDX-License-Identifier: Apache-2.0 */
//! Obelix (pt2) button bring-up under Zephyr.
//!
//! Bottom half: reads the four physical button GPIOs, runs the shipping
//! debounce filter, and emits real PebbleEvents (PEBBLE_BUTTON_DOWN/UP).
//! Top half: feeds those events into the real PebbleOS click_recognizer.
#pragma once

#include <stdint.h>

#include "kernel/events.h"
#include <pbl/drivers/button_id.h>

//! Number of stable 2ms samples a button must hold before its state flips.
//! 20 samples * 2ms = 40ms, identical to the shipping sf32lb52 driver.
#define BUTTON_NUM_DEBOUNCE_SAMPLES 20

//! Pure debounce filter state (one per button set). Host-testable: no I/O.
typedef struct {
  uint32_t timers[NUM_BUTTONS];
  uint32_t debounced_state;  //!< bitset, bit i == BUTTON_ID_i currently pressed
} ButtonDebouncer;

//! Advance the debounce filter one 2ms sample.
//! @param raw_state bitset of raw (already active-level-adjusted) pressed bits
//! @return bitset of buttons whose debounced state flipped this step
uint32_t button_debounce_step(ButtonDebouncer *d, uint32_t raw_state);

//! Board backend: prepare the raw button source. Returns 0 on success.
int button_raw_init(void);

//! Board backend: current raw pressed-bitset (active-level adjusted).
uint32_t button_raw_read(void);

//! Bring up the raw button source and start the 2ms sampling timer.
void button_zephyr_init(void);

//! Runtime self-check of the debounce filter + event mapping. Asserts on
//! failure, logs a marker on success. Safe to call once at boot.
void button_input_selfcheck(void);

//! Initialise the click_recognizer demo (owns a ClickManager, subscribes
//! single/repeat/long/multi/raw handlers that log over UART).
void input_service_init(void);

//! Feed one debounced button event into the click_recognizer. Called from the
//! KernelMain event loop. Logs "BTN <id> DOWN/UP".
void input_service_handle_button_event(PebbleEvent *e);
