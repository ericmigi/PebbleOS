/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "riscv_frame.h"

#include <string.h>

#include <pbl/kernel/compiler.h>
#include <pbl/mcu/fpu.h>
#include <pbl/mcu/interrupts.h>

#include <kernel.h>
#include <pbl_arch_idle.h>

/*
 * Context switch: a thread's saved SP points at a trap frame. Every trap saves one; the switch
 * happens on the way out of the outermost trap, when one is due. A thread asks for one with
 * interrupts locked, and gets it when it unlocks them, through an ecall; an ISR gets it on its
 * return.
 */

struct pbl_thread *pbl_cur;
extern struct pbl_thread *sched_next;

volatile uint32_t riscv_isr_nesting;
volatile bool riscv_switch_pending;

static_assert(offsetof(struct pbl_thread, backend.sp) == 0, "the trap entry saves the SP first");

void arch_cur_set(struct pbl_thread *t) {
  pbl_cur = t;
}

// ---- threads ----------------------------------------------------------------

static void prv_thread_return(void) {
  pbl_thread_abort(nullptr);
}

void arch_thread_init(struct pbl_thread *t, void (*entry)(void *), void *arg) {
  uintptr_t top = ((uintptr_t)t->stack + t->stack_size) & ~15U;
  struct riscv_frame *frame = (struct riscv_frame *)(top - sizeof(struct riscv_frame));

  memset(frame, 0, sizeof(*frame));
  frame->mepc = (uint32_t)entry;
  frame->ra = (uint32_t)prv_thread_return;
  frame->a[0] = (uint32_t)arg;
  frame->mstatus = RISCV_MSTATUS_MPP_M | RISCV_MSTATUS_MPIE;
  t->backend.sp = frame;
}

void arch_thread_regions_set(struct pbl_thread *t, const struct pbl_mpu_region *const *regions) {
  (void)t;
  (void)regions;
}

void arch_thread_aborted(struct pbl_thread *t) {
  (void)t;
}

void arch_thread_exit(void) {
  // The switch requested by the abort takes over as soon as interrupts are back.
  for (;;) {
    arch_irq_unlock(RISCV_MSTATUS_MIE);
    arch_cpu_idle();
  }
}

// ---- interrupts -------------------------------------------------------------

bool arch_in_isr(void) {
  return riscv_isr_nesting != 0U;
}

bool mcu_state_are_interrupts_enabled(void) {
  return !arch_irq_is_locked();
}

void mcu_fpu_cleanup(void) {
}

static bool s_heap_locked;

void arch_heap_lock(void) {
  if (arch_irq_disable_all() != 0U) {
    s_heap_locked = true;
  }
}

void arch_heap_unlock(void) {
  if (s_heap_locked) {
    s_heap_locked = false;
    arch_irq_unlock(RISCV_MSTATUS_MIE);
  }
}

void arch_switch_request(void) {
  riscv_switch_pending = true;
  if (!arch_in_isr() && !arch_irq_is_locked()) {
    __asm volatile("ecall" ::: "memory");
  }
}

void riscv_trap(struct riscv_frame *frame) {
  const uint32_t mcause = riscv_csr_read(mcause);

  if ((mcause & RISCV_MCAUSE_INTR) != 0U) {
    riscv_irq_dispatch(mcause & RISCV_MCAUSE_CODE_MASK);
  } else if (mcause == RISCV_EXC_ECALL_M && riscv_isr_nesting == 1U) {
    // A thread asking for the switch; it happens on the way out
    frame->mepc += 4U;
  } else {
    riscv_fault(frame, mcause);
  }
}

// ---- start and idle ---------------------------------------------------------

void arch_early_init(void) {
#ifdef CONFIG_RISCV_PCCR
  // Count cycles in machine mode
  riscv_csr_write(RISCV_CSR_PCER, 1U);
  riscv_csr_write(RISCV_CSR_PCMR, 1U);
#endif
}

void arch_init(void) {
}

void arch_start(void) {
  // Whatever was created since pbl_cur was set may come first.
  riscv_switch_pending = false;
  pbl_cur = sched_next;
  riscv_trap_restore(pbl_cur->backend.sp);
}

void arch_idle(pbl_tick_t max_ticks) {
  // wfi still wakes up with interrupts masked; they run once the tick is back.
  unsigned int key = arch_irq_lock();

  if (sched_idle_confirm()) {
    idle_enter(max_ticks);
  }
  arch_irq_unlock(key);
}

// ---- introspection ----------------------------------------------------------

// In an ISR, the running thread's SP is the frame of the trap it took, saved all the same.
void arch_thread_saved_regs(const struct pbl_thread *t, struct pbl_thread_saved_regs *regs) {
  const struct riscv_frame *frame = t->backend.sp;

  *regs = (struct pbl_thread_saved_regs){
    .pc = frame->mepc,
    .lr = frame->ra,
    .control = frame->mstatus,
  };
}

// The ARM-shaped register set: r0-r7 are a0-a7, r8-r12 s0-s4, xPSR mstatus.
void arch_thread_info_regs(const struct pbl_thread *t, uint32_t regs[PBL_THREAD_REG_COUNT]) {
  const struct riscv_frame *frame = t->backend.sp;

  for (int i = 0; i < 8; i++) {
    regs[PBL_THREAD_REG_R0 + i] = frame->a[i];
  }
  regs[PBL_THREAD_REG_R8] = frame->s0_s1[0];
  regs[PBL_THREAD_REG_R9] = frame->s0_s1[1];
  regs[PBL_THREAD_REG_R10] = frame->s2_s11[0];
  regs[PBL_THREAD_REG_R11] = frame->s2_s11[1];
  regs[PBL_THREAD_REG_R12] = frame->s2_s11[2];
  regs[PBL_THREAD_REG_SP] = (uint32_t)(uintptr_t)frame + sizeof(*frame);
  regs[PBL_THREAD_REG_LR] = frame->ra;
  regs[PBL_THREAD_REG_PC] = frame->mepc;
  regs[PBL_THREAD_REG_XPSR] = frame->mstatus;
}

// ---- no MPU -----------------------------------------------------------------

void arch_mpu_init(void) {
}

void arch_mpu_enable(void) {
}

void arch_mpu_region_set(unsigned int slot, const struct pbl_mpu_region *region) {
  (void)slot;
  (void)region;
}

void arch_mpu_user_ro_write(int slot, void *dst, const void *src, size_t n) {
  (void)slot;
  memcpy(dst, src, n);
}

bool arch_mpu_region_get(unsigned int slot, struct pbl_mpu_region *region) {
  (void)slot;
  (void)region;
  return false;
}

// RISC-V has no architectural reset: SoCs select SOC_HAS_RESET.
void arch_reset(void) {
  for (;;) {
    arch_cpu_idle();
  }
}
