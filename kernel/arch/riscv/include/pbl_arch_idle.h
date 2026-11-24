/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/kernel/compiler.h>

// wfi wakes up on a pending interrupt even with mstatus.MIE clear.
static PBL_ALWAYS_INLINE void arch_cpu_idle(void) {
  __asm volatile("wfi" ::: "memory");
}

// The SoC's own sleep modes are not wired up yet.
static PBL_ALWAYS_INLINE void arch_cpu_idle_deep(void) {
  arch_cpu_idle();
}
