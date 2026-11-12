//	i860 firmware for Linux's NeXTdimension driver (nextdimension.c)
//
//	The board's ROM branches here, to the start of its DRAM, when the
//	host interrupts it after loading this.  It runs without paging, on
//	the board's physical addresses.  It takes drawing commands for the
//	video memory from a ring in DRAM that the host fills, and never
//	returns.
//
//	Shared area, at DRAM offset 0x8000:
//	  +0  READY	this writes NDFW_MAGIC once it's running
//	  +4  HEAD	the host: where it will put the next command
//	  +8  TAIL	this: where it will take the next command
//	HEAD and TAIL count words from the start of the ring, which is
//	RING_WORDS long, at DRAM offset 0x10000.  The ring is empty when
//	they're equal.  A command doesn't wrap: WRAP sends this back to the
//	start.  Coordinates are byte offsets into the video memory, whose
//	lines are PITCH bytes long; widths are in pixels, heights in lines,
//	and neither is 0.
//
//	FILL  dst w h color			5 words
//	COPY  src dst w h			5 words
//	IMAGE dst w h fg bg stride bits...	6 words, then h rows of
//		stride bytes (a multiple of 4): one bit per pixel, most
//		significant first, set for fg
//	WRAP					1 word
//
//	Registers held throughout: r16 the shared area, r17 the ring, r18
//	the video memory, r19 PITCH, r20 TAIL.

	SHARED	= 0xF8008000
	READY	= 0
	HEAD	= 4
	TAIL	= 8
	RING	= 0xF8010000
	VRAM	= 0xFE000000
	PITCH	= 4608
	NDFW_MAGIC = 0x4E44464C		// "NDFL"

	OP_FILL	 = 1
	OP_COPY	 = 2
	OP_IMAGE = 3
	OP_WRAP	 = 4

	.text
	.globl	_start
_start:
	orh	h%SHARED, r0, r16
	or	l%SHARED, r16, r16
	orh	h%RING, r0, r17
	or	l%RING, r17, r17
	orh	h%VRAM, r0, r18
	or	PITCH, r0, r19
	or	r0, r0, r20
	st.l	r0, TAIL(r16)
	orh	h%NDFW_MAGIC, r0, r4
	or	l%NDFW_MAGIC, r4, r4
	st.l	r4, READY(r16)

idle:
	ld.l	HEAD(r16), r4
	bte	r4, r20, idle
	shl	2, r20, r21		// r21: the command
	addu	r21, r17, r21
	ld.l	0(r21), r5
	bte	OP_FILL, r5, fill
	bte	OP_COPY, r5, copy
	bte	OP_IMAGE, r5, image
	bte	OP_WRAP, r5, wrap
hang:	br	hang			// a bad command: stop, TAIL stays
	nop

wrap:
	or	r0, r0, r20
	st.l	r20, TAIL(r16)
	br	idle
	nop

//	Done with the command at r21, r6 words long
done:
	addu	r6, r20, r20
	st.l	r20, TAIL(r16)
	br	idle
	nop

//	FILL dst w h color
fill:
	ld.l	4(r21), r7		// r7: the line
	ld.l	8(r21), r8		// r8: width
	ld.l	12(r21), r9		// r9: lines left
	ld.l	16(r21), r10		// r10: color
	addu	r7, r18, r7
	or	5, r0, r6
	or	r7, r0, r4		// whole quads?  start and width
	and	15, r4, r0
	bnc	fill_line
	and	3, r8, r0
	bnc	fill_line
	ixfr	r10, f8			// f8-f11: four pixels
	ixfr	r10, f9
	ixfr	r10, f10
	ixfr	r10, f11
	shr	2, r8, r8		// r8: quads
fill_quads:
	adds	-16, r7, r11
	or	r8, r0, r12
fill_quad:
	fst.q	f8, 16(r11)++
	adds	-1, r12, r12
	btne	0, r12, fill_quad
	addu	r19, r7, r7
	adds	-1, r9, r9
	btne	0, r9, fill_quads
	br	done
	nop

fill_line:
	or	r7, r0, r11
	or	r8, r0, r12
fill_pixel:
	st.l	r10, 0(r11)
	addu	4, r11, r11
	adds	-1, r12, r12
	btne	0, r12, fill_pixel
	addu	r19, r7, r7
	adds	-1, r9, r9
	btne	0, r9, fill_line
	br	done
	nop

