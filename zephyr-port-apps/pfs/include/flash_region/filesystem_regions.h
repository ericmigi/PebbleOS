/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include "flash_region/flash_region.h"

typedef struct FSRegion {
  uint32_t start;
  uint32_t end;
} FSRegion;

_Static_assert((FLASH_REGION_FILESYSTEM_BEGIN % SECTOR_SIZE_BYTES) == 0,
               "PFS scratch start must be sector aligned");
_Static_assert((FLASH_REGION_FILESYSTEM_END % SECTOR_SIZE_BYTES) == 0,
               "PFS scratch end must be sector aligned");
_Static_assert((FLASH_REGION_FILESYSTEM_END - FLASH_REGION_FILESYSTEM_BEGIN) ==
                   (4u * SECTOR_SIZE_BYTES),
               "PFS scratch must contain four erase sectors");

static const FSRegion s_region_list[] = {
    {.start = FLASH_REGION_FILESYSTEM_BEGIN,
     .end = FLASH_REGION_FILESYSTEM_END},
};

void filesystem_regions_erase_all(void);
