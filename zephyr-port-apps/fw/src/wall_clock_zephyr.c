/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <time.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/timeutil.h>

// pt2 labels its RTC "rtc"; qemu_emery labels it "rtc0".
#if DT_NODE_EXISTS(DT_NODELABEL(rtc))
#define FW_RTC_NODE DT_NODELABEL(rtc)
#else
#define FW_RTC_NODE DT_NODELABEL(rtc0)
#endif

static const struct device *const s_rtc = DEVICE_DT_GET(FW_RTC_NODE);

#define SF32LB_RTC_RESET_EPOCH 946684800

static bool s_wall_clock_initialized;
static time_t s_wall_clock_offset;

static bool prv_read_rtc(time_t *timestamp) {
  struct rtc_time rtc_time;
  if (!device_is_ready(s_rtc) || rtc_get_time(s_rtc, &rtc_time) != 0) {
    return false;
  }

  struct tm now = *rtc_time_to_tm(&rtc_time);
  if (now.tm_year >= 0 && now.tm_year < 100) {
    now.tm_year += 100;
  }
#if DT_NODE_HAS_COMPAT(FW_RTC_NODE, sifli_sf32lb_rtc)
  // SF32LB driver returns one-based months; Zephyr-compliant drivers don't.
  if (now.tm_mon >= 1 && now.tm_mon <= 12) {
    now.tm_mon -= 1;
  }
#endif
  if (now.tm_year < 100 || now.tm_year > 137 || now.tm_mon < 0 || now.tm_mon > 11 ||
      now.tm_mday < 1 || now.tm_mday > 31) {
    return false;
  }

  *timestamp = timeutil_timegm(&now);
  return true;
}

time_t kernel_wall_clock_get(void) {
  const time_t uptime_seconds = k_uptime_get() / 1000;
  if (!s_wall_clock_initialized) {
    time_t rtc_time;
    if (prv_read_rtc(&rtc_time) && rtc_time != SF32LB_RTC_RESET_EPOCH) {
      s_wall_clock_offset = rtc_time - uptime_seconds;
    } else {
      s_wall_clock_offset = FW_BUILD_EPOCH;
    }
    s_wall_clock_initialized = true;
  }
  return s_wall_clock_offset + uptime_seconds;
}
