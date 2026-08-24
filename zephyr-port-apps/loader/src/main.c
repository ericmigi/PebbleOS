/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

#include "process_management/pebble_process_info.h"
#include "sliding_text_emery_bin.h"
#include "syscall_demo.h"
#include "util/legacy_checksum.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define APP_SEGMENT_CAPACITY 4096U

_Static_assert(sizeof(PebbleProcessInfo) == 130U,
               "PebbleProcessInfo v16 ABI changed");

static uint8_t s_app_segment[APP_SEGMENT_CAPACITY] __aligned(32);

static void prv_sdk_placeholder(void) {}

/* The firmware build supplies the generated, ABI-ordered table instead. */
static const void *const g_pbl_system_tbl[] = {
  &prv_sdk_placeholder,
};

static bool prv_range_fits(size_t offset, size_t size, size_t limit) {
  return (offset <= limit) && (size <= (limit - offset));
}

static bool prv_validate_header(const PebbleProcessInfo *info, size_t blob_size,
                                size_t *stored_size_out) {
  const size_t header_size = sizeof(*info);
  const size_t load_size = info->load_size;
  const size_t virtual_size = info->virtual_size;

  if (memcmp(info->header, "PBLAPP\0\0", sizeof(info->header)) != 0) {
    printk("LOADER_FAIL: invalid magic\n");
    return false;
  }
  if (info->struct_version.major != PROCESS_INFO_CURRENT_STRUCT_VERSION_MAJOR) {
    printk("LOADER_FAIL: unsupported header version %u.%u\n",
           info->struct_version.major, info->struct_version.minor);
    return false;
  }
  if ((info->flags & PROCESS_INFO_PLATFORM_MASK) !=
      PROCESS_INFO_PLATFORM_EMERY) {
    printk("LOADER_FAIL: binary is not for emery (flags=0x%08" PRIx32 ")\n",
           info->flags);
    return false;
  }
  if (load_size < header_size || load_size > virtual_size ||
      virtual_size > sizeof(s_app_segment)) {
    printk("LOADER_FAIL: sizes header=%zu load=%zu virtual=%zu segment=%zu\n",
           header_size, load_size, virtual_size, sizeof(s_app_segment));
    return false;
  }

  const size_t reloc_size = (size_t)info->num_reloc_entries * sizeof(uint32_t);
  if (info->num_reloc_entries != reloc_size / sizeof(uint32_t) ||
      !prv_range_fits(load_size, reloc_size, blob_size) ||
      !prv_range_fits(load_size, reloc_size, sizeof(s_app_segment))) {
    printk("LOADER_FAIL: relocation table does not fit\n");
    return false;
  }

  if ((info->offset & 1U) != 0U ||
      !prv_range_fits(info->offset, sizeof(uint16_t), load_size) ||
      info->offset < header_size) {
    printk("LOADER_FAIL: invalid entry 0x%08" PRIx32 "\n", info->offset);
    return false;
  }
  if ((info->sym_table_addr & (sizeof(uint32_t) - 1U)) != 0U ||
      !prv_range_fits(info->sym_table_addr, sizeof(uint32_t), virtual_size) ||
      info->sym_table_addr < header_size) {
    printk("LOADER_FAIL: invalid jump table slot 0x%08" PRIx32 "\n",
           info->sym_table_addr);
    return false;
  }

  *stored_size_out = load_size + reloc_size;
  return true;
}

static bool prv_verify_crc(const PebbleProcessInfo *info, const uint8_t *image) {
  const size_t header_size = sizeof(*info);
  const uint32_t calculated = legacy_defective_checksum_memory(
      image + header_size, info->load_size - header_size);

  if (calculated != info->crc) {
    printk("LOADER_FAIL: crc calculated=0x%08" PRIx32
           " expected=0x%08" PRIx32 "\n", calculated, info->crc);
    return false;
  }
  return true;
}

static bool prv_apply_relocations(const PebbleProcessInfo *info,
                                  uint8_t *destination) {
  uint8_t *reloc_table = destination + info->load_size;

  for (uint32_t i = 0; i < info->num_reloc_entries; ++i) {
    uint32_t reloc_offset;
    memcpy(&reloc_offset, &reloc_table[i * sizeof(reloc_offset)],
           sizeof(reloc_offset));
    if (!prv_range_fits(reloc_offset, sizeof(uint32_t), info->load_size) ||
        reloc_offset < sizeof(*info)) {
      printk("LOADER_FAIL: invalid relocation target[%" PRIu32
             "]=0x%08" PRIx32 "\n", i, reloc_offset);
      return false;
    }

    uint32_t app_relative_value;
    memcpy(&app_relative_value, destination + reloc_offset,
           sizeof(app_relative_value));
    if (app_relative_value > info->virtual_size) {
      printk("LOADER_FAIL: invalid relocation value[%" PRIu32
             "]=0x%08" PRIx32 "\n", i, app_relative_value);
      return false;
    }

    const uint32_t relocated_value =
        (uint32_t)(uintptr_t)(destination + app_relative_value);
    memcpy(destination + reloc_offset, &relocated_value,
           sizeof(relocated_value));
  }

  if (info->num_reloc_entries != 0U) {
    memset(reloc_table, 0,
           (size_t)info->num_reloc_entries * sizeof(uint32_t));
  }
  return true;
}

static bool prv_load_sliding_text(void) {
  PebbleProcessInfo info;
  size_t stored_size;

  if (sliding_text_emery_pbw_len < sizeof(info)) {
    printk("LOADER_FAIL: image smaller than header\n");
    return false;
  }
  memcpy(&info, sliding_text_emery_pbw, sizeof(info));

  if (!prv_validate_header(&info, sliding_text_emery_pbw_len, &stored_size)) {
    return false;
  }

  memset(s_app_segment, 0, sizeof(s_app_segment));
  memcpy(s_app_segment, sliding_text_emery_pbw, stored_size);
  if (memcmp(&info, s_app_segment, sizeof(info)) != 0) {
    printk("LOADER_FAIL: header changed during copy\n");
    return false;
  }
  if (!prv_verify_crc(&info, s_app_segment)) {
    return false;
  }

  printk("LOADER: header ok (crc=0x%08" PRIx32
         " version=%u.%u entry=0x%08" PRIx32 ")\n",
         info.crc, info.struct_version.major, info.struct_version.minor,
         info.offset);
  if (!prv_apply_relocations(&info, s_app_segment)) {
    return false;
  }
  printk("LOADER: relocated %" PRIu32 " entries\n", info.num_reloc_entries);

  const uint32_t jump_table = (uint32_t)(uintptr_t)&g_pbl_system_tbl;
  memcpy(s_app_segment + info.sym_table_addr, &jump_table, sizeof(jump_table));
  uint32_t patched_value;
  memcpy(&patched_value, s_app_segment + info.sym_table_addr,
         sizeof(patched_value));
  if (patched_value != jump_table) {
    printk("LOADER_FAIL: jump table patch did not stick\n");
    return false;
  }
  printk("LOADER: jumptable patched\n");

  return true;
}

int main(void) {
  if (prv_load_sliding_text()) {
    printk("LOADER_OK\n");
    if (syscall_demo_run()) {
      printk("SYSCALL_OK\n");
    }
  }
  return 0;
}
