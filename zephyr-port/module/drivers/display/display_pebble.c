/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT pebble_display

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

/* Register map must match hw/display/pebble_display.c in qemu-pebble
 * (see src/fw/drivers/qemu/qemu_display_hal.c)
 */
#define DISP_CTRL       0x000
#define DISP_STATUS     0x004
#define DISP_WIDTH      0x008
#define DISP_HEIGHT     0x00C
#define DISP_FORMAT     0x010
#define DISP_FLAGS      0x014
#define DISP_BRIGHTNESS 0x018
#define DISP_INT_STATUS 0x01C
#define DISP_INT_CTRL   0x020

#define CTRL_ENABLE          BIT(0)
#define CTRL_UPDATE_REQUEST  BIT(1)

#define INT_UPDATE_DONE_PENDING BIT(0)

struct pebble_display_config {
	mm_reg_t base;
	uint8_t *fb;
	uint16_t width;
	uint16_t height;
};

static int pebble_display_write(const struct device *dev, const uint16_t x,
				const uint16_t y,
				const struct display_buffer_descriptor *desc,
				const void *buf)
{
	const struct pebble_display_config *config = dev->config;
	const uint8_t *src = buf;

	if (desc->pitch < desc->width || x + desc->width > config->width ||
	    y + desc->height > config->height ||
	    desc->buf_size < (size_t)desc->pitch * desc->height) {
		return -EINVAL;
	}

	for (uint16_t row = 0; row < desc->height; row++) {
		memcpy(&config->fb[(y + row) * config->width + x],
		       &src[row * desc->pitch], desc->width);
	}

	sys_write32(sys_read32(config->base + DISP_CTRL) | CTRL_UPDATE_REQUEST,
		    config->base + DISP_CTRL);

	return 0;
}

static void pebble_display_get_capabilities(const struct device *dev,
					    struct display_capabilities *caps)
{
	const struct pebble_display_config *config = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	/* Buffers carry Pebble GColor8 (2:2:2:2 ARGB) bytes, one per pixel,
	 * matching the L_8 convention used by the SF32LB JDI driver.
	 */
	caps->supported_pixel_formats = PIXEL_FORMAT_L_8;
	caps->current_pixel_format = PIXEL_FORMAT_L_8;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int pebble_display_set_enabled(const struct device *dev, bool enabled)
{
	const struct pebble_display_config *config = dev->config;
	uint32_t ctrl = sys_read32(config->base + DISP_CTRL);

	if (enabled) {
		ctrl |= CTRL_ENABLE;
	} else {
		ctrl &= ~CTRL_ENABLE;
	}
	sys_write32(ctrl, config->base + DISP_CTRL);

	return 0;
}

static int pebble_display_blanking_on(const struct device *dev)
{
	return pebble_display_set_enabled(dev, false);
}

static int pebble_display_blanking_off(const struct device *dev)
{
	return pebble_display_set_enabled(dev, true);
}

static int pebble_display_set_pixel_format(const struct device *dev,
					   const enum display_pixel_format format)
{
	return (format == PIXEL_FORMAT_L_8) ? 0 : -ENOTSUP;
}

static int pebble_display_read(const struct device *dev, const uint16_t x,
			       const uint16_t y,
			       const struct display_buffer_descriptor *desc,
			       void *buf)
{
	return -ENOTSUP;
}

static int pebble_display_set_brightness(const struct device *dev,
					 const uint8_t brightness)
{
	return -ENOTSUP;
}

static int pebble_display_set_contrast(const struct device *dev,
				       const uint8_t contrast)
{
	return -ENOTSUP;
}

static int pebble_display_set_orientation(const struct device *dev,
					  const enum display_orientation orientation)
{
	return (orientation == DISPLAY_ORIENTATION_NORMAL) ? 0 : -ENOTSUP;
}

static int pebble_display_init(const struct device *dev)
{
	const struct pebble_display_config *config = dev->config;

	sys_write32(INT_UPDATE_DONE_PENDING, config->base + DISP_INT_STATUS);
	sys_write32(CTRL_ENABLE, config->base + DISP_CTRL);

	return 0;
}

static DEVICE_API(display, pebble_display_api) = {
	.blanking_on = pebble_display_blanking_on,
	.blanking_off = pebble_display_blanking_off,
	.write = pebble_display_write,
	.read = pebble_display_read,
	.set_brightness = pebble_display_set_brightness,
	.set_contrast = pebble_display_set_contrast,
	.get_capabilities = pebble_display_get_capabilities,
	.set_pixel_format = pebble_display_set_pixel_format,
	.set_orientation = pebble_display_set_orientation,
};

#define PEBBLE_DISPLAY_INIT(n)                                                 \
	static const struct pebble_display_config pebble_display_config_##n = {\
		.base = DT_INST_REG_ADDR_BY_IDX(n, 0),                         \
		.fb = (uint8_t *)DT_INST_REG_ADDR_BY_IDX(n, 1),                \
		.width = DT_INST_PROP(n, width),                               \
		.height = DT_INST_PROP(n, height),                             \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, pebble_display_init, NULL, NULL,              \
			      &pebble_display_config_##n, POST_KERNEL,         \
			      CONFIG_DISPLAY_INIT_PRIORITY,                    \
			      &pebble_display_api);

DT_INST_FOREACH_STATUS_OKAY(PEBBLE_DISPLAY_INIT)
