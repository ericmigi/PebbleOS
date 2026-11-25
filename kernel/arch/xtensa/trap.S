/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "xtensa_frame.h"

#define PS_EXCM_INT3_UM   0x00000033 /* EXCM=1, UM=1, INTLEVEL=3 */
#define PS_WOE_UM_INT3    0x00040023 /* WOE=1,  UM=1, INTLEVEL=3 */
#define PS_EXCM           0x00000010

/*
 * Xtensa LX7 (ESP32-S3) vector table in IRAM (1024-byte aligned).
 * Placed in .vectors, which ramfunc.ld puts at the start of .ramfunc so window
 * and interrupt vectors stay reachable even when the flash cache is suspended.
 */
	.section .vectors, "ax"
	.balign 1024
	.global xtensa_vectors
xtensa_vectors:

	/* 0x000: WindowOverflow4 */
	.org xtensa_vectors + 0x000
	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5, -8
	s32e	a3, a5, -4
	rfwo

	/* 0x040: WindowUnderflow4 */
	.org xtensa_vectors + 0x040
	l32e	a0, a5, -16
	l32e	a1, a5, -12
	l32e	a2, a5, -8
	l32e	a3, a5, -4
	rfwu

	/* 0x080: WindowOverflow8 */
	.org xtensa_vectors + 0x080
	s32e	a0, a9, -16
	l32e	a0, a1, -12
	s32e	a1, a9, -12
	s32e	a2, a9, -8
	s32e	a3, a9, -4
	s32e	a4, a0, -32
	s32e	a5, a0, -28
	s32e	a6, a0, -24
	s32e	a7, a0, -20
	rfwo

	/* 0x0c0: WindowUnderflow8 */
	.org xtensa_vectors + 0x0c0
	l32e	a0, a9, -16
	l32e	a1, a9, -12
	l32e	a2, a9, -8
	l32e	a7, a1, -12
	l32e	a3, a9, -4
	l32e	a4, a7, -32
	l32e	a5, a7, -28
	l32e	a6, a7, -24
	l32e	a7, a7, -20
	rfwu

	/* 0x100: WindowOverflow12 */
	.org xtensa_vectors + 0x100
	s32e	a0, a13, -16
	l32e	a0, a1, -12
	s32e	a1, a13, -12
	s32e	a2, a13, -8
	s32e	a3, a13, -4
	s32e	a4, a0, -48
	s32e	a5, a0, -44
	s32e	a6, a0, -40
	s32e	a7, a0, -36
	s32e	a8, a0, -32
	s32e	a9, a0, -28
	s32e	a10, a0, -24
	s32e	a11, a0, -20
	rfwo

	/* 0x140: WindowUnderflow12 */
	.org xtensa_vectors + 0x140
	l32e	a0, a13, -16
	l32e	a1, a13, -12
	l32e	a2, a13, -8
	l32e	a11, a1, -12
	l32e	a3, a13, -4
	l32e	a4, a11, -48
	l32e	a5, a11, -44
	l32e	a6, a11, -40
	l32e	a7, a11, -36
	l32e	a8, a11, -32
	l32e	a9, a11, -28
	l32e	a10, a11, -24
	l32e	a11, a11, -20
	rfwu

	/* 0x180..0x2c0: Unused high-level interrupt vectors */
	.org xtensa_vectors + 0x180
	rfi	2
	.org xtensa_vectors + 0x1c0
	rfi	3
	.org xtensa_vectors + 0x200
	rfi	4
	.org xtensa_vectors + 0x240
	rfi	5
	.org xtensa_vectors + 0x280
	rfi	6
	.org xtensa_vectors + 0x2c0
	rfi	7

	/* 0x300: KernelExceptionVector */
	.org xtensa_vectors + 0x300
	wsr.excsave1	a0
	j	xtensa_trap_entry

	/* 0x340: UserExceptionVector */
	.org xtensa_vectors + 0x340
	wsr.excsave1	a0
	j	xtensa_trap_entry

	/* 0x3c0: DoubleExceptionVector */
	.org xtensa_vectors + 0x3c0
	wsr.excsave1	a0
	j	xtensa_trap_entry

	.org xtensa_vectors + 0x400

/*
 * Common trap entry in IRAM (reachable from the vectors with a direct jump).
 * Runs initially with PS.EXCM=1 so neither interrupts nor window exceptions fire.
 */
	.section .ramfunc.text.trap, "ax"
	.balign 4
	.literal_position
