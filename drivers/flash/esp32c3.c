/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/flash/esp32c3.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/section.h>
#include <pbl/util/math.h>

#include <devicetree/partitions.h>

// The legacy SPI1 flash routines of the ROM (esp32c3.rom.ld), which take word-aligned buffers in
// internal RAM. They return 0 on success.
typedef int (*rom_fn_t)(uint32_t a0, uint32_t a1, uint32_t a2);
#define ROM_SPIFLASH_ERASE_SECTOR ((rom_fn_t)0x40000128U)
#define ROM_SPIFLASH_WRITE        ((rom_fn_t)0x4000012cU)
#define ROM_SPIFLASH_READ         ((rom_fn_t)0x40000130U)
#define ROM_SPIFLASH_UNLOCK       ((rom_fn_t)0x40000140U)

typedef int (*rom_config_param_t)(uint32_t device_id, uint32_t chip_size, uint32_t block_size,
                                  uint32_t sector_size, uint32_t page_size, uint32_t status_mask);
#define ROM_SPIFLASH_CONFIG_PARAM ((rom_config_param_t)0x40000134U)

#define ROM_CACHE_INVALIDATE_ADDR ((int (*)(uint32_t addr, uint32_t size))0x400004d4U)
#define ROM_CACHE_SUSPEND_ICACHE  ((uint32_t (*)(void))0x40000524U)
#define ROM_CACHE_RESUME_ICACHE   ((void (*)(uint32_t autoload))0x40000528U)

// The chip the ROM routines drive, the first member of the data rom_spiflash_legacy_data points to
struct rom_spiflash_chip {
  uint32_t device_id;
  uint32_t chip_size;
  uint32_t block_size;
  uint32_t sector_size;
  uint32_t page_size;
  uint32_t status_mask;
};
#define ROM_SPIFLASH_LEGACY_DATA (*(struct rom_spiflash_chip *const *)0x3fcdfff0U)

// The image the ROM boots, which nothing updates in place: writes and erases there are refused
#ifdef PART_CODE_OFFSET
#define CODE_END (PART_CODE_OFFSET + PART_CODE_SIZE)
#else
#define CODE_END 0U
#endif

// Bytes moved per critical section
#define CHUNK_SIZE 256U

// The MMU maps the flash in 64 KiB pages, the same entry for both bus windows
#define MMU_TABLE   0x600c5000U
#define MMU_INVALID (1U << 8)
#define MMU_PAGE    0x10000U

// The cache fetches from the flash SPI1 drives, so nothing may go through it meanwhile: this
// runs from RAM, with interrupts masked by the caller, and calls only the ROM. The pages of
// [addr, addr + len) the CPU maps are then mapped again, which drops what the emulator copied of
// them when they were mapped; silicon needs the invalidation that follows.
PBL_SECTION_RAM static int prv_rom_call(rom_fn_t fn, uint32_t a0, uint32_t a1, uint32_t a2,
                                        uint32_t addr, uint32_t len) {
  uint32_t autoload = ROM_CACHE_SUSPEND_ICACHE();
  int ret = fn(a0, a1, a2);

  for (uint32_t page = addr / MMU_PAGE;
       len != 0U && page <= (addr + len - 1U) / MMU_PAGE && page < CONFIG_FLASH_SIZE / MMU_PAGE;
       page++) {
    volatile uint32_t *entry = (volatile uint32_t *)MMU_TABLE + page;
    uint32_t value = *entry;

    *entry = MMU_INVALID;
    *entry = value;
  }

  ROM_CACHE_RESUME_ICACHE(autoload);

  return ret;
}

// Runs fn on the flash, then drops what the cache holds of [addr, addr + len) if the CPU maps it
static int prv_op(rom_fn_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t addr, uint32_t len) {
  int ret;

  pbl_irq_lock();
  ret = prv_rom_call(fn, a0, a1, a2, addr, len);
  if (len != 0U && addr < CONFIG_FLASH_SIZE) {
    ROM_CACHE_INVALIDATE_ADDR(CONFIG_FLASH_BASE + addr, PBL_MIN(len, CONFIG_FLASH_SIZE - addr));
  }
  pbl_irq_unlock();

  return (ret == 0) ? 0 : -EIO;
}

