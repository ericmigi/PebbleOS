/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "riscv_frame.h"

#include <inttypes.h>

#include <pbl/kernel/fatal.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/util/size.h>

#include <kernel.h>

static const char *const s_causes[] = {
  [0] = "instruction address misaligned",
  [1] = "instruction access fault",
  [2] = "illegal instruction",
  [3] = "breakpoint",
  [4] = "load address misaligned",
  [5] = "load access fault",
  [6] = "store address misaligned",
  [7] = "store access fault",
  [8] = "ecall from U-mode",
  [11] = "ecall from M-mode",
};

static enum pbl_fault_reason prv_reason(uint32_t mcause) {
  switch (mcause) {
    case 1:
    case 5:
    case 7:
      return PBL_FAULT_BUS;
    default:
      return PBL_FAULT_USAGE;
  }
}

static const char *prv_thread_name(void) {
  return (pbl_cur != nullptr && riscv_isr_nesting == 1U) ? pbl_cur->name : "ISR";
}

[[noreturn]] static void prv_thread_fault(uint32_t reason, uintptr_t pc, uintptr_t lr,
                                          uintptr_t addr) {
  const struct pbl_fault_info info = {
    .reason = (enum pbl_fault_reason)reason,
    .pc = pc,
    .lr = lr,
    .addr = addr,
  };
  pbl_fault_thread_handler(&info);
}

void riscv_fault(struct riscv_frame *frame, uint32_t mcause) {
  static bool s_in_fault;

  // Recording or logging the first fault faulted: what is left to do is reset
  if (s_in_fault) {
    pbl_soc_reset();
  }
  s_in_fault = true;

  const uint32_t mtval = riscv_csr_read(mtval);
  const char *cause = (mcause < PBL_ARRAY_SIZE(s_causes) && s_causes[mcause] != nullptr)
                          ? s_causes[mcause]
                          : "unknown";
  // A trap taken while handling another can't be blamed on what the first one interrupted
  const struct pbl_fault_info info = {
    .reason = riscv_isr_nesting > 1U ? PBL_FAULT_HARD : prv_reason(mcause),
    .pc = frame->mepc,
    .lr = frame->ra,
    .addr = mtval,
  };
  const uint32_t *x = (const uint32_t *)frame;

  // A thread's own fault, taken with interrupts on so it holds no critical section, only stops
  // the thread
  const bool own = (riscv_isr_nesting == 1U) && (frame->mstatus & RISCV_MSTATUS_MPIE) &&
                   pbl_fault_thread_is_own(&info);
  if (!own) {
    pbl_fault_fatal_record(&info);
  }

  PBL_LOG_FAULT("\r\n\r\n[Fault: %s, thread %s]", cause, prv_thread_name());
  PBL_LOG_FAULT("mcause  = 0x%08" PRIx32 "  mepc = 0x%08" PRIx32 "  mtval = 0x%08" PRIx32, mcause,
                frame->mepc, mtval);
  PBL_LOG_FAULT("mstatus = 0x%08" PRIx32 "  sp   = 0x%08" PRIx32 "  nesting = %" PRIu32,
                frame->mstatus, (uint32_t)(uintptr_t)frame + RISCV_FRAME_SIZE, riscv_isr_nesting);
  for (int i = 4; i < RISCV_FRAME_WORDS; i += 4) {
    PBL_LOG_FAULT("x%-2d = 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32, i, x[i],
                  x[i + 1], x[i + 2], x[i + 3]);
  }
  PBL_LOG_FAULT("ra  = 0x%08" PRIx32, frame->ra);

  // Back to the thread, in prv_thread_fault(), which never returns to the code that faulted
  if (own) {
    frame->a[0] = (uint32_t)info.reason;
    frame->a[1] = info.pc;
    frame->a[2] = info.lr;
    frame->a[3] = info.addr;
    frame->ra = 0U;
    frame->mepc = (uint32_t)(uintptr_t)prv_thread_fault;
    s_in_fault = false;
    return;
  }

  pbl_fault_fatal_handler(&info);
}
