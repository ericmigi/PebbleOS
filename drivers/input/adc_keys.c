/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/input/adc_keys.h>
#include <pbl/input/input.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>

#ifndef CONFIG_INPUT_ADC_KEYS_IDLE_POLL_MS
#define CONFIG_INPUT_ADC_KEYS_IDLE_POLL_MS 0U
#endif
#ifndef CONFIG_INPUT_ADC_KEYS_IDLE_DELAY_MS
#define CONFIG_INPUT_ADC_KEYS_IDLE_DELAY_MS 2000U
#endif

// Time the line must be quiet before it is read
#define SETTLE_MS 20U
// Moving from one key to another raises no edge, so the line is read again while a key is down
#define HELD_SCAN_MS 50U
// A read that keeps failing is left until the next edge, so that the SoC can still sleep
#define MAX_FAILURES 5U
// Polled: presses settle in about a millisecond, releases chatter for up to some 25 ms, so a key
// is taken after two polls that read it, the second soon after the first, and key-up after three
#define PRESS_POLLS   2U
#define RELEASE_POLLS 3U
#define CONFIRM_MS    5U

// The key nearest to the voltage, NULL if key-up is
static const struct pbl_adc_keys_key *prv_nearest(const struct pbl_adc_keys *keys, int32_t uv) {
  const struct pbl_adc_keys_key *nearest = nullptr;
  int64_t best = llabs((int64_t)uv - keys->keyup_uv);

  for (uint8_t i = 0U; i < keys->num_keys; i++) {
    int64_t diff = llabs((int64_t)uv - keys->keys[i].press_uv);

    if (diff < best) {
      best = diff;
      nearest = &keys->keys[i];
    }
  }

  return nearest;
}

// A polled ladder samples on the high priority queue, so threads busy for long, e.g. writing a
// panel, do not starve it into missing taps
static struct pbl_workq *prv_workq(const struct pbl_adc_keys *keys) {
  return (keys->poll_ms != 0U) ? &pbl_sys_workq_high : &pbl_sys_workq;
}

static void prv_report(struct pbl_adc_keys_state *state, const struct pbl_adc_keys_key *key) {
  if (state->pressed != nullptr) {
    pbl_input_report_key(state->pressed->code, false, key == nullptr);
  }
  if (key != nullptr) {
    pbl_input_report_key(key->code, true, true);
  }
  state->pressed = key;
}

static uint32_t prv_poll_delay_ms(const struct pbl_adc_keys_state *state) {
  if (CONFIG_INPUT_ADC_KEYS_IDLE_POLL_MS > state->keys->poll_ms &&
      state->quiet_ms >= CONFIG_INPUT_ADC_KEYS_IDLE_DELAY_MS) {
    return CONFIG_INPUT_ADC_KEYS_IDLE_POLL_MS;
  }
  return state->keys->poll_ms;
}

static void prv_poll(struct pbl_adc_keys_state *state, const struct pbl_adc_keys_key *key) {
  uint32_t delay_ms;

  if (key == state->pressed) {
    state->count = 0U;
    if (state->pressed == nullptr) {
      const uint32_t quiet = (uint32_t)state->quiet_ms + state->delay_ms;

      state->quiet_ms = (uint16_t)PBL_MIN(quiet, (uint32_t)CONFIG_INPUT_ADC_KEYS_IDLE_DELAY_MS);
    } else {
      state->quiet_ms = 0U;
    }
    delay_ms = prv_poll_delay_ms(state);
  } else {
    if (state->count == 0U || key != state->candidate) {
      state->candidate = key;
      state->count = 0U;
    }
    state->count++;
    if (state->count >= (key == nullptr ? RELEASE_POLLS : PRESS_POLLS)) {
      prv_report(state, key);
      state->count = 0U;
      state->quiet_ms = 0U;
      delay_ms = state->keys->poll_ms;
    } else if (key != nullptr) {
      delay_ms = CONFIRM_MS;
    } else {
      state->quiet_ms = 0U;
      delay_ms = state->keys->poll_ms;
    }
  }

  state->delay_ms = (uint16_t)delay_ms;
  (void)pbl_delayed_work_schedule(prv_workq(state->keys), &state->scan, PBL_MSEC(delay_ms));
}

static void prv_scan(struct pbl_work *work) {
  struct pbl_adc_keys_state *state = PBL_CONTAINER_OF(work, struct pbl_adc_keys_state, scan.work);
  const struct pbl_adc_keys *keys = state->keys;
  int32_t uv;

  if (pbl_adc_read(&keys->channel, &uv) != 0) {
    if (keys->poll_ms != 0U) {
      const uint32_t delay_ms = prv_poll_delay_ms(state);

      state->delay_ms = (uint16_t)delay_ms;
      (void)pbl_delayed_work_schedule(prv_workq(state->keys), &state->scan, PBL_MSEC(delay_ms));
      return;
    }
    if (++state->failures < MAX_FAILURES) {
      (void)pbl_delayed_work_schedule(prv_workq(state->keys), &state->scan, PBL_MSEC(HELD_SCAN_MS));
    }
    return;
  }
  state->failures = 0U;

  const struct pbl_adc_keys_key *key = prv_nearest(keys, uv);

  if (keys->poll_ms != 0U) {
    prv_poll(state, key);
    return;
  }

  if (key != state->pressed) {
    prv_report(state, key);
  }

  if (key != nullptr) {
    (void)pbl_delayed_work_schedule(prv_workq(state->keys), &state->scan, PBL_MSEC(HELD_SCAN_MS));
  }
}

static void prv_edge(void *arg) {
  struct pbl_adc_keys_state *state = arg;

  state->failures = 0U;
  (void)pbl_delayed_work_reschedule(prv_workq(state->keys), &state->scan, PBL_MSEC(SETTLE_MS));
}

int pbl_adc_keys_init(const struct pbl_device *dev) {
  const struct pbl_adc_keys *keys = PBL_CONTAINER_OF(dev, const struct pbl_adc_keys, dev);
  struct pbl_adc_keys_state *state = keys->state;
  int ret;

  ret = pbl_device_init(&keys->channel.adc->dev);
  if (ret != 0) {
    return ret;
  }

  if (keys->poll_ms == 0U) {
    ret = pbl_device_init(&keys->irq.intc->dev);
    if (ret != 0) {
      return ret;
    }
  }

  ret = pbl_pinctrl_select_state(&keys->pinctrl, PBL_PINCTRL_STATE_DEFAULT);
  if (ret != 0 && ret != -ENOENT) {
    return ret;
  }

  state->keys = keys;
  state->pressed = nullptr;
  state->count = 0U;
  state->quiet_ms = 0U;
  state->delay_ms = 0U;
  pbl_delayed_work_init(&state->scan, prv_scan);

  if (keys->poll_ms == 0U) {
    ret = pbl_intc_request(&keys->irq, prv_edge, state);
    if (ret != 0) {
      return ret;
    }

    ret = pbl_intc_enable(&keys->irq);
    if (ret != 0) {
      return ret;
    }
  }

  // The keys as they are now
  (void)pbl_delayed_work_schedule(prv_workq(state->keys), &state->scan, PBL_MSEC(SETTLE_MS));

  return 0;
}

#include PBL_DT_INSTANCES(adc_keys)
