/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/charger.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/misc.h>

// X4 Pro board wiring: charge STAT plus the S3's native USB frame counter.
#define USB_FRAME_NUMBER 0x60038024U

struct x4pro_charger {
  struct pbl_charger charger;
  struct pbl_i2c_dev gauge;
  struct pbl_gpio stat;
};

static const struct x4pro_charger *prv_dev(const struct pbl_charger *charger) {
  return PBL_CONTAINER_OF(charger, const struct x4pro_charger, charger);
}

static int prv_init(const struct pbl_charger *charger) {
  return pbl_gpio_configure(&prv_dev(charger)->stat, PBL_GPIO_INPUT);
}

static int prv_online(const struct pbl_charger *charger, bool *online) {
  int stat = pbl_gpio_get(&prv_dev(charger)->stat);
  if (stat < 0) {
    return stat;
  }
  *online = stat != 0;
  if (!*online) {
    uint32_t frame = pbl_readl(USB_FRAME_NUMBER);
    pbl_sleep_ms(3);
    *online = frame != pbl_readl(USB_FRAME_NUMBER);
  }
  return 0;
}

static int prv_status(const struct pbl_charger *charger, enum pbl_charger_status *status) {
  int stat = pbl_gpio_get(&prv_dev(charger)->stat);
  if (stat < 0) {
    return stat;
  }
  *status = stat ? PBL_CHARGER_STATUS_CONSTANT_CURRENT : PBL_CHARGER_STATUS_UNKNOWN;
  return 0;
}

static int prv_read(const struct pbl_charger *charger, struct pbl_charger_measurements *meas) {
  // Read VERSION through SOC_ALERT in one snapshot. A cold, unprofiled gauge
  // can report 0% even with a full cell. Fall back to the voltage curve then.
  uint8_t data[12];
  int ret = pbl_i2c_burst_read(&prv_dev(charger)->gauge, 0, data, sizeof(data));
  if (ret != 0) {
    return ret;
  }
  if ((data[0] & 0xfd) != 0x0d || data[8] != 0) {
    return -EAGAIN;
  }
  meas->voltage_mv = ((((data[2] & 0x3f) << 8 | data[3]) * 5) + 8) >> 4;
  meas->soc_percent = (data[11] & 0x80) && data[4] <= 100 ? data[4] : -1;
  meas->temp_mc = (int)data[6] * 500 - 40000;
  meas->current_ua = 0;
  meas->current_unavailable = true;
  return 0;
}

static int prv_enable(const struct pbl_charger *charger, bool enable) {
  return enable ? 0 : -ENOTSUP;
}

static const struct pbl_charger_ops s_ops = {
  .init = prv_init,
  .is_online = prv_online,
  .get_status = prv_status,
  .get_measurements = prv_read,
  .enable = prv_enable,
};

#define X4PRO_CHARGER_DEFINE(sym, bus, addr, pin)                               \
  PBL_CHARGER_STATE_DEFINE(sym);                                                \
  PBL_DEVICE_INSTANCE(sym, struct x4pro_charger, struct pbl_charger, charger) { \
    .charger = PBL_CHARGER_INIT(sym, "cw2017", &(bus)->dev, &s_ops),            \
    .gauge = PBL_I2C_DEV(bus, addr), .stat = pin,                               \
  }

#include PBL_DT_INSTANCES(xteink_x4_pro_charger)
