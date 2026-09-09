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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pbl/drivers/flash/flash_impl.h>
#include "flash_region/flash_region.h"
#include "system/passert.h"
#include "system/status_codes.h"

void pfs_port_panic(const char *file, int line, const char *format, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

static bool prv_range_is_within(uint32_t address, size_t size, uint32_t begin,
                                uint32_t end) {
  return (address >= begin) && (address <= end) && (size <= (end - address));
}

static void prv_check_range(uint32_t address, size_t size, const char *operation) {
  // PFS scratch plus the OTA regions the fw app touches: real pblboot slots are
  // read-only here (boot-slot reporting), OTA scratch is erased/written by the
  // OTA self-test. All resolve to the same real flash driver below.
  const bool allowed =
      prv_range_is_within(address, size, FLASH_REGION_FILESYSTEM_BEGIN,
                          FLASH_REGION_FILESYSTEM_END) ||
      prv_range_is_within(address, size, FLASH_REGION_OTA_SCRATCH_BEGIN,
                          FLASH_REGION_OTA_SCRATCH_END) ||
      prv_range_is_within(address, size, FLASH_REGION_FIRMWARE_SLOT_0_BEGIN,
                          FLASH_REGION_FIRMWARE_SLOT_0_END) ||
      prv_range_is_within(address, size, FLASH_REGION_FIRMWARE_SLOT_1_BEGIN,
                          FLASH_REGION_FIRMWARE_SLOT_1_END) ||
      prv_range_is_within(address, size, FLASH_REGION_SAFE_FIRMWARE_BEGIN,
                          FLASH_REGION_SAFE_FIRMWARE_END);

  if (!allowed) {
    pfs_port_panic(__FILE__, __LINE__, "%s out of range: %#x + %u",
                   operation, address, (unsigned int)size);
  }
}

int pfs_flash_shim_init(void) {
  // Populate the flash handle from the running controller. Do NOT call
  // flash_impl_init() -> HAL_FLASH_Init(): re-initializing the live XIP
  // controller corrupts fetch from this flash. See qspi_board_flash_init().
  qspi_board_flash_init();
  // Controller is marked initialised, so this only records the flash part
  // (security-register / OTP layout) without touching the HAL.
  flash_impl_init(false);
  return 0;
}

void flash_read_bytes(uint8_t *buffer, uint32_t start_addr, uint32_t buffer_size) {
  // Read-only regions the fw app serves from flash: shared-PRF (bond), the
  // system resource pack banks, MFG info.
  const bool read_only_region =
      prv_range_is_within(start_addr, buffer_size, FLASH_REGION_SHARED_PRF_STORAGE_BEGIN,
                          FLASH_REGION_SHARED_PRF_STORAGE_END) ||
      prv_range_is_within(start_addr, buffer_size, FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN,
                          FLASH_REGION_SYSTEM_RESOURCES_BANK_1_END) ||
      prv_range_is_within(start_addr, buffer_size, FLASH_REGION_MFG_INFO_BEGIN,
                          FLASH_REGION_MFG_INFO_END);
  if (!read_only_region) {
    prv_check_range(start_addr, buffer_size, "flash_read");
  }
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

// OTP (mfg serial / HW version) lives in the flash security registers;
// otp_flash.c reaches them through these flash_api entry points. The
// alternate "cd" flash path is not present on this board.
status_t flash_read_security_register(uint32_t addr, uint8_t *val) {
  return flash_impl_read_security_register(addr, val);
}

status_t flash_security_register_is_locked(uint32_t address, bool *locked) {
  return flash_impl_security_register_is_locked(address, locked);
}

const FlashSecurityRegisters *flash_security_registers_info(void) {
  return flash_impl_security_registers_info();
}

bool cd_flash_active(void) { return false; }
status_t cd_flash_read_security_register(uint32_t addr, uint8_t *val) {
  (void)addr; (void)val;
  return E_INVALID_OPERATION;
}
status_t cd_flash_security_register_is_locked(uint32_t addr, bool *locked) {
  (void)addr; (void)locked;
  return E_INVALID_OPERATION;
}
