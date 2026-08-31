/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/rtc.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

static void input_cb(struct input_event *evt, void *user_data)
{
	printk("BTN %u %d\n", evt->code, evt->value);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(buttons0)), input_cb, NULL);

int main(void)
{
	const struct device *rtc = DEVICE_DT_GET(DT_NODELABEL(rtc0));
	struct rtc_time t;

	for (int i = 0; i < 2; i++) {
		int ret = rtc_get_time(rtc, &t);

		printk("RTC %d: %04d-%02d-%02d %02d:%02d:%02d\n", ret, t.tm_year + 1900,
		       t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
		k_sleep(K_SECONDS(2));
	}
	printk("READY\n");
	return 0;
}
