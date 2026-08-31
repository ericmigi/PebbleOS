/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT pebble_rtc

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/timeutil.h>

/* Register map must match the pebble-rtc device in qemu-pebble */
#define RTC_TIME_LO 0x00

#define BASE DT_INST_REG_ADDR(0)

static int pebble_rtc_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
	int64_t t = timeutil_timegm64((const struct tm *)timeptr);

	if (t < 0 || t > UINT32_MAX) {
		return -EINVAL;
	}

	sys_write32((uint32_t)t, BASE + RTC_TIME_LO);
	return 0;
}

static int pebble_rtc_get_time(const struct device *dev, struct rtc_time *timeptr)
{
	time_t t = (time_t)sys_read32(BASE + RTC_TIME_LO);

	gmtime_r(&t, rtc_time_to_tm(timeptr));
	timeptr->tm_nsec = 0;
	return 0;
}

static DEVICE_API(rtc, pebble_rtc_api) = {
	.set_time = pebble_rtc_set_time,
	.get_time = pebble_rtc_get_time,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_RTC_INIT_PRIORITY,
		      &pebble_rtc_api);
