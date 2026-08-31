/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#define PAGE_SIZE_BYTES 0x100u

#define SECTOR_SIZE_BYTES 0x10000u
#define SECTOR_ADDR_MASK (~(SECTOR_SIZE_BYTES - 1u))

#define SUBSECTOR_SIZE_BYTES 0x1000u
#define SUBSECTOR_ADDR_MASK (~(SUBSECTOR_SIZE_BYTES - 1u))

#ifdef CONFIG_BOARD_QEMU_EMERY

// qemu-pebble emery layout (mirrors src/fw/flash_region/flash_region_qemu.h),
// the exact layout the FreeRTOS `waf qemu_image_spi` target writes into the
// backing SPI image: 32MB chip mapped at XIP base 0x10000000.
#define FLASH_REGION_BASE_ADDRESS 0x10000000u

#define FLASH_REGION_FIRMWARE_SLOT_0_BEGIN 0x10020000u
#define FLASH_REGION_FIRMWARE_SLOT_0_END 0x10320000u
#define FLASH_REGION_FIRMWARE_SLOT_1_BEGIN 0x10320000u
#define FLASH_REGION_FIRMWARE_SLOT_1_END 0x10620000u
#define FLASH_REGION_SAFE_FIRMWARE_BEGIN 0x10a20000u
#define FLASH_REGION_SAFE_FIRMWARE_END 0x10ab0000u

// Carved from the CD region; not part of the FreeRTOS layout. Only touched by
// the off-by-default OTA flash-path self-test.
#define FLASH_REGION_OTA_SCRATCH_BEGIN 0x11f40000u
#define FLASH_REGION_OTA_SCRATCH_END 0x11f80000u

// The real 21056K PFS the FreeRTOS image ships (apps, appdb, settings).
#define FLASH_REGION_FILESYSTEM_BEGIN 0x10ab0000u
#define FLASH_REGION_FILESYSTEM_END 0x11f40000u
#define FLASH_FILESYSTEM_BLOCK_SIZE SUBSECTOR_SIZE_BYTES

#define FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x11fff000u
#define FLASH_REGION_SHARED_PRF_STORAGE_END 0x12000000u

#else

#define FLASH_REGION_BASE_ADDRESS 0x12000000u

// pblboot pt2 partitions (see pblboot/boot/boards/pt2.overlay). This app runs
// XIP from slot0 and streams self-OTA images into slot1. pblboot validates the
// in-image header and boots the valid slot with the higher 64-bit priority.
#define FLASH_REGION_FIRMWARE_SLOT_0_BEGIN 0x12020000u
#define FLASH_REGION_FIRMWARE_SLOT_0_END 0x12320000u
#define FLASH_REGION_FIRMWARE_SLOT_1_BEGIN 0x12320000u
#define FLASH_REGION_FIRMWARE_SLOT_1_END 0x12620000u
#define FLASH_REGION_SAFE_FIRMWARE_BEGIN 0x12a20000u
#define FLASH_REGION_SAFE_FIRMWARE_END 0x12ab0000u

// Dedicated OTA staging scratch for the flash-path self-test. Deliberately NOT
// a bootable pblboot slot and clear of the PFS window (0x13e00000), so a
// committed non-executable test image never bricks boot. 256K, chip-resident.
#define FLASH_REGION_OTA_SCRATCH_BEGIN 0x13d00000u
#define FLASH_REGION_OTA_SCRATCH_END 0x13d40000u

#define FLASH_REGION_FILESYSTEM_BEGIN 0x13e00000u
#define FLASH_REGION_FILESYSTEM_END 0x13e40000u
#define FLASH_FILESYSTEM_BLOCK_SIZE SUBSECTOR_SIZE_BYTES

// Shipping obelix shared-PRF sector. Read-only in the Zephyr firmware app.
#define FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x13fff000u
#define FLASH_REGION_SHARED_PRF_STORAGE_END 0x14000000u

#endif

void flash_region_erase_optimal_range(uint32_t min_start, uint32_t max_start,
                                      uint32_t min_end, uint32_t max_end);
void flash_region_erase_optimal_range_no_watchdog(uint32_t min_start,
                                                  uint32_t max_start,
                                                  uint32_t min_end,
                                                  uint32_t max_end);
