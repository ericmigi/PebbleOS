/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/regulator.h>
#include <pbl/drivers/touch.h>
#include <pbl/input/input.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/work.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>

#define GT911_PRODUCT_ID   0x8140U
#define GT911_STATUS       0x814eU
#define GT911_FIRST_X      0x8150U
#define GT911_READY        0x80U
#define GT911_HOME         0x10U
#define GT911_CONTACTS     0x0fU
#define GT911_MAX_CONTACTS 5U
#define GT911_RETRY_MS     4U
#define GT911_RETRIES      5U

struct gt911 {
  struct pbl_touch touch;
  struct pbl_i2c_dev i2c;
  struct pbl_irq_spec irq;
  struct pbl_gpio int_pin;
  struct pbl_gpio reset;
  const struct pbl_regulator *supply;
  struct gt911_state *state;
};

struct gt911_state {
  struct pbl_delayed_work work;
  const struct gt911 *dev;
  uint8_t retries;
  bool home;
  bool down;
  uint16_t x;
  uint16_t y;
};

static int prv_read(const struct gt911 *dev, uint16_t reg, uint8_t *buf, size_t len) {
  const uint8_t address[] = {reg >> 8, (uint8_t)reg};
  return pbl_i2c_write_read(&dev->i2c, address, sizeof(address), buf, len);
}

// An edge can precede READY. Share one bounded retry window between both edges and I2C errors.
static void prv_retry(struct gt911_state *s) {
  pbl_irq_lock();
  if (s->retries > 0U) {
    --s->retries;
    pbl_delayed_work_schedule(&pbl_sys_workq, &s->work, PBL_MSEC(GT911_RETRY_MS));
  }
  pbl_irq_unlock();
}

static void prv_read_frame(struct pbl_work *work) {
  struct gt911_state *s = PBL_CONTAINER_OF(work, struct gt911_state, work.work);
  const struct gt911 *dev = s->dev;
  uint8_t status;
  uint8_t point[4];
  const uint8_t ack[] = {GT911_STATUS >> 8, (uint8_t)GT911_STATUS, 0};
  int ret = prv_read(dev, GT911_STATUS, &status, 1);
  if (ret != 0 || !(status & GT911_READY)) {
    prv_retry(s);
    return;
  }

  uint8_t count = status & GT911_CONTACTS;
  bool down = count != 0;
  if (down && count <= GT911_MAX_CONTACTS) {
    ret = prv_read(dev, GT911_FIRST_X, point, sizeof(point));
  }
  if (ret == 0) {
    ret = pbl_i2c_write(&dev->i2c, ack, sizeof(ack));
  }
  if (ret != 0) {
    prv_retry(s);
    return;
  }
  if (count > GT911_MAX_CONTACTS) {
    return;
  }

  bool home = (status & GT911_HOME) != 0;
  if (home != s->home) {
    s->home = home;
    pbl_input_report_key(PBL_INPUT_KEY_HOME, home, true);
  }
  if (down) {
    uint16_t x = point[0] | ((uint16_t)point[1] << 8);
    uint16_t y = point[2] | ((uint16_t)point[3] << 8);
    s->x = PBL_MIN(x, dev->touch.props.size_x - 1U);
    s->y = PBL_MIN(y, dev->touch.props.size_y - 1U);
  }
  if ((down || s->down) && pbl_touch_is_enabled(&dev->touch)) {
    pbl_touch_report(&dev->touch, down, s->x, s->y);
  }
  s->down = down;
}

static void prv_irq(void *arg) {
  struct gt911_state *s = arg;
  pbl_irq_lock();
  s->retries = GT911_RETRIES;
  pbl_delayed_work_schedule(&pbl_sys_workq, &s->work, PBL_NO_WAIT);
  pbl_irq_unlock();
}

static int prv_init(const struct pbl_touch *touch) {
  const struct gt911 *dev = PBL_CONTAINER_OF(touch, struct gt911, touch);
  struct gt911_state *s = dev->state;
  int ret = pbl_device_init(&dev->supply->dev);
  if (ret != 0) {
    return ret;
  }
  ret = pbl_regulator_enable(dev->supply);
  if (ret != 0) {
    return ret;
  }
  ret = pbl_device_init(&dev->irq.intc->dev);
  if (ret != 0) {
    goto disable;
  }
  ret = pbl_gpio_configure(&dev->reset, PBL_GPIO_OUTPUT_ACTIVE);
  if (ret != 0) {
    goto disable;
  }
  // The INT level as RESET rises selects the address. Hold it through self-configuration.
  ret = pbl_gpio_configure(&dev->int_pin,
                           dev->i2c.addr == 0x14 ? PBL_GPIO_OUTPUT_HIGH : PBL_GPIO_OUTPUT_LOW);
  if (ret != 0) {
    goto disable;
  }
  pbl_sleep_ms(10);
  ret = pbl_gpio_set(&dev->reset, false);
  if (ret != 0) {
    goto disable;
  }
  pbl_sleep_ms(10);
  // Synchronize the interrupt line low after the chip samples its address.
  ret = pbl_gpio_set_raw(&dev->int_pin, false);
  if (ret != 0) {
    goto disable;
  }
  pbl_sleep_ms(50);
  ret = pbl_gpio_configure(&dev->int_pin, PBL_GPIO_INPUT);
  if (ret != 0) {
    goto disable;
  }
  pbl_sleep_ms(50);
  uint8_t id[4];
  ret = prv_read(dev, GT911_PRODUCT_ID, id, sizeof(id));
  if (ret != 0 || memcmp(id, "911", 3) != 0) {
    ret = ret != 0 ? ret : -ENODEV;
    goto disable;
  }

  s->dev = dev;
  pbl_delayed_work_init(&s->work, prv_read_frame);
  ret = pbl_intc_request(&dev->irq, prv_irq, s);
  if (ret != 0) {
    goto disable;
  }
  ret = pbl_intc_enable(&dev->irq);
  if (ret != 0) {
    (void)pbl_intc_free(&dev->irq);
    goto disable;
  }
  prv_irq(s);
  return 0;

disable:
  (void)pbl_regulator_disable(dev->supply);
  return ret;
}

// The capacitive Home key shares the digitizer and remains available between touch subscriptions.
static int prv_set_touch(const struct pbl_touch *touch) {
  return 0;
}

static const struct pbl_touch_ops s_ops = {
  .init = prv_init,
  .enable = prv_set_touch,
  .disable = prv_set_touch,
};

#define PBL_GT911_DEFINE(sym, bus, address, intr, int_gpio, rst_gpio, rail, width, height)         \
  PBL_TOUCH_STATE_DEFINE(sym);                                                                     \
  static struct gt911_state sym##_gt911_state;                                                     \
  PBL_DEVICE_INSTANCE(sym, struct gt911, struct pbl_touch, touch) {                                \
    .touch = PBL_TOUCH_INIT(sym, "gt911", &(bus)->dev, &s_ops, .size_x = width, .size_y = height), \
    .i2c = PBL_I2C_DEV(bus, address), .irq = intr, .int_pin = int_gpio, .reset = rst_gpio,         \
    .supply = rail, .state = &sym##_gt911_state,                                                   \
  }

#include PBL_DT_INSTANCES(goodix_gt911)
