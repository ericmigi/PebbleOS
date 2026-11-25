/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/rtc.h>
#include <pbl/services/time.h>
#include <pbl/util/misc.h>

struct pcf8563 {
  struct pbl_rtc rtc;
  struct pbl_i2c_dev chip;
};

static int prv_decode(uint8_t value, int min, int max) {
  int n = (value >> 4) * 10 + (value & 15);
  return (value & 15) > 9 || n < min || n > max ? -1 : n;
}

static uint8_t prv_encode(int n) {
  return (n / 10) * 16 + n % 10;
}

static int prv_get(const struct pbl_rtc *rtc, time_t *time) {
  const struct pcf8563 *dev = PBL_CONTAINER_OF(rtc, const struct pcf8563, rtc);
  uint8_t b[7];
  int ret = pbl_i2c_burst_read(&dev->chip, 2, b, sizeof(b));
  if (ret) {
    return ret;
  }
  if (b[0] & 0x80) {
    return -ENODATA;
  }
  struct pbl_tm tm = {
    .tm_sec = prv_decode(b[0] & 0x7f, 0, 59),
    .tm_min = prv_decode(b[1] & 0x7f, 0, 59),
    .tm_hour = prv_decode(b[2] & 0x3f, 0, 23),
    .tm_mday = prv_decode(b[3] & 0x3f, 1, 31),
    .tm_mon = prv_decode(b[5] & 0x1f, 1, 12) - 1,
    .tm_year = prv_decode(b[6], 0, 99) + 100,
  };
  if (tm.tm_sec < 0 || tm.tm_min < 0 || tm.tm_hour < 0 || tm.tm_mday < 1 || tm.tm_mon < 0 ||
      tm.tm_year < 100) {
    return -ENODATA;
  }
  const int day = tm.tm_mday;
  time_t decoded = pbl_mktime(&tm);
  if (decoded == (time_t)-1 || tm.tm_mday != day) {
    return -ENODATA;
  }
  *time = decoded;
  return 0;
}

static int prv_set(const struct pbl_rtc *rtc, time_t time) {
  const struct pcf8563 *dev = PBL_CONTAINER_OF(rtc, const struct pcf8563, rtc);
  struct pbl_tm tm;
  pbl_gmtime_r(&time, &tm);
  if (tm.tm_year < 100 || tm.tm_year > 199) {
    return -EINVAL;
  }
  const uint8_t b[] = {
    prv_encode(tm.tm_sec),
    prv_encode(tm.tm_min),
    prv_encode(tm.tm_hour),
    prv_encode(tm.tm_mday),
    tm.tm_wday,
    prv_encode(tm.tm_mon + 1),
    prv_encode(tm.tm_year - 100)
  };
  return pbl_i2c_burst_write(&dev->chip, 2, b, sizeof(b));
}

static const struct pbl_rtc_ops s_ops = {.get_time = prv_get, .set_time = prv_set};

#define PCF8563_DEFINE(sym, bus, addr)                                \
  PBL_DEVICE_STATE_DEFINE(sym);                                       \
  PBL_DEVICE_INSTANCE(sym, struct pcf8563, struct pbl_rtc, rtc) {     \
    .rtc = PBL_RTC_INIT(sym, "bm8563", nullptr, &(bus)->dev, &s_ops), \
    .chip = PBL_I2C_DEV(bus, addr),                                   \
  }

#include PBL_DT_INSTANCES(nxp_pcf8563)