xtensa_trap_entry:
	/* Fast path for EXCCAUSE_ALLOCA (5): underflow the caller window so movsp succeeds */
	rsr.exccause	a0
	addi	a0, a0, -5
	beqz	a0, .Lalloca_exc

	/* Allocate trap frame + 32B red zone below interrupted SP */
	addi	a1, a1, -XT_FRAME_ALLOC
	s32i	a2, a1, XT_FRAME_A2
	s32i	a3, a1, XT_FRAME_A3
	rsr.excsave1	a2
	s32i	a2, a1, XT_FRAME_A0
	addi	a2, a1, XT_FRAME_ALLOC
	s32i	a2, a1, XT_FRAME_A1
	s32i	a4, a1, XT_FRAME_A4
	s32i	a5, a1, XT_FRAME_A5
	s32i	a6, a1, XT_FRAME_A6
	s32i	a7, a1, XT_FRAME_A7
	s32i	a8, a1, XT_FRAME_A8
	s32i	a9, a1, XT_FRAME_A9
	s32i	a10, a1, XT_FRAME_A10
	s32i	a11, a1, XT_FRAME_A11
	s32i	a12, a1, XT_FRAME_A12
	s32i	a13, a1, XT_FRAME_A13
	s32i	a14, a1, XT_FRAME_A14
	s32i	a15, a1, XT_FRAME_A15

	rsr.epc1	a2
	s32i	a2, a1, XT_FRAME_PC
	rsr.ps	a2
	s32i	a2, a1, XT_FRAME_PS
	rsr.sar	a2
	s32i	a2, a1, XT_FRAME_SAR
	rsr.lbeg	a2
	s32i	a2, a1, XT_FRAME_LBEG
	rsr.lend	a2
	s32i	a2, a1, XT_FRAME_LEND
	rsr.lcount	a2
	s32i	a2, a1, XT_FRAME_LCOUNT
	rsr.exccause	a2
	s32i	a2, a1, XT_FRAME_EXCCAUSE
	rsr.excvaddr	a2
	s32i	a2, a1, XT_FRAME_EXCVADDR

	/* Save FPU state (CP0 is always enabled by arch_early_init) */
	rur.fcr	a2
	s32i	a2, a1, XT_FRAME_FCR
	rur.fsr	a2
	s32i	a2, a1, XT_FRAME_FSR
	ssi	f0, a1, XT_FRAME_F0 + 0
	ssi	f1, a1, XT_FRAME_F0 + 4
	ssi	f2, a1, XT_FRAME_F0 + 8
	ssi	f3, a1, XT_FRAME_F0 + 12
	ssi	f4, a1, XT_FRAME_F0 + 16
	ssi	f5, a1, XT_FRAME_F0 + 20
	ssi	f6, a1, XT_FRAME_F0 + 24
	ssi	f7, a1, XT_FRAME_F0 + 28
	ssi	f8, a1, XT_FRAME_F0 + 32
	ssi	f9, a1, XT_FRAME_F0 + 36
	ssi	f10, a1, XT_FRAME_F0 + 40
	ssi	f11, a1, XT_FRAME_F0 + 44
	ssi	f12, a1, XT_FRAME_F0 + 48
	ssi	f13, a1, XT_FRAME_F0 + 52
	ssi	f14, a1, XT_FRAME_F0 + 56
	ssi	f15, a1, XT_FRAME_F0 + 60

	/* Keep frame pointer in MISC0, restore orig_sp in a1 for caller spilling */
	wsr.misc0	a1
	addi	a1, a1, XT_FRAME_ALLOC

	/*
	 * Spill all other live register windows (every bit in WINDOWSTART other
	 * than current WINDOWBASE). Only a0, a2, a3 are touched before rotation so
	 * a1 (orig_sp) and a4..a15 (which may belong to wrapped-around callers)
	 * remain intact.
	 */
	rsr.windowbase	a2
	rsr.windowstart	a3
	slli	a0, a3, 16
	or	a3, a3, a0
	ssr	a2
	srl	a3, a3
	extui	a3, a3, 1, 15
	beqz	a3, .Lspill_done

	/* Set bit 15 as sentinel for wb0 (the child of the newest live caller) */
	movi	a0, 0x8000
	or	a3, a3, a0

	/* Advance across empty panes until pane +1 is the oldest live caller */
.Lskip_empty:
	bbsi	a3, 0, .Lnext_is_live
	srli	a3, a3, 1
	wsr.misc1	a3
	rotw	1
	rsr.misc1	a3
	j	.Lskip_empty

	/*
	 * Invariant: current pane is p-1 (free scratch), bit 0 of a3 is 1 (pane p
	 * is live), and highest set bit of a3 is wb0. When a3 == 1, p == wb0.
	 */
