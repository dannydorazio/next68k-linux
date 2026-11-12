/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The ROM loads the image at the a.out entry address.  0x04380000 is what
 * the NetBSD boot program uses: it is inside RAM bank 0 on every model and
 * leaves the start of RAM free for the kernel.
 *
 * Everything, including .bss, is emitted into the flat binary so the
 * payload can be appended directly after it.
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
	. = 0x04380000;

	.text : {
		*(.text.start)
		*(.text .text.*)
		*(.rodata .rodata.*)
	}

	.data : {
		*(.data .data.*)
		*(.bss .bss.* COMMON)
		. = ALIGN(16);
	}

	_payload = .;

	/DISCARD/ : {
		*(.comment) *(.note*) *(.eh_frame*)
	}
}
