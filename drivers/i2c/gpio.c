/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/kernel/delay.h>
#include <pbl/util/misc.h>

struct gpio_i2c {
  struct pbl_i2c_bus bus;
  struct pbl_gpio scl;
  struct pbl_gpio sda;
  unsigned int half_us;
};

static int prv_init(const struct pbl_i2c_bus *bus) {
  const struct gpio_i2c *i = PBL_CONTAINER_OF(bus, const struct gpio_i2c, bus);
  const uint32_t flags = PBL_GPIO_OPEN_DRAIN | PBL_GPIO_INPUT | PBL_GPIO_OUTPUT_HIGH;
  int ret = pbl_gpio_configure(&i->sda, flags);
  return ret ? ret : pbl_gpio_configure(&i->scl, flags);
}

static int prv_raise_clock(const struct gpio_i2c *i) {
  pbl_gpio_set_raw(&i->scl, true);
  return PBL_POLL_BUSY_US(pbl_gpio_get_raw(&i->scl) == 1, 1, 1000);
}

static int prv_start_bit(const struct gpio_i2c *i) {
  pbl_gpio_set_raw(&i->sda, true);
  pbl_busy_wait_us(i->half_us);
  int ret = prv_raise_clock(i);
  if (ret) {
    return ret;
  }
  pbl_busy_wait_us(i->half_us);
  if (pbl_gpio_get_raw(&i->sda) != 1) {
    return -EBUSY;
  }
  pbl_gpio_set_raw(&i->sda, false);
  pbl_busy_wait_us(i->half_us);
  pbl_gpio_set_raw(&i->scl, false);
  return 0;
}

static int prv_stop_bit(const struct gpio_i2c *i) {
  pbl_gpio_set_raw(&i->sda, false);
  pbl_busy_wait_us(i->half_us);
  int ret = prv_raise_clock(i);
  pbl_busy_wait_us(i->half_us);
  pbl_gpio_set_raw(&i->sda, true);
  pbl_busy_wait_us(i->half_us);
  return ret;
}

// A peripheral can still be sending a byte after the controller resets mid-read.
static int prv_clear_bus(const struct gpio_i2c *i) {
  pbl_gpio_set_raw(&i->sda, true);
  for (unsigned pulse = 0; pulse < 9 && pbl_gpio_get_raw(&i->sda) == 0; ++pulse) {
    pbl_gpio_set_raw(&i->scl, false);
    pbl_busy_wait_us(i->half_us);
    int ret = prv_raise_clock(i);
    if (ret < 0) {
      return ret;
    }
    pbl_busy_wait_us(i->half_us);
  }
  pbl_gpio_set_raw(&i->scl, false);
  return prv_stop_bit(i);
}

static int prv_bit(const struct gpio_i2c *i, bool level) {
  pbl_gpio_set_raw(&i->sda, level);
  pbl_busy_wait_us(i->half_us);
  int ret = prv_raise_clock(i);
  if (!ret) {
    pbl_busy_wait_us(i->half_us);
    ret = pbl_gpio_get_raw(&i->sda);
  }
  pbl_gpio_set_raw(&i->scl, false);
  return ret;
}

static int prv_write(const struct gpio_i2c *i, uint8_t value, int nack_error) {
  for (unsigned mask = 0x80; mask; mask >>= 1) {
    int ret = prv_bit(i, value & mask);
    if (ret < 0) {
      return ret;
    }
  }
  int ack = prv_bit(i, true);
  return ack == 1 ? nack_error : ack;
}

static int prv_read(const struct gpio_i2c *i, uint8_t *value, bool last) {
  uint8_t byte = 0;
  for (unsigned n = 0; n < 8; ++n) {
    int bit = prv_bit(i, true);
    if (bit < 0) {
      return bit;
    }
    byte = (byte << 1) | bit;
  }
  *value = byte;
  int ret = prv_bit(i, last);
  return ret < 0 ? ret : 0;
}

static int prv_transfer(const struct pbl_i2c_bus *bus, uint16_t addr, struct pbl_i2c_msg *msgs,
                        uint8_t count) {
  const struct gpio_i2c *i = PBL_CONTAINER_OF(bus, const struct gpio_i2c, bus);
  int ret = 0;
  if (addr > 0x7f) {
    return -EINVAL;
  }
  for (unsigned m = 0; !ret && m < count; ++m) {
    bool read = msgs[m].flags & PBL_I2C_MSG_READ;
    if (!(msgs[m].flags & PBL_I2C_MSG_NOSTART)) {
      ret = prv_start_bit(i);
      if (m == 0 && ret == -EBUSY) {
        ret = prv_clear_bus(i);
        if (!ret) {
          ret = prv_start_bit(i);
        }
      }
      if (!ret) {
        ret = prv_write(i, (addr << 1) | read, -ENXIO);
      }
    }
    for (uint32_t n = 0; !ret && n < msgs[m].len; ++n) {
      if (read) {
        ret = prv_read(i, &msgs[m].buf[n], n + 1 == msgs[m].len);
      } else {
        ret = prv_write(i, msgs[m].buf[n], -EIO);
      }
    }
  }
  int stop = prv_stop_bit(i);
  pbl_i2c_bus_complete(bus, ret ? ret : stop);
  return 0;
}

static void prv_noop(const struct pbl_i2c_bus *bus) {
  (void)bus;
}

static const struct pbl_i2c_bus_ops s_ops = {
  .init = prv_init,
  .start = prv_transfer,
  .enable = prv_noop,
  .disable = prv_noop,
  .abort = prv_noop,
};

#define PBL_I2C_GPIO_DEFINE(sym, _name, _scl, _sda, _half_us)                       \
  PBL_I2C_BUS_STATE_DEFINE(sym);                                                    \
  PBL_DEVICE_INSTANCE(sym, struct gpio_i2c, struct pbl_i2c_bus, bus) {              \
    .bus = PBL_I2C_BUS_INIT(sym, _name, nullptr, &s_ops), .scl = _scl, .sda = _sda, \
    .half_us = _half_us,                                                            \
  }

#include PBL_DT_INSTANCES(i2c_gpio)
