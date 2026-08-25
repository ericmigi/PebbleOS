/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#define PAGE_SIZE_BYTES 0x100u

#define SECTOR_SIZE_BYTES 0x10000u
#define SECTOR_ADDR_MASK (~(SECTOR_SIZE_BYTES - 1u))

#define SUBSECTOR_SIZE_BYTES 0x1000u
#define SUBSECTOR_ADDR_MASK (~(SUBSECTOR_SIZE_BYTES - 1u))

#define FLASH_REGION_BASE_ADDRESS 0x12000000u

// Obelix GD25Q256E layout. Keep these in sync with
// src/fw/flash_region/flash_region_gd25q256e.h and pblboot's pt2 overlay.
#define FLASH_REGION_FIRMWARE_SLOT_0_BEGIN 0x12020000u
#define FLASH_REGION_FIRMWARE_SLOT_0_END 0x12320000u
#define FLASH_REGION_FIRMWARE_SLOT_1_BEGIN 0x12320000u
#define FLASH_REGION_FIRMWARE_SLOT_1_END 0x12620000u
#define FLASH_REGION_SAFE_FIRMWARE_BEGIN 0x12a20000u
#define FLASH_REGION_SAFE_FIRMWARE_END 0x12ab0000u

// The scaffold reserves a small PFS test window inside the production
// filesystem region. OTA slot access must not widen this PFS window.
#define FLASH_REGION_FILESYSTEM_BEGIN 0x13e00000u
#define FLASH_REGION_FILESYSTEM_END 0x13e40000u
#define FLASH_FILESYSTEM_BLOCK_SIZE SUBSECTOR_SIZE_BYTES

void flash_region_erase_optimal_range(uint32_t min_start, uint32_t max_start,
                                      uint32_t min_end, uint32_t max_end);
void flash_region_erase_optimal_range_no_watchdog(uint32_t min_start,
                                                  uint32_t max_start,
                                                  uint32_t min_end,
                                                  uint32_t max_end);