static int prv_init(const struct pbl_flash *flash) {
  const struct rom_spiflash_chip *chip = ROM_SPIFLASH_LEGACY_DATA;
  int ret;

  // The ROM keeps the size the boot header gave, which a direct boot has none of
  ret = ROM_SPIFLASH_CONFIG_PARAM(chip->device_id, flash->geometry->size,
                                  flash->geometry->sector_size, flash->geometry->subsector_size,
                                  flash->geometry->page_size, chip->status_mask);
  if (ret != 0) {
    return -EIO;
  }

  // A direct boot leaves the status register's block protection as the flash powered up
  ret = prv_op(ROM_SPIFLASH_UNLOCK, 0U, 0U, 0U, 0U, 0U);
  if (ret != 0) {
    return ret;
  }

  pbl_flash_set_map(flash, (const void *)CONFIG_FLASH_BASE, CONFIG_FLASH_SIZE);

  return 0;
}

static int prv_read(const struct pbl_flash *flash, uint32_t addr, void *buf, size_t len) {
  static uint32_t s_buf[CHUNK_SIZE / 4U];
  uint8_t *dst = buf;

  while (len > 0U) {
    uint32_t start = addr & ~3U;
    uint32_t skip = addr - start;
    size_t chunk = PBL_MIN(len, CHUNK_SIZE - skip);
    uint32_t span = (skip + chunk + 3U) & ~3U;
    int ret = prv_op(ROM_SPIFLASH_READ, start, (uint32_t)(uintptr_t)s_buf, span, 0U, 0U);

    if (ret != 0) {
      return ret;
    }

    memcpy(dst, (const uint8_t *)s_buf + skip, chunk);
    addr += chunk;
    dst += chunk;
    len -= chunk;
  }

  return 0;
}

static int prv_write(const struct pbl_flash *flash, uint32_t addr, const void *buf, size_t len) {
  static uint32_t s_buf[CHUNK_SIZE / 4U];
  const uint8_t *src = buf;

  if (addr < CODE_END) {
    return -EACCES;
  }

  // Whole words, padded with erased bytes, which programming leaves as they are; copied to RAM
  // as the source may be in the flash
  while (len > 0U) {
    uint32_t start = addr & ~3U;
    uint32_t skip = addr - start;
    size_t chunk = PBL_MIN(len, CHUNK_SIZE - skip);
    uint32_t span = (skip + chunk + 3U) & ~3U;
    int ret;

    memset(s_buf, 0xff, span);
    memcpy((uint8_t *)s_buf + skip, src, chunk);
    ret = prv_op(ROM_SPIFLASH_WRITE, start, (uint32_t)(uintptr_t)s_buf, span, start, span);
    if (ret != 0) {
      return ret;
    }

    addr += chunk;
    src += chunk;
    len -= chunk;
  }

  return 0;
}

static int prv_erase(const struct pbl_flash *flash, uint32_t addr, uint32_t size) {
  const uint32_t subsector = flash->geometry->subsector_size;

  if (size != flash->geometry->sector_size && size != subsector) {
    return -EINVAL;
  }

  if (addr < CODE_END) {
    return -EACCES;
  }

  // Sectors as their subsectors, to let interrupts in between
  for (uint32_t offset = 0U; offset < size; offset += subsector) {
    int ret = prv_op(ROM_SPIFLASH_ERASE_SECTOR, (addr + offset) / subsector, 0U, 0U, addr + offset,
                     subsector);

    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

const struct pbl_flash_ops pbl_flash_esp32c3_ops = {
  .init = prv_init,
  .read = prv_read,
  .write = prv_write,
  .erase = prv_erase,
};

#include PBL_DT_INSTANCES(esp_esp32c3_flash)
