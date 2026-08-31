/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT pebble_buttons

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>

/* Register map must match the pebble-gpio device in qemu-pebble */
#define GPIO_BTN_STATE 0x00
#define GPIO_BTN_EDGE  0x04
#define GPIO_INTCTRL   0x08
#define GPIO_INTSTAT   0x0C

#define BASE DT_INST_REG_ADDR(0)

static const uint16_t button_codes[] = {
	INPUT_KEY_BACK,  /* bit 0 */
	INPUT_KEY_UP,    /* bit 1 */
	INPUT_KEY_ENTER, /* bit 2: select */
	INPUT_KEY_DOWN,  /* bit 3 */
};

static uint32_t last_state;

static void pebble_buttons_isr(const struct device *dev)
{
	uint32_t edge = sys_read32(BASE + GPIO_BTN_EDGE);

	sys_write32(edge, BASE + GPIO_INTSTAT);

	uint32_t state = sys_read32(BASE + GPIO_BTN_STATE);
	uint32_t changed = last_state ^ state;

	last_state = state;

	for (int i = 0; i < ARRAY_SIZE(button_codes); i++) {
		if (changed & BIT(i)) {
			input_report_key(dev, button_codes[i], (state & BIT(i)) != 0, true,
					 K_FOREVER);
		}
	}
}

static int pebble_buttons_init(const struct device *dev)
{
	sys_write32(0xF, BASE + GPIO_INTSTAT);
	sys_write32(1, BASE + GPIO_INTCTRL);

	last_state = sys_read32(BASE + GPIO_BTN_STATE);

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), pebble_buttons_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	return 0;
}

DEVICE_DT_INST_DEFINE(0, pebble_buttons_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_INPUT_INIT_PRIORITY, NULL);
