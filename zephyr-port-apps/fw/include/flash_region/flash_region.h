/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#define PAGE_SIZE_BYTES 0x100u

#define SECTOR_SIZE_BYTES 0x10000u
#define SECTOR_ADDR_MASK (~(SECTOR_SIZE_BYTES - 1u))

#define SUBSECTOR_SIZE_BYTES 0x1000u
#define SUBSECTOR_ADDR_MASK (~(SUBSECTOR_SIZE_BYTES - 1u))

#define FLASH_REGION_BASE_ADDRESS 0x12000000u

// pblboot pt2 partitions (see pblboot/boot/boards/pt2.overlay). Read-only from
// this app: fw_boot_select() inspects these to report the active boot slot.
// pblboot boots the valid slot with the higher pblboot-header priority, so an
// OTA image must never be staged here until it is executable + signed.
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

void flash_region_erase_optimal_range(uint32_t min_start, uint32_t max_start,
                                      uint32_t min_end, uint32_t max_end);
void flash_region_erase_optimal_range_no_watchdog(uint32_t min_start,
                                                  uint32_t max_start,
                                                  uint32_t min_end,
                                                  uint32_t max_end);
