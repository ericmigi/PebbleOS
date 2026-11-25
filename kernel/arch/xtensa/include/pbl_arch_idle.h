/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/kernel/compiler.h>

#include <pbl_arch_xtensa.h>

// waiti 0 lowers PS.INTLEVEL to 0 and halts until a level-1 interrupt arrives. Deferring context
// switches across the wait lets idle_enter() finish timekeeping_idle_end() before switching.
static PBL_ALWAYS_INLINE void arch_cpu_idle(void) {
  unsigned int ps;

  xtensa_idle_defer_switch = true;
  __asm volatile(
      "rsr.ps %0\n\t"
      "waiti 0\n\t"
      "wsr.ps %0\n\t"
      "rsync"
      : "=&a"(ps)::"memory");
  xtensa_idle_defer_switch = false;
}

static PBL_ALWAYS_INLINE void arch_cpu_idle_deep(void) {
  arch_cpu_idle();
}
