/* SPDX-License-Identifier: Apache-2.0 */

#include "sandbox_launcher.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include "kernel/pebble_tasks.h"
#include "process_management/pebble_process_info.h"
#include "sandbox.h"
#include "sandbox_bridge.h"
#include "sliding_text_emery_bin.h"
#include "util/legacy_checksum.h"
#include "watchface_port.h"

#define APP_STACK_SIZE 8192U
#define APP_PRIORITY 6

_Static_assert(sizeof(PebbleProcessInfo) == 130U,
               "PebbleProcessInfo v16 ABI changed");

#define s_app_segment g_sandbox_app_arena.app_segment

static struct k_thread s_app_thread;
static struct z_thread_stack_element __kstackmem __aligned(32)
    s_app_stack[K_KERNEL_STACK_LEN(APP_STACK_SIZE)];
static const struct device *const s_display =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const uint8_t *s_framebuffer;
static struct display_buffer_descriptor s_display_desc;
static bool s_display_ready;
static bool s_app_active;

static bool prv_range_fits(size_t offset, size_t size, size_t limit) {
  return offset <= limit && size <= limit - offset;
}

static bool prv_validate_header(const PebbleProcessInfo *info, size_t blob_size,
                                size_t *stored_size_out) {
  const size_t header_size = sizeof(*info);
  const size_t reloc_size =
      (size_t)info->num_reloc_entries * sizeof(uint32_t);

  if (memcmp(info->header, "PBLAPP\0\0", sizeof(info->header)) != 0 ||
      info->struct_version.major != PROCESS_INFO_CURRENT_STRUCT_VERSION_MAJOR ||
      (info->flags & PROCESS_INFO_PLATFORM_MASK) != PROCESS_INFO_PLATFORM_EMERY) {
    printk("FW_LOAD_FAIL header\n");
    return false;
  }
  if (info->load_size < header_size || info->load_size > info->virtual_size ||
      info->virtual_size > SANDBOX_APP_SEGMENT_CAPACITY ||
      !prv_range_fits(info->load_size, reloc_size, blob_size) ||
      !prv_range_fits(info->load_size, reloc_size, sizeof(s_app_segment))) {
    printk("FW_LOAD_FAIL size load=%" PRIu32 " virtual=%" PRIu32 "\n",
           info->load_size, info->virtual_size);
    return false;
  }
  if ((info->offset & 1U) != 0U || info->offset < header_size ||
      !prv_range_fits(info->offset, sizeof(uint16_t), info->load_size) ||
      (info->sym_table_addr & 3U) != 0U ||
      info->sym_table_addr < header_size ||
      !prv_range_fits(info->sym_table_addr, sizeof(uint32_t),
                      info->virtual_size)) {
    printk("FW_LOAD_FAIL entry\n");
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
    memcpy(&reloc_offset, reloc_table + i * sizeof(reloc_offset),
           sizeof(reloc_offset));
    if (reloc_offset < sizeof(*info) ||
        !prv_range_fits(reloc_offset, sizeof(relative_value),
                        info->load_size)) {
      printk("FW_LOAD_FAIL relocation[%" PRIu32 "] target\n", i);
      return false;
    }
    memcpy(&relative_value, s_app_segment + reloc_offset,
           sizeof(relative_value));
    if (relative_value > info->virtual_size) {
      printk("FW_LOAD_FAIL relocation[%" PRIu32 "] value\n", i);
      return false;
    }
    const uint32_t relocated =
        (uint32_t)(uintptr_t)(s_app_segment + relative_value);
    memcpy(s_app_segment + reloc_offset, &relocated, sizeof(relocated));
  }

  memset(reloc_table, 0,
         (size_t)info->num_reloc_entries * sizeof(uint32_t));
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
    printk("FW_LOAD_FAIL crc=0x%08" PRIx32 " expected=0x%08" PRIx32 "\n",
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
  printk("FW_LOADED entry=0x%08" PRIx32 " reloc=%" PRIu32 "\n",
         info.offset, info.num_reloc_entries);
  return true;
}

static void prv_app_main(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  int (*entry)(void) = arg1;
  sandbox_app_runtime_init();
  sandbox_mpu_readback();
  const int probe_result = sandbox_syscall_probe(21);
  (void)sandbox_syscall_probe(probe_result);
  sandbox_app_step(SandboxStepAppEntry, (uintptr_t)entry);
  (void)entry();
  for (;;) {
    __WFE();
  }
}

// Dump the raw 8bpp Pebble framebuffer over the console UART, framed so the
// host reconstructor can pick it out of interleaved logs and verify integrity.
// Each screen render calls this; the host keeps the last complete block.
//   FB_BEGIN seq=<n> w=.. h=.. bpp=8 stride=.. size=..
//   FB <up to 64 bytes as hex>   (repeated)
//   FB_END crc=0x........
// crc is the standard CRC-32 (zlib) over the raw framebuffer bytes.
void fw_fb_dump_uart(void) {
#ifndef FW_FB_UART_DUMP
  // Screens are captured via the QEMU monitor screendump now; a full hex dump
  // blocks KernelMain ~0.7s per changed frame, so it's opt-in.
  return;
#endif
  static uint32_t s_seq;
  static uint32_t s_last_crc;
  static bool s_have_last;
  if (!s_display_ready || s_framebuffer == NULL) {
    return;
  }
  const size_t size = s_display_desc.buf_size;
  const uint32_t crc = crc32_ieee(s_framebuffer, size);
  // prv_render_top() runs on every UI pump (each button + the per-second tick),
  // but the screen only changes on navigation. Emit a frame only when the
  // content actually changed so idle ticks don't spam the UART or stall the
  // event loop (a full dump blocks KernelMain ~0.7s at 1Mbaud).
  if (s_have_last && crc == s_last_crc) {
    return;
  }
  s_last_crc = crc;
  s_have_last = true;
  printk("FB_BEGIN seq=%u w=%u h=%u bpp=8 stride=%u size=%zu\n", s_seq++,
         (unsigned)s_display_desc.width, (unsigned)s_display_desc.height,
         (unsigned)s_display_desc.pitch, size);
  static const char hexdigits[] = "0123456789abcdef";
  char line[3 + 64 * 2 + 1];
  size_t off = 0;
  while (off < size) {
    size_t n = size - off;
    if (n > 64) {
      n = 64;
    }
    char *p = line;
    *p++ = 'F';
    *p++ = 'B';
    *p++ = ' ';
    for (size_t i = 0; i < n; ++i) {
      const uint8_t byte = s_framebuffer[off + i];
      *p++ = hexdigits[byte >> 4];
      *p++ = hexdigits[byte & 0xf];
    }
    *p = '\0';
    printk("%s\n", line);
    off += n;
  }
  printk("FB_END crc=0x%08x\n", crc);
}

void fw_display_push_buffer(const uint8_t *buffer) {
  if (!s_display_ready) {
    return;
  }
  const int ret = display_write(s_display, 0U, 0U, &s_display_desc, buffer);
  if (ret != 0) {
    printk("DISPLAY_PUSH_FAIL %d\n", ret);
  }
}

void watchface_port_push_frame(void) {
  fw_display_push_buffer(s_framebuffer);
}

void fw_sandbox_display_init(void) {
  size_t framebuffer_size;
  uint16_t framebuffer_stride;

  watchface_port_graphics_init();
  s_framebuffer =
      watchface_framebuffer_bytes(&framebuffer_size, &framebuffer_stride);
  s_display_desc = (struct display_buffer_descriptor) {
    .buf_size = framebuffer_size,
    .width = framebuffer_stride,
    .height = framebuffer_size / framebuffer_stride,
    .pitch = framebuffer_stride,
  };
  if (!device_is_ready(s_display)) {
    printk("DISPLAY_PUSH_FAIL %d\n", -ENODEV);
    return;
  }
  const int ret = display_blanking_off(s_display);
  if (ret != 0) {
    printk("DISPLAY_PUSH_FAIL %d\n", ret);
    return;
  }
  s_display_ready = true;
}

bool fw_sandbox_launch(void) {
  PebbleProcessInfo info;

  memset(&g_sandbox_app_arena, 0, sizeof(g_sandbox_app_arena));
  fw_sandbox_display_init();
  if (!prv_load_pbw(&info)) {
    printk("SANDBOX_LOAD_FAIL\n");
    return false;
  }
  int (*entry)(void) =
      (int (*)(void))((uintptr_t)(s_app_segment + info.offset) | 1U);

  watchface_port_set_threads(
      pebble_task_get_handle_for_task(PebbleTask_KernelMain), &s_app_thread);
  const size_t app_stack_size = K_KERNEL_STACK_SIZEOF(s_app_stack);
  k_thread_create(&s_app_thread, s_app_stack, app_stack_size, prv_app_main,
                  entry, NULL, NULL, APP_PRIORITY, 0, K_FOREVER);
  k_thread_name_set(&s_app_thread, "PebbleAppSandbox");
  if (!sandbox_prepare(&s_app_thread,
                       (uintptr_t)K_KERNEL_STACK_BUFFER(s_app_stack),
                       app_stack_size)) {
    return false;
  }

  pebble_task_register(PebbleTask_App, &s_app_thread);
  sandbox_arm();
  k_thread_start(&s_app_thread);
  s_app_active = true;
  return true;
}

void fw_sandbox_exit(void) {
  if (!s_app_active) {
    return;
  }

  k_thread_abort(&s_app_thread);
  sandbox_disarm();
  pebble_task_unregister(PebbleTask_App);
  s_app_active = false;
  printk("SANDBOX_EXIT\n");
}
