/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! Trap frame, on the stack of whatever was interrupted. A 32-byte gap is kept above the frame
//! below the interrupted SP so spilling the interrupted caller's base save area at [orig_sp - 16]
//! never overlaps the saved frame.
#define XT_FRAME_A0       0
#define XT_FRAME_A1       4
#define XT_FRAME_A2       8
#define XT_FRAME_A3       12
#define XT_FRAME_A4       16
#define XT_FRAME_A5       20
#define XT_FRAME_A6       24
#define XT_FRAME_A7       28
#define XT_FRAME_A8       32
#define XT_FRAME_A9       36
#define XT_FRAME_A10      40
#define XT_FRAME_A11      44
#define XT_FRAME_A12      48
#define XT_FRAME_A13      52
#define XT_FRAME_A14      56
#define XT_FRAME_A15      60
#define XT_FRAME_PC       64
#define XT_FRAME_PS       68
#define XT_FRAME_SAR      72
#define XT_FRAME_LBEG     76
#define XT_FRAME_LEND     80
#define XT_FRAME_LCOUNT   84
#define XT_FRAME_EXCCAUSE 88
#define XT_FRAME_EXCVADDR 92
#define XT_FRAME_FCR      96
#define XT_FRAME_FSR      100
#define XT_FRAME_F0       104
#define XT_FRAME_SIZE     176
#define XT_FRAME_REDZONE  32
#define XT_FRAME_ALLOC    (XT_FRAME_SIZE + XT_FRAME_REDZONE)

#ifndef __ASSEMBLER__
#include <stdint.h>

struct xtensa_frame {
  uint32_t a[16];
  uint32_t pc;
  uint32_t ps;
  uint32_t sar;
  uint32_t lbeg;
  uint32_t lend;
  uint32_t lcount;
  uint32_t exccause;
  uint32_t excvaddr;
  uint32_t fcr;
  uint32_t fsr;
  uint32_t f[16];
  uint32_t pad[2];
};

static_assert(sizeof(struct xtensa_frame) == XT_FRAME_SIZE, "trap frame layout");
static_assert(XT_FRAME_SIZE % 16 == 0, "the ABI wants sp 16-byte aligned");
static_assert(XT_FRAME_ALLOC % 16 == 0, "total allocation must stay 16-byte aligned");

//! Called by the trap entry for every trap, on the ISR stack unless nested.
void xtensa_trap(struct xtensa_frame *frame);

//! Fault: logs the frame and ends in the fatal handler, or returns to the thread in
//! pbl_fault_thread_handler() when the fault is the thread's own (pbl_fault_thread_is_own()).
void xtensa_fault(struct xtensa_frame *frame);

//! Restores the frame at @p sp and returns from the trap there.
[[noreturn]] void xtensa_trap_restore(void *sp);

//! Thread entry trampoline (called via rfe with a2 = entry, a3 = arg).
void xtensa_thread_trampoline(void);

//! Vector table in IRAM, 1024-byte aligned.
extern const char xtensa_vectors[];
#endif
