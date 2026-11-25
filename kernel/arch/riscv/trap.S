/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "riscv_frame.h"

#define FRAME(i) (4 * (i))(sp)

/*
 * Every trap: save the frame on the interrupted stack, move to the ISR stack unless nested,
 * handle the trap, then switch thread if one is due and nothing is nested any more.
 */

	.section .vectors, "ax"
	.option push
	.option norvc
	.balign 256
	.global riscv_vectors
riscv_vectors:
	/* Exceptions take entry 0, interrupt line n entry n */
	.rept 32
	j riscv_trap_entry
	.endr
	.option pop

	/* Next to the vectors, which reach it with a jump of at most 1 MiB */
	.balign 4
riscv_trap_entry:
	addi sp, sp, -RISCV_FRAME_SIZE
	sw x1, FRAME(1)
	sw x4, FRAME(4)
	sw x5, FRAME(5)
	sw x6, FRAME(6)
	sw x7, FRAME(7)
	sw x8, FRAME(8)
	sw x9, FRAME(9)
	sw x10, FRAME(10)
	sw x11, FRAME(11)
	sw x12, FRAME(12)
	sw x13, FRAME(13)
	sw x14, FRAME(14)
	sw x15, FRAME(15)
	sw x16, FRAME(16)
	sw x17, FRAME(17)
	sw x18, FRAME(18)
	sw x19, FRAME(19)
	sw x20, FRAME(20)
	sw x21, FRAME(21)
	sw x22, FRAME(22)
	sw x23, FRAME(23)
	sw x24, FRAME(24)
	sw x25, FRAME(25)
	sw x26, FRAME(26)
	sw x27, FRAME(27)
	sw x28, FRAME(28)
	sw x29, FRAME(29)
	sw x30, FRAME(30)
	sw x31, FRAME(31)
	csrr t0, mepc
	sw t0, FRAME(RISCV_FRAME_MEPC)
	csrr t0, mstatus
	sw t0, FRAME(RISCV_FRAME_MSTATUS)

	/* s0 keeps the frame across the handler */
	mv s0, sp
	lw t1, riscv_isr_nesting
	addi t2, t1, 1
	sw t2, riscv_isr_nesting, t0
	bnez t1, 1f

	/* Already on the ISR stack, as startup runs before the first thread: stay below the frame */
	la t0, __isr_stack_start__
	bltu sp, t0, 4f
	la t0, _estack
	bltu sp, t0, 1f
4:
	/* From a thread: its saved SP is the frame */
	lw t0, pbl_cur
	beqz t0, 2f
	sw sp, 0(t0)
2:
	la sp, _estack
1:
	mv a0, s0
	call riscv_trap

	lw t1, riscv_isr_nesting
	addi t1, t1, -1
	sw t1, riscv_isr_nesting, t0
	bnez t1, 3f
	lbu t1, riscv_switch_pending
	beqz t1, 3f
	sb zero, riscv_switch_pending, t0

	/* pbl_cur = sched_next, and its frame */
	lw t2, sched_next
	sw t2, pbl_cur, t0
	lw s0, 0(t2)
3:
	mv a0, s0
	/* Fall through */

	.global riscv_trap_restore
riscv_trap_restore:
	mv sp, a0
	lw t0, FRAME(RISCV_FRAME_MEPC)
	csrw mepc, t0
	lw t0, FRAME(RISCV_FRAME_MSTATUS)
	csrw mstatus, t0
	lw x1, FRAME(1)
	lw x4, FRAME(4)
	lw x5, FRAME(5)
	lw x6, FRAME(6)
	lw x7, FRAME(7)
	lw x8, FRAME(8)
	lw x9, FRAME(9)
	lw x10, FRAME(10)
	lw x11, FRAME(11)
	lw x12, FRAME(12)
	lw x13, FRAME(13)
	lw x14, FRAME(14)
	lw x15, FRAME(15)
	lw x16, FRAME(16)
	lw x17, FRAME(17)
	lw x18, FRAME(18)
	lw x19, FRAME(19)
	lw x20, FRAME(20)
	lw x21, FRAME(21)
	lw x22, FRAME(22)
	lw x23, FRAME(23)
	lw x24, FRAME(24)
	lw x25, FRAME(25)
	lw x26, FRAME(26)
	lw x27, FRAME(27)
	lw x28, FRAME(28)
	lw x29, FRAME(29)
	lw x30, FRAME(30)
	lw x31, FRAME(31)
	addi sp, sp, RISCV_FRAME_SIZE
	mret

	.section .reset, "ax"
	.global Reset_Handler
Reset_Handler:
	.option push
	.option norelax
	la gp, __global_pointer$
	.option pop
	csrci mstatus, 8
	la sp, _estack
	/* Vectored: interrupt line n jumps to entry n */
	la t0, riscv_vectors
	ori t0, t0, 1
	csrw mtvec, t0
	/* Wherever the linker puts it */
	tail kernel_prep_c
