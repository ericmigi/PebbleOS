/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "xtensa_frame.h"

#include <inttypes.h>

#include <pbl/kernel/fatal.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/util/size.h>

#include <kernel.h>

static const char *const s_causes[] = {
  [0] = "illegal instruction",     [1] = "syscall",
  [2] = "instruction fetch error", [3] = "load/store error",
  [4] = "level-1 interrupt",       [5] = "alloca",
  [6] = "integer divide by zero",  [8] = "privileged instruction",
  [9] = "unaligned load/store",    [20] = "instruction prohibited",
  [28] = "load prohibited",        [29] = "store prohibited",
  [32] = "coprocessor 0 disabled",
};

static enum pbl_fault_reason prv_reason(uint32_t exccause) {
  switch (exccause) {
    case 2:
    case 3:
    case 20:
    case 28:
    case 29:
      return PBL_FAULT_BUS;
    default:
      return PBL_FAULT_USAGE;
  }
}

static const char *prv_thread_name(void) {
  return (pbl_cur != nullptr && xtensa_isr_nesting == 1U) ? pbl_cur->name : "ISR";
}

extern void xtensa_fault_trampoline(void);

[[noreturn]] void xtensa_thread_fault(uint32_t reason, uintptr_t pc, uintptr_t lr, uintptr_t addr) {
  const struct pbl_fault_info info = {
    .reason = (enum pbl_fault_reason)reason,
    .pc = pc,
    .lr = lr,
    .addr = addr,
  };
  pbl_fault_thread_handler(&info);
}

void xtensa_fault(struct xtensa_frame *frame) {
  static bool s_in_fault;

  if (s_in_fault) {
    pbl_soc_reset();
  }
  s_in_fault = true;

  const uint32_t exccause = frame->exccause;
  const char *cause = (exccause < PBL_ARRAY_SIZE(s_causes) && s_causes[exccause] != nullptr)
                          ? s_causes[exccause]
                          : "unknown";
  // In the windowed ABI a0 carries CALLINC in place of the return address's top two bits.
  const uintptr_t lr = frame->a[0] ? (frame->pc & 0xc0000000U) | (frame->a[0] & 0x3fffffffU) : 0;
  const struct pbl_fault_info info = {
    .reason = xtensa_isr_nesting > 1U ? PBL_FAULT_HARD : prv_reason(exccause),
    .pc = frame->pc,
    .lr = lr,
    .addr = frame->excvaddr,
  };

  const bool own = (xtensa_isr_nesting == 1U) && ((frame->ps & XTENSA_PS_INTLEVEL_MASK) == 0U) &&
                   pbl_fault_thread_is_own(&info);
  if (!own) {
    pbl_fault_fatal_record(&info);
  }

  PBL_LOG_FAULT("\r\n\r\n[Fault: %s, thread %s]", cause, prv_thread_name());
  PBL_LOG_FAULT("exccause = 0x%08" PRIx32 "  pc = 0x%08" PRIx32 "  excvaddr = 0x%08" PRIx32,
                exccause, frame->pc, frame->excvaddr);
  PBL_LOG_FAULT("ps       = 0x%08" PRIx32 "  sp = 0x%08" PRIx32 "  nesting  = %" PRIu32, frame->ps,
                frame->a[1], xtensa_isr_nesting);
  for (int i = 0; i < 16; i += 4) {
    PBL_LOG_FAULT("a%-2d = 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32, i,
                  frame->a[i], frame->a[i + 1], frame->a[i + 2], frame->a[i + 3]);
  }

  if (own) {
    frame->a[2] = (uint32_t)info.reason;
    frame->a[3] = info.pc;
    frame->a[4] = info.lr;
    frame->a[5] = info.addr;
    frame->pc = (uint32_t)(uintptr_t)xtensa_fault_trampoline;
    frame->ps = XTENSA_PS_WOE | XTENSA_PS_UM;
    s_in_fault = false;
    return;
  }

  pbl_fault_fatal_handler(&info);
}
