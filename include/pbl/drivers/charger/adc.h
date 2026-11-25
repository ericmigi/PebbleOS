/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/adc.h>
#include <pbl/drivers/charger.h>
#include <pbl/drivers/gpio.h>

/**
 * @defgroup drivers_charger_adc Charger seen through a pin and an ADC
 * @ingroup drivers_charger
 * @brief A charger the firmware cannot talk to: a pin says whether external power is there, and
 * an ADC channel reads the battery through a divider.
 *
 * With no charge status to read, external power with the battery below 4.15 V reports as
 * constant-current charging, and above it as full.
 * @{
 */

/** @cond INTERNAL_HIDDEN */
struct pbl_charger_adc {
  struct pbl_charger charger;
  /** External power present, @c gpios. */
  struct pbl_gpio online;
  /** Battery through the divider, @c io-channels. */
  struct pbl_adc_channel channel;
  /** Divider, @c full-ohms over @c output-ohms. */
  uint32_t full_ohms;
  uint32_t output_ohms;
};

extern const struct pbl_charger_ops pbl_charger_adc_ops;

#define PBL_CHARGER_ADC_DEFINE(sym, _name, _online, _channel, _output_ohms, _full_ohms)        \
  PBL_CHARGER_STATE_DEFINE(sym);                                                               \
  PBL_DEVICE_INSTANCE(sym, struct pbl_charger_adc, struct pbl_charger, charger) {              \
    .charger = PBL_CHARGER_INIT(sym, _name, nullptr, &pbl_charger_adc_ops), .online = _online, \
    .channel = _channel, .full_ohms = (_full_ohms), .output_ohms = (_output_ohms),             \
  }
/** @endcond */

/** @} */
