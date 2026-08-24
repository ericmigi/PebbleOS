/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "pbl/os/assert.h"
#include "pbl/os/malloc.h"

void os_log(const char *filename, int line, const char *string) {
  printk("%s:%d %s\n", filename, line, string);
}

NORETURN os_assertion_failed(const char *filename, int line) {
  os_log(filename, line, "*** OS ASSERT FAILED");
  k_panic();
  CODE_UNREACHABLE;
}

NORETURN os_assertion_failed_lr(const char *filename, int line, uint32_t lr) {
  printk("%s:%d *** OS ASSERT FAILED (lr=%#x)\n", filename, line, lr);
  k_panic();
  CODE_UNREACHABLE;
}

void *os_malloc(size_t size) {
  return k_malloc(size);
}

void *os_malloc_check(size_t size) {
  void *ptr = k_malloc(size);
  OS_ASSERT(ptr);
  return ptr;
}

void os_free(void *ptr) {
  k_free(ptr);
}
