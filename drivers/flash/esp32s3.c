/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/flash/esp32s3.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/section.h>
#include <pbl/util/math.h>

#include <devicetree/partitions.h>

// Legacy SPI1 flash and cache routines of the ESP32-S3 ROM (esp32s3.rom.ld).
typedef int (*rom_fn_t)(uint32_t a0, uint32_t a1, uint32_t a2);
#define ROM_SPIFLASH_ERASE_SECTOR ((rom_fn_t)0x400009fcU)
#define ROM_SPIFLASH_WRITE        ((rom_fn_t)0x40000a14U)
#define ROM_SPIFLASH_READ         ((rom_fn_t)0x40000a20U)
#define ROM_SPIFLASH_UNLOCK       ((rom_fn_t)0x40000a2cU)

typedef int (*rom_config_param_t)(uint32_t device_id, uint32_t chip_size, uint32_t block_size,
                                  uint32_t sector_size, uint32_t page_size, uint32_t status_mask);
#define ROM_SPIFLASH_CONFIG_PARAM ((rom_config_param_t)0x40000a50U)

#define ROM_CACHE_INVALIDATE_ADDR ((int (*)(uint32_t addr, uint32_t size))0x400016b0U)
#define ROM_CACHE_SUSPEND_ICACHE  ((uint32_t (*)(void))0x4000189cU)
#define ROM_CACHE_RESUME_ICACHE   ((void (*)(uint32_t autoload))0x400018a8U)
#define ROM_CACHE_SUSPEND_DCACHE  ((uint32_t (*)(void))0x400018b4U)
#define ROM_CACHE_RESUME_DCACHE   ((void (*)(uint32_t autoload))0x400018c0U)

struct rom_spiflash_chip {
  uint32_t device_id;
  uint32_t chip_size;
  uint32_t block_size;
  uint32_t sector_size;
  uint32_t page_size;
  uint32_t status_mask;
};
#define ROM_SPIFLASH_LEGACY_DATA (*(struct rom_spiflash_chip *const *)0x3fceffe4U)

#ifdef PART_CODE_OFFSET
#define CODE_END (PART_CODE_OFFSET + PART_CODE_SIZE)
#else
#define CODE_END 0U
#endif

#define CHUNK_SIZE 256U

// Shared I/D MMU table (64 KiB pages); bit 14 marks an entry invalid on ESP32-S3.
#define MMU_TABLE   0x600c5000U
#define MMU_INVALID (1U << 14)
#define MMU_PAGE    0x10000U

PBL_SECTION_RAM static int prv_rom_call(rom_fn_t fn, uint32_t a0, uint32_t a1, uint32_t a2,
                                        uint32_t addr, uint32_t len) {
  uint32_t icache = ROM_CACHE_SUSPEND_ICACHE();
  uint32_t dcache = ROM_CACHE_SUSPEND_DCACHE();
  int ret = fn(a0, a1, a2);

  for (uint32_t page = addr / MMU_PAGE;
       len != 0U && page <= (addr + len - 1U) / MMU_PAGE && page < CONFIG_FLASH_SIZE / MMU_PAGE;
       page++) {
    volatile uint32_t *entry = (volatile uint32_t *)MMU_TABLE + page;
    uint32_t value = *entry;

    *entry = MMU_INVALID;
    *entry = value;
  }

  ROM_CACHE_RESUME_DCACHE(dcache);
  ROM_CACHE_RESUME_ICACHE(icache);

  return ret;
}

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

  ret = ROM_SPIFLASH_CONFIG_PARAM(chip->device_id, flash->geometry->size,
                                  flash->geometry->sector_size, flash->geometry->subsector_size,
                                  flash->geometry->page_size, chip->status_mask);
  if (ret != 0) {
    return -EIO;
  }

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

  for (uint32_t offset = 0U; offset < size; offset += subsector) {
    int ret = prv_op(ROM_SPIFLASH_ERASE_SECTOR, (addr + offset) / subsector, 0U, 0U, addr + offset,
                     subsector);

    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

const struct pbl_flash_ops pbl_flash_esp32s3_ops = {
  .init = prv_init,
  .read = prv_read,
  .write = prv_write,
  .erase = prv_erase,
};

#include PBL_DT_INSTANCES(esp_esp32s3_flash)
