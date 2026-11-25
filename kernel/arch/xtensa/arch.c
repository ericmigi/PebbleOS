/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "xtensa_frame.h"

#include <string.h>

#include <pbl/kernel/compiler.h>
#include <pbl/mcu/fpu.h>
#include <pbl/mcu/interrupts.h>

#include <kernel.h>
#include <pbl_arch_idle.h>

/*
 * Context switch: a thread's saved SP points at a trap frame, with all of its caller register
 * windows already spilled onto its stack. Every trap saves one; the switch happens on the way
 * out of the outermost trap, when one is due. A thread asks for one with interrupts locked, and
 * gets it when it unlocks them, through a syscall; an ISR gets it on its return.
 */

struct pbl_thread *pbl_cur;
extern struct pbl_thread *sched_next;

volatile uint32_t xtensa_isr_nesting;
volatile bool xtensa_switch_pending;
volatile bool xtensa_idle_defer_switch;

static_assert(offsetof(struct pbl_thread, backend.sp) == 0, "the trap entry saves the SP first");

void arch_cur_set(struct pbl_thread *t) {
  pbl_cur = t;
}

// ---- threads ----------------------------------------------------------------

void arch_thread_init(struct pbl_thread *t, void (*entry)(void *), void *arg) {
  uintptr_t top = ((uintptr_t)t->stack + t->stack_size) & ~15U;
  struct xtensa_frame *frame = (struct xtensa_frame *)(top - XT_FRAME_ALLOC);

  memset(frame, 0, sizeof(*frame));
  frame->pc = (uint32_t)(uintptr_t)xtensa_thread_trampoline;
  frame->ps = XTENSA_PS_WOE | XTENSA_PS_UM;
  // Leave 16 bytes at the top of the stack for the initial call window's base save area.
  frame->a[1] = (uint32_t)(top - 16U);
  frame->a[2] = (uint32_t)(uintptr_t)entry;
  frame->a[3] = (uint32_t)(uintptr_t)arg;
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
    arch_irq_unlock(0U);
    arch_cpu_idle();
  }
}

// ---- interrupts -------------------------------------------------------------

bool arch_in_isr(void) {
  return xtensa_isr_nesting != 0U;
}

bool mcu_state_are_interrupts_enabled(void) {
  return !arch_irq_is_locked();
}

void mcu_fpu_cleanup(void) {
}

static bool s_heap_locked;

void arch_heap_lock(void) {
  if (arch_irq_disable_all() == 0U) {
    s_heap_locked = true;
  }
}

void arch_heap_unlock(void) {
  if (s_heap_locked) {
    s_heap_locked = false;
    arch_irq_unlock(0U);
  }
}

void arch_switch_request(void) {
  xtensa_switch_pending = true;
  if (!arch_in_isr() && !arch_irq_is_locked()) {
    __asm volatile("syscall" ::: "memory");
  }
}

void xtensa_trap(struct xtensa_frame *frame) {
  switch (frame->exccause) {
    case XTENSA_EXCCAUSE_LEVEL1_INTERRUPT:
      xtensa_irq_dispatch();
      break;
    case XTENSA_EXCCAUSE_SYSCALL:
      if (xtensa_isr_nesting == 1U) {
        // syscall is a 3-byte instruction; the switch happens on trap exit.
        frame->pc += 3U;
        break;
      }
      xtensa_fault(frame);
      break;
    case XTENSA_EXCCAUSE_ALLOCA:
      // All caller windows were already spilled by trap entry; re-executing movsp succeeds.
      break;
    default:
      xtensa_fault(frame);
      break;
  }
}

// ---- start and idle ---------------------------------------------------------

void arch_early_init(void) {
  // Enable coprocessor 0 (single-precision FPU) and install the IRAM vector table.
  xtensa_sr_write(cpenable, 1U);
  xtensa_sr_write(intenable, 0U);
  xtensa_sr_write(vecbase, (uint32_t)(uintptr_t)xtensa_vectors);
  __asm volatile("isync" ::: "memory");
}

void arch_init(void) {
}

void arch_start(void) {
  xtensa_switch_pending = false;
  pbl_cur = sched_next;
  xtensa_trap_restore(pbl_cur->backend.sp);
}

void arch_idle(pbl_tick_t max_ticks) {
  unsigned int key = arch_irq_lock();

  if (sched_idle_confirm()) {
    idle_enter(max_ticks);
  }
  arch_irq_unlock(key);
}

// ---- introspection ----------------------------------------------------------

void arch_thread_saved_regs(const struct pbl_thread *t, struct pbl_thread_saved_regs *regs) {
  const struct xtensa_frame *frame = t->backend.sp;

  *regs = (struct pbl_thread_saved_regs){
    .pc = frame->pc,
    .lr = frame->a[0],
    .control = frame->ps,
  };
}

// Map Xtensa registers onto the ARM-shaped PBL_THREAD_REG_* slots for diagnostics.
void arch_thread_info_regs(const struct pbl_thread *t, uint32_t regs[PBL_THREAD_REG_COUNT]) {
  const struct xtensa_frame *frame = t->backend.sp;

  for (int i = 0; i < 13; i++) {
    regs[PBL_THREAD_REG_R0 + i] = frame->a[i + 2];
  }
  regs[PBL_THREAD_REG_SP] = frame->a[1];
  regs[PBL_THREAD_REG_LR] = frame->a[0];
  regs[PBL_THREAD_REG_PC] = frame->pc;
  regs[PBL_THREAD_REG_XPSR] = frame->ps;
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

// Xtensa has no architectural reset: SoCs select SOC_HAS_RESET.
void arch_reset(void) {
  for (;;) {
    arch_cpu_idle();
  }
}
