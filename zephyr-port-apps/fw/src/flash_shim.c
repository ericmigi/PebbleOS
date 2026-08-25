/* SPDX-License-Identifier: Apache-2.0 */

// PFS flash seam for the Zephyr fw app. Routes the filesystem's
// flash_read_bytes / flash_write_bytes / flash_erase_* calls through the
// shipping PebbleOS flash stack (flash_impl_* -> src/fw/drivers/flash/gd25q256e.c
// -> src/fw/drivers/sf32lb52/qspi.c -> SiFli QSPI HAL), which is the driver that
// provably erases/writes/reads the filesystem region on obelix. The previous
// implementation went through the Zephyr flash driver, which silently failed to
// persist writes/erases at the filesystem region device offset (~30MB), so
// pfs_format() never produced a valid filesystem.

#include "pfs_flash_shim.h"

#include <stddef.h>
#include <stdint.h>

#include <pbl/drivers/flash/flash_impl.h>
#include "flash_region/flash_region.h"
#include "system/passert.h"
#include "system/status_codes.h"

void pfs_port_panic(const char *file, int line, const char *format, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

static void prv_check_range(uint32_t address, size_t size, const char *operation) {
  if ((address < FLASH_REGION_FILESYSTEM_BEGIN) ||
      (address > FLASH_REGION_FILESYSTEM_END) ||
      (size > (FLASH_REGION_FILESYSTEM_END - address))) {
    pfs_port_panic(__FILE__, __LINE__, "%s out of range: %#x + %u",
                   operation, address, (unsigned int)size);
  }
}

int pfs_flash_shim_init(void) {
  // Populate the flash handle from the running controller. Do NOT call
  // flash_impl_init() -> HAL_FLASH_Init(): re-initializing the live XIP
  // controller corrupts fetch from this flash. See qspi_board_flash_init().
  qspi_board_flash_init();
  return 0;
}

void flash_read_bytes(uint8_t *buffer, uint32_t start_addr, uint32_t buffer_size) {
  prv_check_range(start_addr, buffer_size, "flash_read");
  status_t status = flash_impl_read_sync(buffer, start_addr, buffer_size);
  if (FAILED(status)) {
    pfs_port_panic(__FILE__, __LINE__, "flash_read failed: %d", (int)status);
  }
}

void flash_write_bytes(const uint8_t *buffer, uint32_t start_addr, uint32_t buffer_size) {
  prv_check_range(start_addr, buffer_size, "flash_write");
  while (buffer_size > 0) {
    int written = flash_impl_write_page_begin(buffer, start_addr, buffer_size);
    if (written < 0) {
      pfs_port_panic(__FILE__, __LINE__, "flash_write begin failed: %d", written);
    }
    status_t status;
    while ((status = flash_impl_get_write_status()) == E_BUSY) {
    }
    if (FAILED(status)) {
      pfs_port_panic(__FILE__, __LINE__, "flash_write status: %d", (int)status);
    }
    buffer += written;
    start_addr += written;
    buffer_size -= written;
  }
}

static void prv_erase_blocking(uint32_t addr, bool is_subsector, const char *operation) {
  status_t status = is_subsector ? flash_impl_erase_subsector_begin(addr)
                                 : flash_impl_erase_sector_begin(addr);
  if (FAILED(status)) {
    pfs_port_panic(__FILE__, __LINE__, "%s begin failed: %d", operation, (int)status);
  }
  while ((status = flash_impl_get_erase_status()) == E_BUSY) {
  }
  if (FAILED(status)) {
    pfs_port_panic(__FILE__, __LINE__, "%s status: %d", operation, (int)status);
  }
}

void flash_erase_subsector_blocking(uint32_t subsector_addr) {
  PBL_ASSERTN((subsector_addr & (SUBSECTOR_SIZE_BYTES - 1u)) == 0);
  prv_check_range(subsector_addr, SUBSECTOR_SIZE_BYTES, "flash_erase_4k");
  prv_erase_blocking(subsector_addr, true /* is_subsector */, "flash_erase_4k");
}

void flash_erase_sector_blocking(uint32_t sector_addr) {
  PBL_ASSERTN((sector_addr & (SECTOR_SIZE_BYTES - 1u)) == 0);
  prv_check_range(sector_addr, SECTOR_SIZE_BYTES, "flash_erase_64k");
  prv_erase_blocking(sector_addr, false /* !is_subsector */, "flash_erase_64k");
}
