/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <pbl_arch_riscv.h>

// Enabled by arch_early_init(). Inline: code running from RAM counts cycles too.
static PBL_ALWAYS_INLINE uint32_t arch_cycle_get_32(void) {
#ifdef CONFIG_RISCV_PCCR
  return riscv_csr_read(RISCV_CSR_PCCR);
#else
  return riscv_csr_read(mcycle);
#endif
}
