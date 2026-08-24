/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "pbl/util/attributes.h"

void sandbox_syscall_maybe_skip_privilege(void);

#define DEFINE_SYSCALL(ret_type, function_name, ...)                              \
  ret_type NAKED_FUNC USED SECTION(".sandbox_syscall_text." #function_name)       \
      function_name(__VA_ARGS__) {                                                \
    __asm volatile(                                                               \
        "push {lr}\n"                                                           \
        "bl sandbox_syscall_maybe_skip_privilege\n"                             \
        "svc #4\n"                                                              \
        "b __" #function_name "\n");                                           \
  }                                                                               \
  ret_type EXTERNALLY_VISIBLE USED __##function_name(__VA_ARGS__)
