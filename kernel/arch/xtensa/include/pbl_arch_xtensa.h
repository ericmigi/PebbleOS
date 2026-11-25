/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define XTENSA_PS_INTLEVEL_MASK 0x0000000fU
#define XTENSA_PS_EXCM          (1U << 4)
#define XTENSA_PS_UM            (1U << 5)
#define XTENSA_PS_WOE           (1U << 18)

#define XTENSA_EXCM_LEVEL 3U

#define XTENSA_EXCCAUSE_SYSCALL          1U
#define XTENSA_EXCCAUSE_LEVEL1_INTERRUPT 4U
#define XTENSA_EXCCAUSE_ALLOCA           5U

#define XTENSA_STR_(x) #x
#define XTENSA_STR(x)  XTENSA_STR_(x)

#define xtensa_sr_read(sr)                                            \
  ({                                                                  \
    uint32_t v_;                                                      \
    __asm volatile("rsr." XTENSA_STR(sr) " %0" : "=a"(v_)::"memory"); \
    v_;                                                               \
  })

#define xtensa_sr_write(sr, val) \
  __asm volatile("wsr." XTENSA_STR(sr) " %0\n\trsync" ::"a"((uint32_t)(val)) : "memory")

//! Interrupt nesting depth, 0 in threads.
extern volatile uint32_t xtensa_isr_nesting;

//! A context switch is due at the next trap exit.
extern volatile bool xtensa_switch_pending;

//! Defers context switch on ISR exit while inside arch_cpu_idle() so timekeeping_idle_end() runs.
extern volatile bool xtensa_idle_defer_switch;

//! Interrupt controller hook, implemented by the SoC: handles pending level-1 interrupts.
void xtensa_irq_dispatch(void);

//! Handlers of the SoC interrupt lines, indexed by line; arch_irq_spurious() when unbound.
extern void (*const xtensa_isr_table[CONFIG_NUM_IRQS])(void);
