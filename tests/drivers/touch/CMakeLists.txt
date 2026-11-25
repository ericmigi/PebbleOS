# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_touch_class
  SOURCES
    drivers/base/device.c
    drivers/touch/touch.c
    subsys/input/input.c
  DEFINES
    CONFIG_DEVICE_LOG_LEVEL=0
)

pbl_clar_test(test_gt911
  SOURCES
    drivers/base/device.c
    drivers/touch/touch.c
    drivers/touch/gt911.c
    subsys/input/input.c
  INCLUDES
    tests/drivers/touch/fixtures
  DEFINES
    CONFIG_DEVICE_LOG_LEVEL=0
)
