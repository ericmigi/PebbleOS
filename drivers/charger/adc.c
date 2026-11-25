/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/devicetree.h>
#include <pbl/drivers/charger/adc.h>
#include <pbl/util/misc.h>

// ponytail: no charge status pin, so a plugged-in battery this close to a 4.2 V charger's end of
// charge counts as full; a board with a status pin would read it instead
#define FULL_MV 4150

static const struct pbl_charger_adc *prv_adc(const struct pbl_charger *charger) {
  return PBL_CONTAINER_OF(charger, const struct pbl_charger_adc, charger);
}

static int prv_init(const struct pbl_charger *charger) {
  const struct pbl_charger_adc *chg = prv_adc(charger);
  int ret;

  ret = pbl_device_init(&chg->channel.adc->dev);
  if (ret == 0) {
    ret = pbl_gpio_configure(&chg->online, PBL_GPIO_INPUT);
  }

  return ret;
}

static int prv_is_online(const struct pbl_charger *charger, bool *online) {
  int ret = pbl_gpio_get(&prv_adc(charger)->online);

  if (ret < 0) {
    return ret;
  }
  *online = (ret != 0);

  return 0;
}

static int prv_read_mv(const struct pbl_charger_adc *chg, int32_t *mv) {
  int32_t uv;
  int ret = pbl_adc_read(&chg->channel, &uv);

  if (ret == 0) {
    *mv = (int32_t)((int64_t)uv * chg->full_ohms / chg->output_ohms / 1000);
  }

  return ret;
}

static int prv_get_status(const struct pbl_charger *charger, enum pbl_charger_status *status) {
  bool online;
  int32_t mv;
  int ret = prv_is_online(charger, &online);

  if (ret == 0 && online) {
    ret = prv_read_mv(prv_adc(charger), &mv);
  }
  if (ret != 0) {
    return ret;
  }

  if (!online) {
    *status = PBL_CHARGER_STATUS_UNKNOWN;
  } else {
    *status = (mv < FULL_MV) ? PBL_CHARGER_STATUS_CONSTANT_CURRENT : PBL_CHARGER_STATUS_FULL;
  }

  return 0;
}

static int prv_get_measurements(const struct pbl_charger *charger,
                                struct pbl_charger_measurements *meas) {
  // Neither current nor temperature can be measured
  meas->current_ua = 0;
  meas->temp_mc = 25000;

  return prv_read_mv(prv_adc(charger), &meas->voltage_mv);
}

// The charger charges whenever there is power: nothing to switch
static int prv_enable(const struct pbl_charger *charger, bool enable) {
  return 0;
}

const struct pbl_charger_ops pbl_charger_adc_ops = {
  .init = prv_init,
  .is_online = prv_is_online,
  .get_status = prv_get_status,
  .get_measurements = prv_get_measurements,
  .enable = prv_enable,
};

#include PBL_DT_INSTANCES(coredevices_adc_charger)
