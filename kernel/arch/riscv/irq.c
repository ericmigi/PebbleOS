/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/compiler.h>
#include <pbl/util/listify.h>

#include <kernel.h>

void arch_irq_spurious(void);
void arch_irq_spurious(void) {
  // A line enabled with no handler
  PBL_ASSERTN(false);
}

#define RISCV_ISR_DECLARE(n) PBL_WEAK PBL_ALIAS("arch_irq_spurious") void pbl_isr_##n(void);
PBL_LISTIFY(CONFIG_NUM_IRQS, RISCV_ISR_DECLARE)

void (*const riscv_isr_table[CONFIG_NUM_IRQS])(void) = {
#define RISCV_ISR_ENTRY(n) pbl_isr_##n,
  PBL_LISTIFY(CONFIG_NUM_IRQS, RISCV_ISR_ENTRY)
};