//	COPY src dst w h: forwards if dst is below src, else backwards from
//	the last pixel, so that overlapping areas come out right.
copy:
	ld.l	4(r21), r7		// r7: source line
	ld.l	8(r21), r13		// r13: destination line
	ld.l	12(r21), r8		// r8: width
	ld.l	16(r21), r9		// r9: lines left
	addu	r7, r18, r7
	addu	r13, r18, r13
	or	5, r0, r6
	and	3, r8, r0		// r22: 1 for whole quads
	bnc	copy_any
	and	15, r7, r0
	bnc	copy_any
	and	15, r13, r0
	bnc	copy_any
	or	1, r0, r22
	br	copy_dir
	nop
copy_any:
	or	r0, r0, r22
copy_dir:
	subu	r7, r13, r0		// CC: dst <= src
	bc	copy_fwd

	// backwards: from the end of the last line, upwards
	adds	-1, r9, r4
copy_last:
	bte	0, r4, copy_bwd
	addu	r19, r7, r7
	addu	r19, r13, r13
	adds	-1, r4, r4
	br	copy_last
	nop
copy_bwd:
	shl	2, r8, r4		// r7, r13: ends of the lines
	addu	r4, r7, r7
	addu	r4, r13, r13
	bte	0, r22, copy_bwd_line
	shr	2, r8, r8
copy_bwd_quads:
	or	r7, r0, r11
	or	r13, r0, r14
	or	r8, r0, r12
copy_bwd_quad:
	fld.q	-16(r11)++, f8
	fst.q	f8, -16(r14)++
	adds	-1, r12, r12
	btne	0, r12, copy_bwd_quad
	subu	r7, r19, r7
	subu	r13, r19, r13
	adds	-1, r9, r9
	btne	0, r9, copy_bwd_quads
	br	done
	nop
copy_bwd_line:
	or	r7, r0, r11
	or	r13, r0, r14
	or	r8, r0, r12
copy_bwd_pixel:
	adds	-4, r11, r11
	adds	-4, r14, r14
	ld.l	0(r11), r4
	st.l	r4, 0(r14)
	adds	-1, r12, r12
	btne	0, r12, copy_bwd_pixel
	subu	r7, r19, r7
	subu	r13, r19, r13
	adds	-1, r9, r9
	btne	0, r9, copy_bwd_line
	br	done
	nop

copy_fwd:
	bte	0, r22, copy_fwd_line
	shr	2, r8, r8
copy_fwd_quads:
	adds	-16, r7, r11
	adds	-16, r13, r14
	or	r8, r0, r12
copy_fwd_quad:
	fld.q	16(r11)++, f8
	fst.q	f8, 16(r14)++
	adds	-1, r12, r12
	btne	0, r12, copy_fwd_quad
	addu	r19, r7, r7
	addu	r19, r13, r13
	adds	-1, r9, r9
	btne	0, r9, copy_fwd_quads
	br	done
	nop
copy_fwd_line:
	or	r7, r0, r11
	or	r13, r0, r14
	or	r8, r0, r12
copy_fwd_pixel:
	ld.l	0(r11), r4
	st.l	r4, 0(r14)
	addu	4, r11, r11
	addu	4, r14, r14
	adds	-1, r12, r12
	btne	0, r12, copy_fwd_pixel
	addu	r19, r7, r7
	addu	r19, r13, r13
	adds	-1, r9, r9
	btne	0, r9, copy_fwd_line
	br	done
	nop

//	IMAGE dst w h fg bg stride bits...
image:
	ld.l	4(r21), r7		// r7: the line
	ld.l	8(r21), r8		// r8: width
	ld.l	12(r21), r9		// r9: lines left
	ld.l	16(r21), r10		// r10: fg
	ld.l	20(r21), r22		// r22: bg
	ld.l	24(r21), r24		// r24: stride
	addu	r7, r18, r7
	addu	28, r21, r23		// r23: the bits of the line
	shr	2, r24, r29		// r29: words of a line
	or	7, r0, r6
image_line:
	addu	r29, r6, r6
	or	r7, r0, r11
	or	r8, r0, r12		// r12: pixels left
	or	r23, r0, r25		// r25: next byte
image_byte:
	ld.b	0(r25), r26
	and	0xff, r26, r26
	addu	1, r25, r25
	or	0x80, r0, r27		// r27: the bit
image_pixel:
	or	r22, r0, r28
	and	r27, r26, r0		// CC: bit clear
	bc	image_store
	or	r10, r0, r28
image_store:
	st.l	r28, 0(r11)
	addu	4, r11, r11
	adds	-1, r12, r12
	bte	0, r12, image_next
	shr	1, r27, r27
	btne	0, r27, image_pixel
	br	image_byte
	nop
image_next:
	addu	r24, r23, r23
	addu	r19, r7, r7
	adds	-1, r9, r9
	btne	0, r9, image_line
	br	done
	nop
