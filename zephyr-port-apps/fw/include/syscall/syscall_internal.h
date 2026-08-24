/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

#define DEFINE_SYSCALL(ret_type, function, ...) ret_type function(__VA_ARGS__)
#define PRIVILEGE_WAS_ELEVATED 0

// Kernel-context port: syscalls run privileged, so userspace-buffer checks in
// the (dead) PRIVILEGE_WAS_ELEVATED branches are no-ops.
static inline void syscall_assert_userspace_buffer(const void *buf, size_t num_bytes) {
  (void)buf;
  (void)num_bytes;
}
