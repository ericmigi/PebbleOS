/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "applib/tick_timer_service_private.h"
#include "pbl/services/new_timer/new_timer_service.h"
#include "pbl/services/regular_timer.h"
#include "process_management/pebble_process_info.h"
#include "sliding_text_emery_bin.h"
#include "util/legacy_checksum.h"
#include "watchface_port.h"

#define APP_SEGMENT_CAPACITY 4096U
#define APP_STACK_SIZE 8192
#define APP_PRIORITY 6
#define KERNEL_STACK_SIZE 3072
#define KERNEL_PRIORITY 5

_Static_assert(sizeof(PebbleProcessInfo) == 130U, "PebbleProcessInfo v16 ABI changed");

static uint8_t s_app_segment[APP_SEGMENT_CAPACITY] __aligned(32);
static struct k_thread s_kernel_thread;
static struct k_thread s_app_thread;
static const struct device *const s_display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint8_t *s_framebuffer;
static struct display_buffer_descriptor s_display_desc;
static bool s_display_ready;
static K_THREAD_STACK_DEFINE(s_kernel_stack, KERNEL_STACK_SIZE);
static K_THREAD_STACK_DEFINE(s_app_stack, APP_STACK_SIZE);

static bool prv_range_fits(size_t offset, size_t size, size_t limit) {
  return offset <= limit && size <= limit - offset;
}

static bool prv_validate_header(const PebbleProcessInfo *info, size_t blob_size,
                                size_t *stored_size_out) {
  const size_t header_size = sizeof(*info);
  const size_t reloc_size = (size_t)info->num_reloc_entries * sizeof(uint32_t);

  if (memcmp(info->header, "PBLAPP\0\0", sizeof(info->header)) != 0 ||
      info->struct_version.major != PROCESS_INFO_CURRENT_STRUCT_VERSION_MAJOR ||
      (info->flags & PROCESS_INFO_PLATFORM_MASK) != PROCESS_INFO_PLATFORM_EMERY) {
    printk("WATCHFACE_LOAD_FAIL header\n");
    return false;
  }
  if (info->load_size < header_size || info->load_size > info->virtual_size ||
      info->virtual_size > sizeof(s_app_segment) ||
      !prv_range_fits(info->load_size, reloc_size, blob_size) ||
      !prv_range_fits(info->load_size, reloc_size, sizeof(s_app_segment))) {
    printk("WATCHFACE_LOAD_FAIL size load=%" PRIu32 " virtual=%" PRIu32 "\n",
           info->load_size, info->virtual_size);
    return false;
  }
  if ((info->offset & 1U) != 0U || info->offset < header_size ||
      !prv_range_fits(info->offset, sizeof(uint16_t), info->load_size) ||
      (info->sym_table_addr & 3U) != 0U || info->sym_table_addr < header_size ||
      !prv_range_fits(info->sym_table_addr, sizeof(uint32_t), info->virtual_size)) {
    printk("WATCHFACE_LOAD_FAIL entry\n");
    return false;
  }

  *stored_size_out = info->load_size + reloc_size;
  return true;
}

static bool prv_apply_relocations(const PebbleProcessInfo *info) {
  uint8_t *reloc_table = s_app_segment + info->load_size;

  for (uint32_t i = 0; i < info->num_reloc_entries; ++i) {
    uint32_t reloc_offset;
    uint32_t relative_value;
    memcpy(&reloc_offset, reloc_table + i * sizeof(reloc_offset), sizeof(reloc_offset));
    if (reloc_offset < sizeof(*info) ||
        !prv_range_fits(reloc_offset, sizeof(relative_value), info->load_size)) {
      printk("WATCHFACE_LOAD_FAIL relocation[%" PRIu32 "] target\n", i);
      return false;
    }
    memcpy(&relative_value, s_app_segment + reloc_offset, sizeof(relative_value));
    if (relative_value > info->virtual_size) {
      printk("WATCHFACE_LOAD_FAIL relocation[%" PRIu32 "] value\n", i);
      return false;
    }
    const uint32_t relocated = (uint32_t)(uintptr_t)(s_app_segment + relative_value);
    memcpy(s_app_segment + reloc_offset, &relocated, sizeof(relocated));
  }

  memset(reloc_table, 0, (size_t)info->num_reloc_entries * sizeof(uint32_t));
  return true;
}

