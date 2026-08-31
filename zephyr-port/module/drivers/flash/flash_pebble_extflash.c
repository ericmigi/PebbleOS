/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT pebble_extflash

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#include <string.h>

/* Register map must match hw/flash/pebble_extflash in qemu-pebble
 * (see src/fw/drivers/qemu/qemu_flash_hal.c for the reference protocol).
 */
#define FLASH_CMD      0x00
#define FLASH_ADDR     0x04
#define FLASH_STATUS   0x08
#define FLASH_SYNC_LEN 0x18
#define FLASH_SYNC     0x1C

#define CMD_ERASE_SUBSECTOR 1

#define STATUS_BUSY BIT(0)

#define SUBSECTOR_SIZE 4096
#define PAGE_SIZE      256

struct extflash_config {
	mm_reg_t regs;
	mm_reg_t xip;
	size_t size;
};

static int extflash_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	const struct extflash_config *config = dev->config;

	if (offset < 0 || (size_t)offset + len > config->size) {
		return -EINVAL;
	}

	memcpy(data, (const void *)(config->xip + offset), len);
	return 0;
}

static int extflash_write(const struct device *dev, off_t offset, const void *data, size_t len)
{
	const struct extflash_config *config = dev->config;
	const uint8_t *src = data;

	if (offset < 0 || (size_t)offset + len > config->size) {
		return -EINVAL;
	}

	/* Write directly into the XIP window, one flash page at a time, then
	 * ask QEMU to persist that range to the backing file (SYNC handshake).
	 */
	while (len > 0) {
		mm_reg_t dst = config->xip + offset;
		size_t chunk = MIN(len, PAGE_SIZE - (offset & (PAGE_SIZE - 1)));

		for (size_t i = 0; i < chunk; i++) {
			sys_write8(src[i], dst + i);
		}

		sys_write32(chunk, config->regs + FLASH_SYNC_LEN);
		sys_write32(dst, config->regs + FLASH_SYNC);

		src += chunk;
		offset += chunk;
		len -= chunk;
	}

	return 0;
}

static int extflash_erase(const struct device *dev, off_t offset, size_t size)
{
	const struct extflash_config *config = dev->config;

	if (offset < 0 || (size_t)offset + size > config->size ||
	    (offset | size) & (SUBSECTOR_SIZE - 1)) {
		return -EINVAL;
	}

	for (size_t erased = 0; erased < size; erased += SUBSECTOR_SIZE) {
		sys_write32(config->xip + offset + erased, config->regs + FLASH_ADDR);
		sys_write32(CMD_ERASE_SUBSECTOR, config->regs + FLASH_CMD);
		while (sys_read32(config->regs + FLASH_STATUS) & STATUS_BUSY) {
		}
	}

	return 0;
}

static const struct flash_parameters *extflash_get_parameters(const struct device *dev)
{
	static const struct flash_parameters parameters = {
		.write_block_size = 1,
		.erase_value = 0xff,
	};

	ARG_UNUSED(dev);
	return &parameters;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static void extflash_page_layout(const struct device *dev, const struct flash_pages_layout **layout,
				 size_t *layout_size)
{
	const struct extflash_config *config = dev->config;
	static struct flash_pages_layout pages_layout = {
		.pages_size = SUBSECTOR_SIZE,
	};

	pages_layout.pages_count = config->size / SUBSECTOR_SIZE;
	*layout = &pages_layout;
	*layout_size = 1;
}
#endif

static DEVICE_API(flash, extflash_api) = {
	.read = extflash_read,
	.write = extflash_write,
	.erase = extflash_erase,
	.get_parameters = extflash_get_parameters,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = extflash_page_layout,
#endif
};

static const struct extflash_config extflash_config_0 = {
	.regs = DT_INST_REG_ADDR_BY_IDX(0, 0),
	.xip = DT_INST_REG_ADDR_BY_IDX(0, 1),
	.size = DT_INST_REG_SIZE_BY_IDX(0, 1),
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, &extflash_config_0, POST_KERNEL,
		      CONFIG_FLASH_INIT_PRIORITY, &extflash_api);
