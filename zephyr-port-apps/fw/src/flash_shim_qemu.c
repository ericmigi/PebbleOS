/* SPDX-License-Identifier: Apache-2.0 */

// PFS flash seam for qemu_emery. PebbleOS flash addresses are XIP-window
// absolute (0x10000000-based, mirroring src/fw/flash_region/flash_region_qemu.h);
// the Zephyr pebble,extflash driver takes chip-relative offsets, so every call
// subtracts FLASH_REGION_BASE_ADDRESS. Persistence is the qemu -drive mtd file.

#include "pfs_flash_shim.h"

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#include "flash_region/flash_region.h"
#include "system/passert.h"

void pfs_port_panic(const char *file, int line, const char *format, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

static const struct device *const s_flash = DEVICE_DT_GET(DT_NODELABEL(extflash0));

#define QEMU_FLASH_SIZE 0x2000000u

static uint32_t prv_offset(uint32_t address, size_t size, const char *operation) {
  if ((address < FLASH_REGION_BASE_ADDRESS) ||
      ((address - FLASH_REGION_BASE_ADDRESS) > QEMU_FLASH_SIZE) ||
      (size > QEMU_FLASH_SIZE - (address - FLASH_REGION_BASE_ADDRESS))) {
    pfs_port_panic(__FILE__, __LINE__, "%s out of range: %#x + %u", operation,
                   address, (unsigned int)size);
  }
  return address - FLASH_REGION_BASE_ADDRESS;
}

int pfs_flash_shim_init(void) {
  return device_is_ready(s_flash) ? 0 : -1;
}

void flash_read_bytes(uint8_t *buffer, uint32_t start_addr, uint32_t buffer_size) {
  const int rc = flash_read(s_flash, prv_offset(start_addr, buffer_size, "flash_read"),
                            buffer, buffer_size);
  if (rc != 0) {
    pfs_port_panic(__FILE__, __LINE__, "flash_read failed: %d", rc);
  }
}

void flash_write_bytes(const uint8_t *buffer, uint32_t start_addr, uint32_t buffer_size) {
  const int rc = flash_write(s_flash, prv_offset(start_addr, buffer_size, "flash_write"),
                             buffer, buffer_size);
  if (rc != 0) {
    pfs_port_panic(__FILE__, __LINE__, "flash_write failed: %d", rc);
  }
}

static void prv_erase(uint32_t address, uint32_t size, const char *operation) {
  const int rc = flash_erase(s_flash, prv_offset(address, size, operation), size);
  if (rc != 0) {
    pfs_port_panic(__FILE__, __LINE__, "%s failed: %d", operation, rc);
  }
}

void flash_erase_subsector_blocking(uint32_t subsector_addr) {
  PBL_ASSERTN((subsector_addr & (SUBSECTOR_SIZE_BYTES - 1u)) == 0);
  prv_erase(subsector_addr, SUBSECTOR_SIZE_BYTES, "flash_erase_4k");
}

void flash_erase_sector_blocking(uint32_t sector_addr) {
  PBL_ASSERTN((sector_addr & (SECTOR_SIZE_BYTES - 1u)) == 0);
  prv_erase(sector_addr, SECTOR_SIZE_BYTES, "flash_erase_64k");
}