.Lnext_is_live:
	movi	a0, 1
	beq	a3, a0, .Lspill_done
	bbsi	a3, 1, .Lspill_c4
	bbsi	a3, 2, .Lspill_c8

	/* CALL12: next live pane is p+3 */
	srli	a3, a3, 3
	wsr.misc1	a3
	rotw	1
	s32e	a0, a13, -16
	l32e	a0, a1, -12
	s32e	a1, a13, -12
	s32e	a2, a13, -8
	s32e	a3, a13, -4
	s32e	a4, a0, -48
	s32e	a5, a0, -44
	s32e	a6, a0, -40
	s32e	a7, a0, -36
	s32e	a8, a0, -32
	s32e	a9, a0, -28
	s32e	a10, a0, -24
	s32e	a11, a0, -20
	rotw	2
	rsr.misc1	a3
	j	.Lnext_is_live

.Lspill_c8:
	/* CALL8: next live pane is p+2 */
	srli	a3, a3, 2
	wsr.misc1	a3
	rotw	1
	s32e	a0, a9, -16
	l32e	a0, a1, -12
	s32e	a1, a9, -12
	s32e	a2, a9, -8
	s32e	a3, a9, -4
	s32e	a4, a0, -32
	s32e	a5, a0, -28
	s32e	a6, a0, -24
	s32e	a7, a0, -20
	rotw	1
	rsr.misc1	a3
	j	.Lnext_is_live

.Lspill_c4:
	/* CALL4: next live pane is p+1 */
	srli	a3, a3, 1
	wsr.misc1	a3
	rotw	1
	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5, -8
	s32e	a3, a5, -4
	rsr.misc1	a3
	j	.Lnext_is_live

.Lspill_done:
	/* Reset register window state cleanly before entering C */
	movi	a0, 0
	wsr.windowbase	a0
	rsync
	movi	a0, 1
	wsr.windowstart	a0
	rsync

	/* a2 = frame pointer (preserved across callx4!) */
	rsr.misc0	a2

	/* Increment xtensa_isr_nesting */
	movi	a3, xtensa_isr_nesting
	l32i	a4, a3, 0
	addi	a5, a4, 1
	s32i	a5, a3, 0
	bnez	a4, .Lon_isr_stack

	/* Already on ISR stack (early boot before first thread)? */
	movi	a4, __isr_stack_start__
	bltu	a2, a4, .Lfrom_thread
	movi	a4, _estack
	bltu	a2, a4, .Lon_isr_stack

.Lfrom_thread:
	movi	a4, pbl_cur
	l32i	a4, a4, 0
	beqz	a4, .Lload_estack
	s32i	a2, a4, 0
.Lload_estack:
	movi	a1, _estack
	addi	a1, a1, -16
	j	.Lcall_c

.Lon_isr_stack:
	addi	a1, a2, -16

.Lcall_c:
	/* Leave EXCM with WOE=1, UM=1, INTLEVEL=3 so C can make windowed calls */
	movi	a3, PS_WOE_UM_INT3
	wsr.ps	a3
	rsync

	mov	a6, a2
	movi	a4, xtensa_trap
	callx4	a4

	/* Decrement xtensa_isr_nesting */
	movi	a3, xtensa_isr_nesting
	l32i	a4, a3, 0
	addi	a4, a4, -1
	s32i	a4, a3, 0
	bnez	a4, .Ltrap_restore_from_a2

	/* Defer switch if waking inside arch_cpu_idle() */
	movi	a3, xtensa_idle_defer_switch
	l8ui	a4, a3, 0
	bnez	a4, .Ltrap_restore_from_a2

	movi	a3, xtensa_switch_pending
	l8ui	a4, a3, 0
	beqz	a4, .Ltrap_restore_from_a2
	movi	a4, 0
	s8i	a4, a3, 0

	/* pbl_cur = sched_next; a2 = pbl_cur->backend.sp */
	movi	a3, sched_next
	l32i	a4, a3, 0
	movi	a3, pbl_cur
	s32i	a4, a3, 0
	l32i	a2, a4, 0
	j	.Ltrap_restore_from_a2

	.balign 4
	.literal_position
	.global xtensa_trap_restore
xtensa_trap_restore:
	entry	a1, 16
	/* fall through with a2 = frame */

