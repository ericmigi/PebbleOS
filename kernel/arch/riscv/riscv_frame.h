/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! Trap frame, on the stack of whatever was interrupted. Word i holds xi, but for x0, which holds
//! mepc, x2 (sp), which holds mstatus, and x3 (gp), which never changes and is not saved: the
//! stack pointer before the trap is the frame's address plus its size.
#define RISCV_FRAME_WORDS   32
#define RISCV_FRAME_SIZE    (RISCV_FRAME_WORDS * 4)
#define RISCV_FRAME_MEPC    0
#define RISCV_FRAME_MSTATUS 2

#ifndef __ASSEMBLER__
#include <stdint.h>

struct riscv_frame {
  uint32_t mepc;
  uint32_t ra;
  uint32_t mstatus;
  uint32_t gp;
  uint32_t tp;
  uint32_t t0_t2[3];
  uint32_t s0_s1[2];
  uint32_t a[8];
  uint32_t s2_s11[10];
  uint32_t t3_t6[4];
};

static_assert(sizeof(struct riscv_frame) == RISCV_FRAME_SIZE, "trap frame layout");
static_assert(RISCV_FRAME_SIZE % 16 == 0, "the ABI wants sp 16-byte aligned");

//! Called by the trap entry for every trap, on the ISR stack unless nested.
void riscv_trap(struct riscv_frame *frame);

//! Fault: logs the frame and ends in the fatal handler, or returns to the thread in
//! pbl_fault_thread_handler() when the fault is the thread's own (pbl_fault_thread_is_own()).
void riscv_fault(struct riscv_frame *frame, uint32_t mcause);

//! Restores the frame at @p sp and returns from the trap there.
[[noreturn]] void riscv_trap_restore(void *sp);
#endif
