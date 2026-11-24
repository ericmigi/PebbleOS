/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RISCV_MSTATUS_MIE   (1U << 3)
#define RISCV_MSTATUS_MPIE  (1U << 7)
#define RISCV_MSTATUS_MPP_M (3U << 11)

#define RISCV_MCAUSE_INTR      (1U << 31)
#define RISCV_MCAUSE_CODE_MASK 0x7fffffffU

#define RISCV_EXC_ECALL_M 11U

// Espressif performance counter CSRs: event enable, counter mode and cycle count
#define RISCV_CSR_PCER 0x7e0
#define RISCV_CSR_PCMR 0x7e1
#define RISCV_CSR_PCCR 0x7e2

#define RISCV_STR_(x) #x
#define RISCV_STR(x)  RISCV_STR_(x)

#define riscv_csr_read(csr)                                          \
  ({                                                                 \
    uint32_t v_;                                                     \
    __asm volatile("csrr %0, " RISCV_STR(csr) : "=r"(v_)::"memory"); \
    v_;                                                              \
  })

#define riscv_csr_write(csr, val) \
  __asm volatile("csrw " RISCV_STR(csr) ", %0" ::"rK"((uint32_t)(val)) : "memory")

//! Interrupt nesting depth, 0 in threads.
extern volatile uint32_t riscv_isr_nesting;

//! A context switch is due at the next trap exit.
extern volatile bool riscv_switch_pending;

//! Interrupt controller hook, implemented by the SoC: handles the interrupt @p cause, mcause
//! without its interrupt bit. Called with interrupts masked, on the ISR stack.
void riscv_irq_dispatch(uint32_t cause);

//! Handlers of the SoC interrupt lines, indexed by line; arch_irq_spurious() when unbound.
extern void (*const riscv_isr_table[CONFIG_NUM_IRQS])(void);
