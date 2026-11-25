# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_adc_keys
  SOURCES
    drivers/base/device.c
    drivers/input/adc_keys.c
  DEFINES
    CONFIG_DEVICE_LOG_LEVEL=0
    CONFIG_INPUT_ADC_KEYS_IDLE_POLL_MS=80
    CONFIG_INPUT_ADC_KEYS_IDLE_DELAY_MS=2000
)
