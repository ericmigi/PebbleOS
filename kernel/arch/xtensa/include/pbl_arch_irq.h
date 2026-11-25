/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <pbl_arch_xtensa.h>
#include <soc_irqs.h>

// The kernel lock raises PS.INTLEVEL to XTENSA_EXCM_LEVEL (3); all SoC sources are taken at
// level 1, one at a time.
#define ARCH_IRQ_PRIO_LOWEST 13U
#define ARCH_IRQ_PRIO_KERNEL 1U

#define ARCH_IRQ_HANDLER(_n)  ARCH_IRQ_HANDLER_(_n)
#define ARCH_IRQ_HANDLER_(_n) pbl_isr_##_n

#define ARCH_IRQ_DECLARE(_n, _name, _prio, _flags)                                                 \
  static_assert((_prio) >= 0 && (_prio) <= ARCH_IRQ_PRIO_LOWEST, _name ": priority out of range"); \
  static_assert(!((_flags) & PBL_IRQ_ZERO_LATENCY),                                                \
                _name ": no zero-latency interrupts on Xtensa");                                   \
  static_assert((_n) < CONFIG_NUM_IRQS, _name ": no such line");                                   \
  void ARCH_IRQ_HANDLER(_n)(void)

#define ARCH_IRQ_CONNECT(_n, _name, _prio, _isr, _arg, _flags) \
  ARCH_IRQ_DECLARE(_n, _name, _prio, _flags);                  \
  void ARCH_IRQ_HANDLER(_n)(void) {                            \
    _isr(_arg);                                                \
  }                                                            \
  static_assert(1, "")

#define ARCH_IRQ_DIRECT(_n, _name, _prio, _flags) \
  ARCH_IRQ_DECLARE(_n, _name, _prio, _flags);     \
  void ARCH_IRQ_HANDLER(_n)(void)

static PBL_ALWAYS_INLINE unsigned int arch_irq_lock(void) {
  unsigned int ps;

  __asm volatile("rsil %0, 3" : "=a"(ps)::"memory");

  return ps & XTENSA_PS_INTLEVEL_MASK;
}

// A switch requested under the lock is taken here, synchronously, before interrupts come back.
static PBL_ALWAYS_INLINE void arch_irq_unlock(unsigned int key) {
  if (key == 0U) {
    if (xtensa_switch_pending) {
      __asm volatile("syscall" ::: "memory");
    }
    unsigned int ps;
    __asm volatile("rsil %0, 0" : "=a"(ps)::"memory");
  }
}

static PBL_ALWAYS_INLINE bool arch_irq_is_locked(void) {
  return (xtensa_sr_read(ps) & (XTENSA_PS_INTLEVEL_MASK | XTENSA_PS_EXCM)) != 0U;
}

static PBL_ALWAYS_INLINE unsigned int arch_irq_disable_all(void) {
  return arch_irq_lock();
}

static PBL_ALWAYS_INLINE void arch_irq_restore_all(unsigned int key) {
  if (key == 0U) {
    unsigned int ps;
    __asm volatile("rsil %0, 0" : "=a"(ps)::"memory");
  }
}

// Nothing is more urgent than the kernel's own level, so nothing ever runs in the window.
static PBL_ALWAYS_INLINE bool arch_irq_window_pending(unsigned int key) {
  (void)key;
  return false;
}

static PBL_ALWAYS_INLINE void arch_irq_window(void) {
}
