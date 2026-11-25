/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>

#include <pbl/drivers/input/adc_keys.h>
#include <pbl/input/input.h>
#include <pbl/kernel/work.h>

#include <clar.h>

// Stubs for ADC, work queues and input reporting

struct pbl_workq pbl_sys_workq;
struct pbl_workq pbl_sys_workq_high;

static int s_adc_ret;
static int32_t s_adc_uv;

int pbl_adc_read(const struct pbl_adc_channel *channel, int32_t *microvolt) {
  (void)channel;
  if (s_adc_ret != 0) {
    return s_adc_ret;
  }
  *microvolt = s_adc_uv;
  return 0;
}

static struct pbl_workq *s_last_workq;
static pbl_timeout_t s_last_delay;
static uint32_t s_schedule_count;

void pbl_delayed_work_init(struct pbl_delayed_work *dwork, pbl_work_fn_t fn) {
  dwork->work.fn = fn;
}

bool pbl_delayed_work_schedule(struct pbl_workq *q, struct pbl_delayed_work *dwork,
                               pbl_timeout_t delay) {
  (void)dwork;
  s_last_workq = q;
  s_last_delay = delay;
  s_schedule_count++;
  return true;
}

bool pbl_delayed_work_reschedule(struct pbl_workq *q, struct pbl_delayed_work *dwork,
                                 pbl_timeout_t delay) {
  return pbl_delayed_work_schedule(q, dwork, delay);
}

int pbl_intc_request(const struct pbl_irq_spec *spec, pbl_intc_handler_t handler, void *arg) {
  (void)spec;
  (void)handler;
  (void)arg;
  return 0;
}

int pbl_intc_enable(const struct pbl_irq_spec *spec) {
  (void)spec;
  return 0;
}

int pbl_pinctrl_select_state(const struct pbl_pinctrl *pinctrl, enum pbl_pinctrl_state_id id) {
  (void)pinctrl;
  (void)id;
  return -ENOENT;
}

static uint16_t s_last_code;
static bool s_last_pressed;
static uint32_t s_report_count;

void pbl_input_report(enum pbl_input_type type, uint16_t code, int32_t value, bool sync) {
  (void)type;
  (void)sync;
  s_last_code = code;
  s_last_pressed = value != 0;
  s_report_count++;
}

// Test ladder matching Xteink X4 keys_b: 15 ms active poll, key-up 3.03 V, Up 1.67 V, Down 0 V

static const struct pbl_adc_ops s_adc_ops;
PBL_DEVICE_STATE_DEFINE(s_adc);
static const struct pbl_adc s_adc = {
  .dev = PBL_DEVICE_INIT(s_adc, "adc", nullptr, nullptr),
  .ops = &s_adc_ops,
  .num_channels = 4U,
};

static const struct pbl_adc_keys_key s_keys[] = {
  PBL_ADC_KEYS_KEY("Up", PBL_INPUT_KEY_UP, 1670000),
  PBL_ADC_KEYS_KEY("Down", PBL_INPUT_KEY_DOWN, 0),
};
static struct pbl_adc_keys_state s_keys_state;
PBL_DEVICE_STATE_DEFINE(s_keys_dev);
static const struct pbl_adc_keys s_keys_dev = {
  .dev = PBL_DEVICE_INIT(s_keys_dev, "keys", pbl_adc_keys_init, nullptr),
  .channel = PBL_ADC_CHANNEL(&s_adc, 1U),
  .keyup_uv = 3030000,
  .poll_ms = 15U,
  .pinctrl = PBL_PINCTRL(nullptr, nullptr, 0U),
  .keys = s_keys,
  .num_keys = PBL_ARRAY_SIZE(s_keys),
  .state = &s_keys_state,
};

static void prv_step(void) {
  s_keys_state.scan.work.fn(&s_keys_state.scan.work);
}

static void prv_drive_to_idle(void) {
  s_adc_ret = 0;
  s_adc_uv = 3030000;
  for (uint32_t i = 0U; i < 150U; i++) {
    prv_step();
  }
}