.Ltrap_restore_from_a2:
	/* Re-enter EXCM=1, INTLEVEL=3 before tearing down window state */
	movi	a3, PS_EXCM_INT3_UM
	wsr.ps	a3
	rsync

	wsr.misc0	a2
	movi	a3, 0
	wsr.windowbase	a3
	rsync
	movi	a3, 1
	wsr.windowstart	a3
	rsync
	rsr.misc0	a1

	/* Restore FPU state */
	l32i	a2, a1, XT_FRAME_FCR
	wur.fcr	a2
	l32i	a2, a1, XT_FRAME_FSR
	wur.fsr	a2
	lsi	f0, a1, XT_FRAME_F0 + 0
	lsi	f1, a1, XT_FRAME_F0 + 4
	lsi	f2, a1, XT_FRAME_F0 + 8
	lsi	f3, a1, XT_FRAME_F0 + 12
	lsi	f4, a1, XT_FRAME_F0 + 16
	lsi	f5, a1, XT_FRAME_F0 + 20
	lsi	f6, a1, XT_FRAME_F0 + 24
	lsi	f7, a1, XT_FRAME_F0 + 28
	lsi	f8, a1, XT_FRAME_F0 + 32
	lsi	f9, a1, XT_FRAME_F0 + 36
	lsi	f10, a1, XT_FRAME_F0 + 40
	lsi	f11, a1, XT_FRAME_F0 + 44
	lsi	f12, a1, XT_FRAME_F0 + 48
	lsi	f13, a1, XT_FRAME_F0 + 52
	lsi	f14, a1, XT_FRAME_F0 + 56
	lsi	f15, a1, XT_FRAME_F0 + 60

	/* Restore special registers; keep EXCM=1 until rfe clears it atomically */
	l32i	a2, a1, XT_FRAME_PC
	wsr.epc1	a2
	l32i	a2, a1, XT_FRAME_SAR
	wsr.sar	a2
	l32i	a2, a1, XT_FRAME_LBEG
	wsr.lbeg	a2
	l32i	a2, a1, XT_FRAME_LEND
	wsr.lend	a2
	l32i	a2, a1, XT_FRAME_LCOUNT
	wsr.lcount	a2
	l32i	a2, a1, XT_FRAME_PS
	movi	a3, PS_EXCM
	or	a2, a2, a3
	wsr.ps	a2
	rsync

	l32i	a0, a1, XT_FRAME_A0
	l32i	a2, a1, XT_FRAME_A2
	l32i	a3, a1, XT_FRAME_A3
	l32i	a4, a1, XT_FRAME_A4
	l32i	a5, a1, XT_FRAME_A5
	l32i	a6, a1, XT_FRAME_A6
	l32i	a7, a1, XT_FRAME_A7
	l32i	a8, a1, XT_FRAME_A8
	l32i	a9, a1, XT_FRAME_A9
	l32i	a10, a1, XT_FRAME_A10
	l32i	a11, a1, XT_FRAME_A11
	l32i	a12, a1, XT_FRAME_A12
	l32i	a13, a1, XT_FRAME_A13
	l32i	a14, a1, XT_FRAME_A14
	l32i	a15, a1, XT_FRAME_A15
	l32i	a1, a1, XT_FRAME_A1
	rfe

	/*
	 * Alloca (movsp) exception handler: when the caller window is spilled to
	 * [sp - 16], movsp traps so the caller can be underflowed back into the
	 * register file before sp moves. Set PS.OWB = current WINDOWBASE, rotate
	 * back by the caller's call increment (a0[31:30]), and enter the matching
	 * WindowUnderflow vector whose rfwu resumes movsp.
	 */
.Lalloca_exc:
	rsr.windowbase	a0
	rotw	-1
	rsr.ps	a2
	extui	a3, a2, 8, 4
	xor	a3, a3, a4
	rsr.excsave1	a4
	slli	a3, a3, 8
	xor	a2, a2, a3
	wsr.ps	a2
	rsync
	bbci	a4, 31, .Lalloca_uf4
	rotw	-1
	bbci	a8, 30, .Lalloca_uf8
	rotw	-1
	j	xtensa_vectors + 0x140
.Lalloca_uf4:
	j	xtensa_vectors + 0x040
.Lalloca_uf8:
	j	xtensa_vectors + 0x0c0

/*
 * Thread entry trampoline (entered via rfe with a2 = entry, a3 = arg).
 */
	.section .text.xtensa_trampolines, "ax"
	.balign 4
	.literal_position
	.global xtensa_thread_trampoline
xtensa_thread_trampoline:
	mov	a6, a3
	callx4	a2
	movi	a6, 0
	movi	a4, pbl_thread_abort
	callx4	a4
1:
	j	1b

	.balign 4
	.literal_position
	.global xtensa_fault_trampoline
xtensa_fault_trampoline:
	mov	a6, a2
	mov	a7, a3
	mov	a8, a4
	mov	a9, a5
	movi	a10, xtensa_thread_fault
	callx4	a10
1:
	j	1b

/*
 * Firmware entry point in XIP flash (jumped to by the SoC boot header stub).
 */
	.section .reset, "ax"
	.balign 4
	.global Reset_Handler
Reset_Handler:
	j	.Lreset_body
	.balign 4
	.literal_position
.Lreset_body:
	movi	a2, PS_WOE_UM_INT3
	wsr.ps	a2
	rsync
	movi	a2, 0
	wsr.windowbase	a2
	rsync
	movi	a2, 1
	wsr.windowstart	a2
	rsync
	movi	a1, _estack
	addi	a1, a1, -16
	movi	a4, kernel_prep_c
	callx4	a4
1:
	j	1b
