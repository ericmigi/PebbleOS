/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT pebble_simple_uart

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/sys_io.h>

/* Register map must match hw/char/pebble_simple_uart.c in qemu-pebble */
#define UART_DATA  0x00
#define UART_STATE 0x04

#define STATE_TX_READY BIT(0)
#define STATE_RX_READY BIT(1)

struct pebble_uart_config {
	mm_reg_t base;
};

static int pebble_uart_poll_in(const struct device *dev, unsigned char *c)
{
	const struct pebble_uart_config *config = dev->config;

	if (!(sys_read32(config->base + UART_STATE) & STATE_RX_READY)) {
		return -1;
	}

	*c = (unsigned char)sys_read32(config->base + UART_DATA);
	return 0;
}

static void pebble_uart_poll_out(const struct device *dev, unsigned char c)
{
	const struct pebble_uart_config *config = dev->config;

	while (!(sys_read32(config->base + UART_STATE) & STATE_TX_READY)) {
	}

	sys_write32(c, config->base + UART_DATA);
}

static DEVICE_API(uart, pebble_uart_api) = {
	.poll_in = pebble_uart_poll_in,
	.poll_out = pebble_uart_poll_out,
};

#define PEBBLE_UART_INIT(n)                                                    \
	static const struct pebble_uart_config pebble_uart_config_##n = {      \
		.base = DT_INST_REG_ADDR(n),                                   \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &pebble_uart_config_##n,    \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,       \
			      &pebble_uart_api);

DT_INST_FOREACH_STATUS_OKAY(PEBBLE_UART_INIT)