void test_adc_keys__initialize(void) {
  s_adc_ret = 0;
  s_adc_uv = 3030000;
  s_last_workq = nullptr;
  s_last_delay = PBL_NO_WAIT;
  s_schedule_count = 0U;
  s_last_code = 0U;
  s_last_pressed = false;
  s_report_count = 0U;

  // Reset device init state between tests
  *s_adc.dev.state = (struct pbl_device_state){0};
  *s_keys_dev.dev.state = (struct pbl_device_state){0};
  cl_assert_equal_i(pbl_device_init(&s_keys_dev.dev), 0);
  cl_assert_equal_p(s_last_workq, &pbl_sys_workq_high);
}

void test_adc_keys__slows_after_idle_delay(void) {
  // First scan is still active (15 ms)
  prv_step();
  cl_assert_equal_i(s_keys_state.delay_ms, 15U);
  cl_assert_equal_i(s_last_delay.ticks, PBL_MSEC(15U).ticks);

  // 133 more polls * 15 ms = 1995 ms quiet: still active
  for (uint32_t i = 0U; i < 133U; i++) {
    prv_step();
    cl_assert_equal_i(s_keys_state.delay_ms, 15U);
  }

  // Next poll crosses 2000 ms quiet -> slows to 80 ms
  prv_step();
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);
  cl_assert_equal_i(s_last_delay.ticks, PBL_MSEC(80U).ticks);
  cl_assert_equal_i(s_report_count, 0U);
}

void test_adc_keys__wakes_fast_on_press_and_slows_after_release(void) {
  prv_drive_to_idle();
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);

  // First pressed sample schedules 5 ms confirm
  s_adc_uv = 1670000;
  prv_step();
  cl_assert_equal_i(s_keys_state.delay_ms, 5U);
  cl_assert_equal_i(s_report_count, 0U);

  // Second pressed sample confirms key and resumes 15 ms active polling
  prv_step();
  cl_assert_equal_i(s_report_count, 1U);
  cl_assert_equal_i(s_last_code, PBL_INPUT_KEY_UP);
  cl_assert(s_last_pressed);
  cl_assert_equal_i(s_keys_state.delay_ms, 15U);

  // Held key stays at 15 ms indefinitely
  for (uint32_t i = 0U; i < 200U; i++) {
    prv_step();
    cl_assert_equal_i(s_keys_state.delay_ms, 15U);
  }

  // Release takes 3 polls at 15 ms
  s_adc_uv = 3030000;
  prv_step();
  cl_assert_equal_i(s_report_count, 1U);
  cl_assert_equal_i(s_keys_state.delay_ms, 15U);
  prv_step();
  cl_assert_equal_i(s_report_count, 1U);
  cl_assert_equal_i(s_keys_state.delay_ms, 15U);
  prv_step();
  cl_assert_equal_i(s_report_count, 2U);
  cl_assert_equal_i(s_last_code, PBL_INPUT_KEY_UP);
  cl_assert(!s_last_pressed);
  cl_assert_equal_i(s_keys_state.delay_ms, 15U);

  // Quiet period starts fresh after release, then returns to 80 ms
  prv_drive_to_idle();
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);
}

void test_adc_keys__glitch_while_idle_returns_to_idle(void) {
  prv_drive_to_idle();
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);

  // Single spurious sample asks for 5 ms confirm
  s_adc_uv = 0;
  prv_step();
  cl_assert_equal_i(s_keys_state.delay_ms, 5U);

  // Next sample back at key-up returns straight to 80 ms without waiting 2 s again
  s_adc_uv = 3030000;
  prv_step();
  cl_assert_equal_i(s_report_count, 0U);
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);
}

void test_adc_keys__read_error_preserves_idle_rate(void) {
  prv_drive_to_idle();
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);

  s_adc_ret = -EIO;
  prv_step();
  cl_assert_equal_i(s_keys_state.delay_ms, 80U);
}
