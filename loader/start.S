/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeXT Linux loader entry point and kernel hand-off.
 *
 * The ROM monitor loads this image at its link address and calls it like a
 * subroutine with the boot argument string on the stack.  A pointer to the
 * monitor globals is kept in the second longword of the ROM vector table.
 * If loader_main() returns, its return value goes back to the ROM, which
 * treats zero as a failed boot.
 */

	.section .text.start, "ax"
	.globl	_start
_start:
	movew	%sr,%sp@-
	movew	#0x2700,%sr		| no interrupts
	movel	%sp@(6),%d1		| boot argument
	movec	%vbr,%a0
	movel	%d1,%sp@-
	movel	%a0,%sp@-
	movel	%a0@(4),%sp@-		| monitor globals
	jsr	loader_main		| loader_main(mg, vbr, arg)
	lea	%sp@(12),%sp
	movew	%sp@+,%sr
	rts

	.text

/*
 * void enter_kernel(const struct handoff *h, int is_040)
 *
 * Turn off the caches and the transparent translation registers the ROM
 * set up (one of them maps the whole address space), then jump to the
 * trampoline.  Does not return.
 */
	.globl	enter_kernel
enter_kernel:
	movew	#0x2700,%sr
	movel	%sp@(4),%a4
	tstl	%sp@(8)
	jeq	1f

	.chip	68040
	nop
	cpusha	%bc
	nop
	moveq	#0,%d0
	movec	%d0,%cacr
	cinva	%bc
	movec	%d0,%tc
	movec	%d0,%itt0
	movec	%d0,%itt1
	movec	%d0,%dtt0
	movec	%d0,%dtt1
	pflusha
	.chip	68k
	jra	2f

1:	.chip	68030
	movel	#0x0808,%d0		| clear and disable both caches
	movec	%d0,%cacr
	clrl	%sp@-
	pmove	%sp@,%tc
	pmove	%sp@,%tt0
	pmove	%sp@,%tt1
	addql	#4,%sp
	pflusha
	.chip	68k

2:	moveml	%a4@,%d0-%d2/%a0-%a3/%a5	| see struct handoff
	jmp	%a5@

/*
 * int cpu_is_040(void)
 *
 * The 68040's CACR has its instruction cache enable in bit 15, where the
 * 68030's has nothing and reads back 0.  The CACR is restored at once.
 */
	.globl	cpu_is_040
cpu_is_040:
	movew	%sr,%d1
	movew	#0x2700,%sr
	movel	%d2,%sp@-
	.chip	68030
	movec	%cacr,%d2
	movel	%d2,%d0
	orl	#0x00008000,%d0
	movec	%d0,%cacr
	movec	%cacr,%d0
	movec	%d2,%cacr
	.chip	68k
	movel	%sp@+,%d2
	movew	%d1,%sr
	andl	#0x00008000,%d0
	rts

/*
 * int cpu_is_060(void)
 *
 * Only for a CPU that cpu_is_040() accepts: the 68060's CACR keeps the
 * branch cache enable in bit 23, where the 68040's reads back 0.
 */
	.globl	cpu_is_060
cpu_is_060:
	movew	%sr,%d1
	movew	#0x2700,%sr
	movel	%d2,%sp@-
	.chip	68040
	movec	%cacr,%d2
	movel	%d2,%d0
	orl	#0x00800000,%d0
	movec	%d0,%cacr
	movec	%cacr,%d0
	movec	%d2,%cacr
	.chip	68k
	movel	%sp@+,%d2
	movew	%d1,%sr
	andl	#0x00800000,%d0
	rts

/*
 * void data_cache_off(int is_040)
 *
 * Write back and turn off the data cache, so that DMA from the disk lands
 * where the CPU will read it.  The instruction cache stays on.
 */
	.globl	data_cache_off
data_cache_off:
	tstl	%sp@(4)
	jeq	1f
	.chip	68040
	nop
	cpusha	%dc
	movel	#0x00008000,%d0		| instruction cache only
	movec	%d0,%cacr
	nop
	cinva	%dc
	.chip	68k
	rts
1:	.chip	68030
	movel	#0x0809,%d0		| clear both, instruction cache on
	movec	%d0,%cacr
	.chip	68k
	rts

/*
 * Final copy and kernel entry.  enter_kernel() runs a copy of this code
 * placed above both the loaded image and the kernel's final location,
 * because copying the kernel may overwrite the loader.  It must be
 * position independent and must not touch the stack.
 *
 *   a0 = kernel image source		a1 = kernel destination
 *   d0 = image size			d1 = bss size
 *   a2 = bootinfo source		d2 = bootinfo size
 *   a3 = kernel entry
 *
 * All sizes are in bytes and multiples of four.
 */
	.globl	trampoline, trampoline_end
trampoline:
	lsrl	#2,%d0
	jra	2f
1:	movel	%a0@+,%a1@+
2:	subql	#1,%d0
	jcc	1b

	lsrl	#2,%d1
	jra	4f
3:	clrl	%a1@+
4:	subql	#1,%d1
	jcc	3b

	/* the bootinfo goes directly behind the kernel's bss (_end) */
	lsrl	#2,%d2
	jra	6f
5:	movel	%a2@+,%a1@+
6:	subql	#1,%d2
	jcc	5b

	jmp	%a3@
trampoline_end:
