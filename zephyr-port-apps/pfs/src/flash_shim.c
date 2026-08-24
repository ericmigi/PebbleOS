/* SPDX-License-Identifier: Apache-2.0 */

#include "pfs_flash_shim.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#include "flash_region/flash_region.h"
#include "system/passert.h"

#define PFS_FLASH_NODE DT_NODELABEL(mpi2)

#define PFS_DEVICE_OFFSET(address) ((address) - FLASH_REGION_BASE_ADDRESS)

static const struct device *const s_flash = DEVICE_DT_GET(PFS_FLASH_NODE);

static void prv_check_range(uint32_t address, size_t size, const char *operation) {
  if ((address < FLASH_REGION_FILESYSTEM_BEGIN) ||
      (address > FLASH_REGION_FILESYSTEM_END) ||
      (size > (FLASH_REGION_FILESYSTEM_END - address))) {
    pfs_port_panic(__FILE__, __LINE__, "%s out of range: %#x + %u",
                   operation, address, (unsigned int)size);
  }
}

static void prv_check_result(const char *operation, int result) {
  if (result != 0) {
    pfs_port_panic(__FILE__, __LINE__, "%s failed: %d", operation, result);
  }
}

int pfs_flash_shim_init(void) {
  if (!device_is_ready(s_flash)) {
    return -ENODEV;
  }

  uint64_t flash_size;
  int result = flash_get_size(s_flash, &flash_size);
  if (result != 0) {
    return result;
  }

  if (PFS_DEVICE_OFFSET(FLASH_REGION_FILESYSTEM_END) > flash_size) {
    return -ERANGE;
  }

  return 0;
}

void flash_read_bytes(uint8_t *buffer, uint32_t start_addr,
                      uint32_t buffer_size) {
  prv_check_range(start_addr, buffer_size, "flash_read");
  prv_check_result("flash_read",
                   flash_read(s_flash, PFS_DEVICE_OFFSET(start_addr), buffer,
                              buffer_size));
}

void flash_write_bytes(const uint8_t *buffer, uint32_t start_addr,
                       uint32_t buffer_size) {
  prv_check_range(start_addr, buffer_size, "flash_write");
  prv_check_result("flash_write",
                   flash_write(s_flash, PFS_DEVICE_OFFSET(start_addr), buffer,
                               buffer_size));
}

void flash_erase_subsector_blocking(uint32_t subsector_addr) {
  PBL_ASSERTN((subsector_addr & (SUBSECTOR_SIZE_BYTES - 1u)) == 0);
  prv_check_range(subsector_addr, SUBSECTOR_SIZE_BYTES, "flash_erase_4k");
  prv_check_result("flash_erase_4k",
                   flash_erase(s_flash, PFS_DEVICE_OFFSET(subsector_addr),
                               SUBSECTOR_SIZE_BYTES));
}

void flash_erase_sector_blocking(uint32_t sector_addr) {
  PBL_ASSERTN((sector_addr & (SECTOR_SIZE_BYTES - 1u)) == 0);
  prv_check_range(sector_addr, SECTOR_SIZE_BYTES, "flash_erase_64k");
  prv_check_result("flash_erase_64k",
                   flash_erase(s_flash, PFS_DEVICE_OFFSET(sector_addr),
                               SECTOR_SIZE_BYTES));
}