static bool prv_load_pbw(PebbleProcessInfo *info_out) {
  PebbleProcessInfo info;
  size_t stored_size;

  if (sliding_text_emery_pbw_len < sizeof(info)) {
    return false;
  }
  memcpy(&info, sliding_text_emery_pbw, sizeof(info));
  if (!prv_validate_header(&info, sliding_text_emery_pbw_len, &stored_size)) {
    return false;
  }

  const uint32_t crc = legacy_defective_checksum_memory(
      sliding_text_emery_pbw + sizeof(info), info.load_size - sizeof(info));
  if (crc != info.crc) {
    printk("WATCHFACE_LOAD_FAIL crc=0x%08" PRIx32 " expected=0x%08" PRIx32 "\n",
           crc, info.crc);
    return false;
  }

  memset(s_app_segment, 0, sizeof(s_app_segment));
  memcpy(s_app_segment, sliding_text_emery_pbw, stored_size);
  if (!prv_apply_relocations(&info)) {
    return false;
  }

  const uint32_t jump_table = (uint32_t)(uintptr_t)g_pbl_system_tbl;
  memcpy(s_app_segment + info.sym_table_addr, &jump_table, sizeof(jump_table));

  (void)sys_cache_data_flush_range(s_app_segment, info.virtual_size);
  (void)sys_cache_instr_invd_range(s_app_segment, info.virtual_size);

  *info_out = info;
  printk("WATCHFACE_LOADED entry=0x%08" PRIx32 " reloc=%" PRIu32 "\n",
         info.offset, info.num_reloc_entries);
  return true;
}

static void prv_kernel_main(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  printk("WATCHFACE_KERNEL_UP\n");
  while (true) {
    PebbleEvent event;
    if (watchface_port_take_kernel_event(&event)) {
      watchface_port_dispatch_kernel_event(&event);
    }
  }
}

static void prv_app_main(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  watchface_port_app_state_init();
  tick_timer_service_init();

  PebbleProcessInfo info;
  if (!prv_load_pbw(&info)) {
    printk("WATCHFACE_PATH FALLBACK\n");
    watchface_start_fallback();
    return;
  }

  printk("WATCHFACE_PATH PBW_PRIVILEGED\n");
  int (*entry)(void) = (int (*)(void))((uintptr_t)(s_app_segment + info.offset) | 1U);
  const int result = entry();
  printk("WATCHFACE_EXIT %d\n", result);
}

void watchface_port_push_frame(void) {
  if (!s_display_ready) {
    return;
  }

  // Push the Pebble framebuffer straight to the panel. Hardware verification
  // showed the software 180° rotation left the physical panel upside-down, so
  // the native scan orientation is already correct.
  const int ret = display_write(s_display, 0U, 0U, &s_display_desc, s_framebuffer);
  if (ret != 0) {
    printk("DISPLAY_PUSH_FAIL %d\n", ret);
    return;
  }
  printk("DISPLAY_PUSH ok\n");
  printk("DISPLAY_PUSH_EOF\n");
}

int main(void) {
  watchface_port_graphics_init();
  size_t framebuffer_size;
  uint16_t framebuffer_stride;
  uint8_t *framebuffer = watchface_framebuffer_bytes(&framebuffer_size, &framebuffer_stride);
  printk("WATCHFACE_FB %p size=%zu stride=%u ARGB2222\n", framebuffer,
         framebuffer_size, framebuffer_stride);
  s_framebuffer = framebuffer;
  s_display_desc = (struct display_buffer_descriptor){
      .buf_size = framebuffer_size,
      .width = framebuffer_stride,
      .height = framebuffer_size / framebuffer_stride,
      .pitch = framebuffer_stride,
  };
  if (!device_is_ready(s_display)) {
    printk("DISPLAY_PUSH_FAIL %d\n", -ENODEV);
  } else {
    const int ret = display_blanking_off(s_display);
    if (ret != 0) {
      printk("DISPLAY_PUSH_FAIL %d\n", ret);
    } else {
      s_display_ready = true;
    }
  }
  new_timer_service_init();
  regular_timer_init();

  watchface_port_set_threads(&s_kernel_thread, &s_app_thread);
  k_thread_create(&s_kernel_thread, s_kernel_stack, K_THREAD_STACK_SIZEOF(s_kernel_stack),
                  prv_kernel_main, NULL, NULL, NULL, KERNEL_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(&s_kernel_thread, "KernelMain");
  k_thread_create(&s_app_thread, s_app_stack, K_THREAD_STACK_SIZEOF(s_app_stack),
                  prv_app_main, NULL, NULL, NULL, APP_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(&s_app_thread, "PebbleApp");
  return 0;
}
