/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "kernel/pbl_malloc.h"
#include "kernel/pebble_tasks.h"
#include "pbl/os/malloc.h"

void pfs_port_panic(const char *file, int line, const char *format, ...) {
  va_list arguments;

  printk("PFS_FAIL %s:%d ", file, line);
  va_start(arguments, format);
  vprintk(format, arguments);
  va_end(arguments);
  printk("\n");
  k_panic();
  CODE_UNREACHABLE;
}

void *kernel_malloc(size_t size) {
  return os_malloc(size);
}

void *kernel_malloc_check(size_t size) {
  return os_malloc_check(size);
}

void kernel_free(void *pointer) {
  os_free(pointer);
}

char *kernel_strdup(const char *string) {
  size_t size = strlen(string) + 1u;
  char *copy = os_malloc(size);
  if (copy != NULL) {
    memcpy(copy, string, size);
  }
  return copy;
}

char *kernel_strdup_check(const char *string) {
  char *copy = kernel_strdup(string);
  if (copy == NULL) {
    pfs_port_panic(__FILE__, __LINE__, "out of memory");
  }
  return copy;
}

void psleep(int millis) {
  if (millis == 0) {
    k_yield();
  } else {
    k_msleep(millis);
  }
}

uint64_t rtc_get_ticks(void) {
  return k_uptime_get();
}

PebbleTask pebble_task_get_current(void) {
  return PebbleTask_KernelMain;
}

void task_watchdog_bit_set(PebbleTask task) {
  (void)task;
}

void util_assertion_failed(const char *file, int line) {
  pfs_port_panic(file, line, "utility assertion");
}

void util_log(const char *file, int line, const char *string) {
  printk("%s:%d %s\n", file, line, string);
}

void prompt_send_response(const char *response) {
  printk("%s\n", response);
}

void prompt_send_response_fmt(char *buffer, size_t buffer_size,
                              const char *format, ...) {
  va_list arguments;

  va_start(arguments, format);
  vsnprintk(buffer, buffer_size, format, arguments);
  va_end(arguments);
  printk("%s\n", buffer);
}
