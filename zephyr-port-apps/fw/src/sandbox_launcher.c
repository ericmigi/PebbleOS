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
#include <zephyr/sys/printk.h>

#include "kernel/pebble_tasks.h"
#include "kernel/util/segment.h"
#include "process_management/app_manager.h"
#include "sandbox.h"
#include "sandbox_bridge.h"
#include "watchface_port.h"

#define APP_STACK_SIZE 8192U
#define APP_PRIORITY 6

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

void watchface_port_push_frame(void) {
  if (!s_display_ready) {
    return;
  }
  const int ret =
      display_write(s_display, 0U, 0U, &s_display_desc, s_framebuffer);
  if (ret != 0) {
    printk("DISPLAY_PUSH_FAIL %d\n", ret);
  }
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

bool fw_sandbox_launch(AppInstallId install_id) {
  memset(&g_sandbox_app_arena, 0, sizeof(g_sandbox_app_arena));
  fw_sandbox_display_init();
  MemorySegment app_segment = {
    .start = s_app_segment,
    .end = s_app_segment + sizeof(s_app_segment),
  };
  size_t loaded_size = 0;
  void *entry =
      app_manager_load_code_bank(install_id, &app_segment, &loaded_size);
  if (!entry) {
    printk("SANDBOX_LOAD_FAIL\n");
    return false;
  }
  (void)sys_cache_data_flush_range(s_app_segment, loaded_size);
  (void)sys_cache_instr_invd_range(s_app_segment, loaded_size);
  printk("FW_STORAGE_LAUNCH id=%" PRId32 "\n", install_id);

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
