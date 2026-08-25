/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#ifdef CONFIG_PEBBLE_ZEPHYR_CORE_BOOT

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>

#include <pbl/drivers/watchdog.h>
#include "system/passert.h"

#define WATCHDOG_TIMEOUT_MS 10000U

static const struct device *const s_watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int s_channel_id = -ENODEV;
static McuRebootReason s_cached_reset_flag;

void watchdog_init(void) {
  PBL_ASSERTN(device_is_ready(s_watchdog));

  const struct wdt_timeout_cfg timeout = {
      .window = {
          .min = 0U,
          .max = WATCHDOG_TIMEOUT_MS,
      },
      .callback = NULL,
      .flags = WDT_FLAG_RESET_SOC,
  };

  s_channel_id = wdt_install_timeout(s_watchdog, &timeout);
  PBL_ASSERTN(s_channel_id >= 0);
}

void watchdog_start(void) {
  PBL_ASSERTN(s_channel_id >= 0);
  PBL_ASSERTN(wdt_setup(s_watchdog, 0U) == 0);
}

void watchdog_stop(void) {
  PBL_ASSERTN(wdt_disable(s_watchdog) == 0);
}

void watchdog_feed(void) {
  PBL_ASSERTN(s_channel_id >= 0);
  PBL_ASSERTN(wdt_feed(s_watchdog, s_channel_id) == 0);
}

bool watchdog_check_reset_flag(void) {
  return s_cached_reset_flag.window_watchdog_reset;
}

McuRebootReason watchdog_clear_reset_flag(void) {
  // ponytail: Zephyr's SF32LB watchdog driver programs and feeds WDT1 but does
  // not yet expose PMUC_WSR reset-cause bits. Add an SF32LB hwinfo reset-cause
  // driver, then populate and clear s_cached_reset_flag through that API.
  s_cached_reset_flag = (McuRebootReason){0};
  return s_cached_reset_flag;
}

McuRebootReason watchdog_get_reset_flag(void) {
  return s_cached_reset_flag;
}

#endif  // CONFIG_PEBBLE_ZEPHYR_CORE_BOOT
