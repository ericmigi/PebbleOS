/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>

#define TEST_OFFSET 0x10000
#define PATTERN_LEN 300

int main(void)
{
	const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(extflash0));
	uint8_t buf[PATTERN_LEN];
	uint8_t pattern[PATTERN_LEN];
	int rc;

	if (!device_is_ready(flash)) {
		printk("FAIL: device not ready\n");
		return 0;
	}

	for (int i = 0; i < PATTERN_LEN; i++) {
		pattern[i] = (uint8_t)(0xA5 ^ i);
	}

	/* Step 1: read before erase (also shows persistence across restarts) */
	rc = flash_read(flash, TEST_OFFSET, buf, sizeof(buf));
	printk("step1 read: rc=%d first bytes %02x %02x %02x %02x\n", rc, buf[0], buf[1], buf[2],
	       buf[3]);
	printk("step1 %s\n", rc == 0 ? "PASS" : "FAIL");
	printk("persisted-pattern: %s\n",
	       memcmp(buf, pattern, PATTERN_LEN) == 0 ? "YES" : "NO");

	/* Step 2: erase 4KB and verify 0xff */
	rc = flash_erase(flash, TEST_OFFSET, 4096);
	if (rc == 0) {
		rc = flash_read(flash, TEST_OFFSET, buf, sizeof(buf));
	}
	for (int i = 0; rc == 0 && i < PATTERN_LEN; i++) {
		if (buf[i] != 0xff) {
			rc = -EIO;
		}
	}
	printk("step2 erase+blank-check %s (rc=%d)\n", rc == 0 ? "PASS" : "FAIL", rc);

	/* Step 3: write 300 bytes crossing a 256-byte page boundary, verify */
	rc = flash_write(flash, TEST_OFFSET, pattern, PATTERN_LEN);
	if (rc == 0) {
		rc = flash_read(flash, TEST_OFFSET, buf, sizeof(buf));
	}
	if (rc == 0 && memcmp(buf, pattern, PATTERN_LEN) != 0) {
		rc = -EIO;
	}
	printk("step3 write+verify %s (rc=%d)\n", rc == 0 ? "PASS" : "FAIL", rc);

	printk("done\n");
	return 0;
}
