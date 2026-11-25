/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/regulator.h>
#include <pbl/drivers/touch.h>
#include <pbl/input/input.h>

#include <clar.h>
#include <fake_work.h>

extern const struct pbl_touch test_panel;
static pbl_intc_handler_t s_irq;
static void *s_irq_arg;
static uint8_t s_status;
static unsigned s_acks;
static unsigned s_samples;
static unsigned s_home_events;
static bool s_home;
static struct pbl_touch_sample s_last;

int pbl_i2c_transfer(const struct pbl_i2c_dev *dev, struct pbl_i2c_msg *msgs, uint8_t count) {
  cl_assert_equal_i(dev->addr, 0x5d);
  cl_assert_equal_i(msgs[0].flags, PBL_I2C_MSG_WRITE);
  if (count == 1) {
    const uint8_t ack[] = {0x81, 0x4e, 0};
    cl_assert_equal_i(msgs[0].len, sizeof(ack));
    cl_assert_equal_m(msgs[0].buf, ack, sizeof(ack));
    s_status = 0;
    s_acks++;
    return 0;
  }
  cl_assert_equal_i(count, 2);
  cl_assert_equal_i(msgs[0].len, 2);
  cl_assert_equal_i(msgs[1].flags, PBL_I2C_MSG_READ);
  switch ((msgs[0].buf[0] << 8) | msgs[0].buf[1]) {
    case 0x8140:
      cl_assert_equal_i(msgs[1].len, 4);
      memcpy(msgs[1].buf, "911", 4);
      break;
    case 0x814e:
      cl_assert_equal_i(msgs[1].len, 1);
      msgs[1].buf[0] = s_status;
      break;
    case 0x8150: {
      const uint8_t coords[] = {0x2c, 0x01, 0x08, 0x02};
      cl_assert_equal_i(msgs[1].len, sizeof(coords));
      memcpy(msgs[1].buf, coords, sizeof(coords));
      break;
    }
    default:
      cl_fail("unexpected GT911 register");
  }
  return 0;
}

int pbl_intc_request(const struct pbl_irq_spec *spec, pbl_intc_handler_t handler, void *arg) {
  s_irq = handler;
  s_irq_arg = arg;
  return 0;
}

int pbl_intc_enable(const struct pbl_irq_spec *spec) {
  return 0;
}
int pbl_intc_free(const struct pbl_irq_spec *spec) {
  return 0;
}
int pbl_gpio_configure(const struct pbl_gpio *gpio, uint32_t flags) {
  return 0;
}
int pbl_gpio_set_raw(const struct pbl_gpio *gpio, bool high) {
  return 0;
}
int pbl_gpio_set(const struct pbl_gpio *gpio, bool active) {
  return 0;
}
int pbl_regulator_enable(const struct pbl_regulator *reg) {
  return 0;
}
int pbl_regulator_disable(const struct pbl_regulator *reg) {
  return 0;
}

static void prv_input(const struct pbl_input_event *event, void *data) {
  if (event->type == PBL_INPUT_EV_KEY && event->code == PBL_INPUT_KEY_HOME) {
    cl_assert(event->sync);
    s_home = event->value != 0;
    s_home_events++;
  }
  if (event->sync && event->type == PBL_INPUT_EV_ABS) {
    pbl_touch_get_last(&test_panel, &s_last);
    s_samples++;
  }
}
PBL_INPUT_CALLBACK_DEFINE(prv_input, nullptr);

static void prv_fire_next(void) {
  struct pbl_delayed_work *next = fake_work_get_next();
  cl_assert(next != nullptr);
  fake_work_fire(next);
}

static void prv_drain_idle(void) {
  for (unsigned i = 0; fake_work_get_next() != nullptr; i++) {
    cl_assert(i < 6);
    prv_fire_next();
  }
}

void test_gt911__irq_before_ready(void) {
  cl_assert_equal_i(pbl_device_init(&test_panel.dev), 0);
  cl_assert_equal_i(pbl_touch_enable(&test_panel), 0);
  prv_drain_idle();

  s_irq(s_irq_arg);
  prv_fire_next();
  cl_assert_equal_i(s_samples, 0);
  cl_assert_equal_i(s_acks, 0);

  cl_assert(fake_work_get_next() != nullptr);
  cl_assert(fake_work_timeout_ms(fake_work_get_next()) > 0);
  s_status = 0x91;
  prv_fire_next();
  cl_assert_equal_i(s_samples, 1);
  cl_assert(s_last.down);
  cl_assert(s_home);
  cl_assert_equal_i(s_last.x, 300);
  cl_assert_equal_i(s_last.y, 520);

  s_status = 0x80;
  s_irq(s_irq_arg);
  prv_fire_next();
  cl_assert_equal_i(s_samples, 2);
  cl_assert(!s_last.down);
  cl_assert(!s_home);
  cl_assert_equal_i(s_last.x, 300);
  cl_assert_equal_i(s_last.y, 520);

  s_status = 0x80;
  s_irq(s_irq_arg);
  prv_drain_idle();
  cl_assert_equal_i(s_acks, 3);
  cl_assert_equal_i(s_home_events, 2);
  cl_assert_equal_i(s_samples, 2);
  cl_assert_equal_i(pbl_touch_disable(&test_panel), 0);
}
