/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#define PAGE_SIZE_BYTES 0x100u

#define SECTOR_SIZE_BYTES 0x10000u
#define SECTOR_ADDR_MASK (~(SECTOR_SIZE_BYTES - 1u))

#define SUBSECTOR_SIZE_BYTES 0x1000u
#define SUBSECTOR_ADDR_MASK (~(SUBSECTOR_SIZE_BYTES - 1u))

#define FLASH_REGION_BASE_ADDRESS 0x12000000u
#define FLASH_REGION_FILESYSTEM_BEGIN 0x13e00000u
#define FLASH_REGION_FILESYSTEM_END 0x13e40000u
#define FLASH_FILESYSTEM_BLOCK_SIZE SUBSECTOR_SIZE_BYTES

// Match the production GD25Q256E layout. This 512 KiB region is disjoint from
// the temporary four-sector PFS scratch area above.
#define FLASH_REGION_CD_BEGIN 0x13f40000u
#define FLASH_REGION_CD_END 0x13fc0000u

void flash_region_erase_optimal_range(uint32_t min_start, uint32_t max_start,
                                      uint32_t min_end, uint32_t max_end);
void flash_region_erase_optimal_range_no_watchdog(uint32_t min_start,
                                                  uint32_t max_start,
                                                  uint32_t min_end,
                                                  uint32_t max_end);
