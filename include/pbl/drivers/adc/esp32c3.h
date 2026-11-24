/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/adc.h>
#include <pbl/drivers/pinctrl.h>

/**
 * @defgroup drivers_adc_esp32c3 ESP32-C3 SAR ADC
 * @ingroup drivers_adc
 * @brief @ref drivers_adc driver for SAR ADC1 of the ESP32-C3, one-shot conversions.
 *
 * Channels 0 to 4 are GPIO0 to GPIO4, each at its own attenuation. The offset code and the
 * transfer come from the factory calibration in eFuse BLOCK2; a part without it gets a nominal
 * transfer. ADC2 is not used.
 * @{
 */

/** @cond INTERNAL_HIDDEN */
#define PBL_ADC_ESP32C3__NUM_CHANNELS 5U
#define PBL_ADC_ESP32C3__NUM_ATTENS   4U

// Per attenuation: the offset code, and the code read at a reference voltage
struct pbl_adc_esp32c3_state {
  bool calibrated;
  uint16_t init_code[PBL_ADC_ESP32C3__NUM_ATTENS];
  int32_t cal_raw[PBL_ADC_ESP32C3__NUM_ATTENS];
};

struct pbl_adc_esp32c3 {
  struct pbl_adc adc;
  uintptr_t base;
  struct pbl_pinctrl pinctrl;
  const uint8_t *atten;
  struct pbl_adc_esp32c3_state *state;
};

extern const struct pbl_adc_ops pbl_adc_esp32c3_ops;
/** @endcond */

/**
 * @brief Define the ESP32-C3 SAR ADC.
 *
 * @param sym Symbol of the ADC.
 * @param _name Name.
 * @param _base APB_SARADC registers.
 * @param _pins Pin state putting the channels' pads in analog mode, or nullptr.
 * @param ... Attenuation of each channel: 0, 1, 2 or 3 for 0, 2.5, 6 or 11 dB.
 */
#define PBL_ADC_ESP32C3_DEFINE(sym, _name, _base, _pins, ...)                                      \
  PBL_ADC_STATE_DEFINE(sym);                                                                       \
  static const uint8_t sym##_atten[PBL_ADC_ESP32C3__NUM_CHANNELS] = {__VA_ARGS__};                 \
  static struct pbl_adc_esp32c3_state sym##_esp32c3_state;                                         \
  PBL_DEVICE_INSTANCE(sym, struct pbl_adc_esp32c3, struct pbl_adc, adc) {                          \
    .adc = PBL_ADC_INIT(sym, _name, nullptr, &pbl_adc_esp32c3_ops, PBL_ADC_ESP32C3__NUM_CHANNELS), \
    .base = (_base), .pinctrl = PBL_PINCTRL(_pins, nullptr, 0U), .atten = sym##_atten,             \
    .state = &sym##_esp32c3_state,                                                                 \
  }

/** @} */
