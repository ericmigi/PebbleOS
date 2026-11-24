/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <pbl_arch_riscv.h>
#include <soc_irqs.h>

// The kernel lock is mstatus.MIE, and handlers run with it clear: priorities are checked for
// range but every line is taken at the kernel's own level, one at a time.
#define ARCH_IRQ_PRIO_LOWEST 13U
#define ARCH_IRQ_PRIO_KERNEL 1U

#define ARCH_IRQ_HANDLER(_n)  ARCH_IRQ_HANDLER_(_n)
#define ARCH_IRQ_HANDLER_(_n) pbl_isr_##_n

#define ARCH_IRQ_DECLARE(_n, _name, _prio, _flags)                                                 \
  static_assert((_prio) >= 0 && (_prio) <= ARCH_IRQ_PRIO_LOWEST, _name ": priority out of range"); \
  static_assert(!((_flags) & PBL_IRQ_ZERO_LATENCY),                                                \
                _name ": no zero-latency interrupts on RISC-V");                                   \
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
  unsigned int key;

  __asm volatile("csrrci %0, mstatus, %1" : "=r"(key) : "i"(RISCV_MSTATUS_MIE) : "memory");

  return key & RISCV_MSTATUS_MIE;
}

// A switch requested under the lock is taken here, synchronously, before interrupts come back.
static PBL_ALWAYS_INLINE void arch_irq_unlock(unsigned int key) {
  if (key != 0U) {
    if (riscv_switch_pending) {
      __asm volatile("ecall" ::: "memory");
    }
    __asm volatile("csrsi mstatus, %0" ::"i"(RISCV_MSTATUS_MIE) : "memory");
  }
}

static PBL_ALWAYS_INLINE bool arch_irq_is_locked(void) {
  return (riscv_csr_read(mstatus) & RISCV_MSTATUS_MIE) == 0U;
}

static PBL_ALWAYS_INLINE unsigned int arch_irq_disable_all(void) {
  return arch_irq_lock();
}

static PBL_ALWAYS_INLINE void arch_irq_restore_all(unsigned int key) {
  if (key != 0U) {
    __asm volatile("csrsi mstatus, %0" ::"i"(RISCV_MSTATUS_MIE) : "memory");
  }
}

// Nothing is more urgent than the kernel's own level, so nothing ever runs in the window.
static PBL_ALWAYS_INLINE bool arch_irq_window_pending(unsigned int key) {
  (void)key;
  return false;
}

static PBL_ALWAYS_INLINE void arch_irq_window(void) {
}
