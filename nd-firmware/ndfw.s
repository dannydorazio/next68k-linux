//	i860 firmware for Linux's NeXTdimension driver (nextdimension.c)
//	and the X driver (xorg-nextdimension/)
//
//	The board's ROM branches here, to the start of its DRAM, when the
//	host interrupts it after loading this.  It runs without paging, on
//	the board's physical addresses.  It takes drawing commands from a
//	ring in DRAM that the host fills, and never returns.
//
//	Shared area, at DRAM offset 0x8000:
//	  +0  READY	this writes NDFW_MAGIC once it's running
//	  +4  HEAD	the host: where it will put the next command
//	  +8  TAIL	this: where it will take the next command
//	  +12 EPSR	this: its extended processor status register, which
//			says which i860 it is (written before READY)
//	HEAD and TAIL count words from the start of the ring, which is
//	RING_WORDS long, at DRAM offset 0x10000.  The ring is empty when
//	they're equal.  A command doesn't wrap: WRAP sends this back to the
//	start.  Widths are in pixels, heights in lines, and neither is 0.
//
//	The console's commands take byte offsets into the video memory,
//	whose lines are PITCH bytes long:
//	FILL  dst w h color			5 words
//	COPY  src dst w h			5 words
//	IMAGE dst w h fg bg stride bits...	7 words, then h rows of
//		stride bytes (a multiple of 4): one bit per pixel, most
//		significant first, set for fg
//	WRAP					1 word
//
//	X's take the i860's addresses, in DRAM or the video memory, and the
//	length in bytes of each line (pitch), a multiple of 4:
//	FILLP    dst pitch w h color		6 words
//	COPYP    src spitch dst dpitch w h	7 words: backwards if dst is
//		above src, so that the same surface may overlap
//	PRESENT  src spitch dst dpitch w h	7 words: copies X's pixels,
//		0xXXRRGGBB, as the video memory's, 0xRRGGBBXX
//	FILL8    dst pitch w h value		6 words: bytes
//	ADD8     src spitch dst dpitch w h	7 words: bytes, dst + src,
//		at most 255 (Render's Add, for glyph masks)
//	OVERMASK color mask mpitch dst dpitch w h	8 words: the
//		premultiplied 0xAARRGGBB color over 32-bit dst through the
//		bytes of mask (Render's Over, for text)
//
//	Render's arithmetic looks a*b/255 up in a table this builds at TAB
//	before it's READY: rounding down, so that a sum never passes 255.
//
//	Registers held throughout: r16 the shared area, r17 the ring, r18
//	the video memory, r19 PITCH, r20 TAIL.  The drawing code takes the
//	destination's pitch in r25 and the source's in r26.

	SHARED	= 0xF8008000
	READY	= 0
	HEAD	= 4
	TAIL	= 8
	EPSR	= 12
	RING	= 0xF8010000
	TAB	= 0xF8020000		// 256 x 256 bytes
	VRAM	= 0xFE000000
	PITCH	= 4608
	NDFW_MAGIC = 0x4E44464C		// "NDFL"

	OP_FILL	 = 1
	OP_COPY	 = 2
	OP_IMAGE = 3
	OP_WRAP	 = 4
	OP_FILLP = 5
	OP_COPYP = 6
	OP_PRESENT = 7
	OP_FILL8 = 8
	OP_ADD8	 = 9
	OP_OVERMASK = 10

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
	orh	h%TAB, r0, r23		// TAB[a * 256 + b] = a * b / 255
	or	l%TAB, r23, r23
	or	256, r0, r9
	or	r0, r0, r4		// r4: a
tab_a:
	or	r0, r0, r5		// r5: a * b
	or	r0, r0, r6		// r6: b
tab_b:
	shr	8, r5, r7		// (x + 1 + x / 256) / 256 = x / 255
	addu	r5, r7, r7
	addu	1, r7, r7
	shr	8, r7, r7
	st.b	r7, 0(r23)
	addu	1, r23, r23
	addu	r4, r5, r5
	addu	1, r6, r6
	btne	r9, r6, tab_b
	addu	1, r4, r4
	btne	r9, r4, tab_a
	ld.c	epsr, r4
	st.l	r4, EPSR(r16)
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
	bte	OP_FILLP, r5, fillp
	bte	OP_COPYP, r5, copyp
	bte	OP_PRESENT, r5, present
	bte	OP_FILL8, r5, fill8
	bte	OP_ADD8, r5, add8
	bte	OP_OVERMASK, r5, overmask
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
	or	r19, r0, r25
	br	fill_any
	or	5, r0, r6

//	FILLP dst pitch w h color
fillp:
	ld.l	4(r21), r7
	ld.l	8(r21), r25
	ld.l	12(r21), r8
	ld.l	16(r21), r9
	ld.l	20(r21), r10
	or	6, r0, r6
fill_any:
	or	r7, r0, r4		// whole quads?  start, width, pitch
	and	15, r4, r0
	bnc	fill_line
	and	3, r8, r0
	bnc	fill_line
	and	15, r25, r0
	bnc	fill_line
	ixfr	r10, f8			// f8-f11: four pixels
	ixfr	r10, f9
	ixfr	r10, f10
	ixfr	r10, f11
	shr	2, r8, r8		// r8: quads
fill_quads:				// singles first, then fours
	adds	-16, r7, r11
	and	3, r8, r13
	shr	2, r8, r12
	bte	0, r13, fill_fours
fill_quad:
	fst.q	f8, 16(r11)++
	adds	-1, r13, r13
	btne	0, r13, fill_quad
fill_fours:
	bte	0, r12, fill_quads_next
fill_four:
	fst.q	f8, 16(r11)++
	fst.q	f8, 16(r11)++
	fst.q	f8, 16(r11)++
	fst.q	f8, 16(r11)++
	adds	-1, r12, r12
	btne	0, r12, fill_four
fill_quads_next:
	addu	r25, r7, r7
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
	addu	r25, r7, r7
	adds	-1, r9, r9
	btne	0, r9, fill_line
	br	done
	nop

//	COPY src dst w h
copy:
	ld.l	4(r21), r7		// r7: source line
	ld.l	8(r21), r13		// r13: destination line
	ld.l	12(r21), r8		// r8: width
	ld.l	16(r21), r9		// r9: lines left
	addu	r7, r18, r7
	addu	r13, r18, r13
	or	r19, r0, r25
	or	r19, r0, r26
	br	copy_any
	or	5, r0, r6

//	COPYP src spitch dst dpitch w h
copyp:
	ld.l	4(r21), r7
	ld.l	8(r21), r26
	ld.l	12(r21), r13
	ld.l	16(r21), r25
	ld.l	20(r21), r8
	ld.l	24(r21), r9
	or	7, r0, r6

//	Forwards if dst is below src, else backwards from the last pixel, so
//	that overlapping areas come out right.
copy_any:
	or	r0, r0, r22		// r22: 1 for whole quads
	and	3, r8, r0
	bnc	copy_dir
	and	15, r7, r0
	bnc	copy_dir
	and	15, r13, r0
	bnc	copy_dir
	and	15, r25, r0
	bnc	copy_dir
	and	15, r26, r0
	bnc	copy_dir
	or	1, r0, r22
copy_dir:
	subu	r7, r13, r0		// CC: dst <= src
	bc	copy_fwd

	// backwards: from the end of the last line, upwards
	adds	-1, r9, r4
copy_last:
	bte	0, r4, copy_bwd
	addu	r26, r7, r7
	addu	r25, r13, r13
	adds	-1, r4, r4
	br	copy_last
	nop
copy_bwd:
	shl	2, r8, r4		// r7, r13: ends of the lines
	addu	r4, r7, r7
	addu	r4, r13, r13
	bte	0, r22, copy_bwd_line
	shr	2, r8, r8
copy_bwd_quads:				// singles first, then fours
	or	r7, r0, r11
	or	r13, r0, r14
	and	3, r8, r15
	shr	2, r8, r12
	bte	0, r15, copy_bwd_fours
copy_bwd_quad:
	fld.q	-16(r11)++, f8
	fst.q	f8, -16(r14)++
	adds	-1, r15, r15
	btne	0, r15, copy_bwd_quad
copy_bwd_fours:
	bte	0, r12, copy_bwd_quads_next
copy_bwd_four:				// four loads before the stores
	fld.q	-16(r11)++, f8
	fld.q	-16(r11)++, f12
	fld.q	-16(r11)++, f16
	fld.q	-16(r11)++, f20
	fst.q	f8, -16(r14)++
	fst.q	f12, -16(r14)++
	fst.q	f16, -16(r14)++
	fst.q	f20, -16(r14)++
	adds	-1, r12, r12
	btne	0, r12, copy_bwd_four
copy_bwd_quads_next:
	subu	r7, r26, r7
	subu	r13, r25, r13
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
	subu	r7, r26, r7
	subu	r13, r25, r13
	adds	-1, r9, r9
	btne	0, r9, copy_bwd_line
	br	done
	nop

copy_fwd:
	bte	0, r22, copy_fwd_line
	shr	2, r8, r8
copy_fwd_quads:				// singles first, then fours
	adds	-16, r7, r11
	adds	-16, r13, r14
	and	3, r8, r15
	shr	2, r8, r12
	bte	0, r15, copy_fwd_fours
copy_fwd_quad:
	fld.q	16(r11)++, f8
	fst.q	f8, 16(r14)++
	adds	-1, r15, r15
	btne	0, r15, copy_fwd_quad
copy_fwd_fours:
	bte	0, r12, copy_fwd_quads_next
copy_fwd_four:				// four loads before the stores
	fld.q	16(r11)++, f8
	fld.q	16(r11)++, f12
	fld.q	16(r11)++, f16
	fld.q	16(r11)++, f20
	fst.q	f8, 16(r14)++
	fst.q	f12, 16(r14)++
	fst.q	f16, 16(r14)++
	fst.q	f20, 16(r14)++
	adds	-1, r12, r12
	btne	0, r12, copy_fwd_four
copy_fwd_quads_next:
	addu	r26, r7, r7
	addu	r25, r13, r13
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
	addu	r26, r7, r7
	addu	r25, r13, r13
	adds	-1, r9, r9
	btne	0, r9, copy_fwd_line
	br	done
	nop

//	PRESENT src spitch dst dpitch w h: each pixel shifted left a byte
present:
	ld.l	4(r21), r7		// r7: source line
	ld.l	8(r21), r26
	ld.l	12(r21), r13		// r13: destination line
	ld.l	16(r21), r25
	ld.l	20(r21), r8		// r8: width
	ld.l	24(r21), r9		// r9: lines left
	or	7, r0, r6
present_line:				// singles first, then fours
	or	r7, r0, r11
	or	r13, r0, r14
	and	3, r8, r15
	shr	2, r8, r12
	bte	0, r15, present_fours
present_pixel:
	ld.l	0(r11), r4
	shl	8, r4, r4
	st.l	r4, 0(r14)
	addu	4, r11, r11
	addu	4, r14, r14
	adds	-1, r15, r15
	btne	0, r15, present_pixel
present_fours:
	bte	0, r12, present_next
present_four:
	ld.l	0(r11), r4
	ld.l	4(r11), r5
	ld.l	8(r11), r10
	ld.l	12(r11), r22
	shl	8, r4, r4
	shl	8, r5, r5
	shl	8, r10, r10
	shl	8, r22, r22
	st.l	r4, 0(r14)
	st.l	r5, 4(r14)
	st.l	r10, 8(r14)
	st.l	r22, 12(r14)
	addu	16, r11, r11
	addu	16, r14, r14
	adds	-1, r12, r12
	btne	0, r12, present_four
present_next:
	addu	r26, r7, r7
	addu	r25, r13, r13
	adds	-1, r9, r9
	btne	0, r9, present_line
	br	done
	nop

//	FILL8 dst pitch w h value
fill8:
	ld.l	4(r21), r7		// r7: the line
	ld.l	8(r21), r25
	ld.l	12(r21), r8		// r8: width
	ld.l	16(r21), r9		// r9: lines left
	ld.l	20(r21), r10		// r10: value
	or	6, r0, r6
fill8_line:
	or	r7, r0, r11
	or	r8, r0, r12
fill8_byte:
	st.b	r10, 0(r11)
	addu	1, r11, r11
	adds	-1, r12, r12
	btne	0, r12, fill8_byte
	addu	r25, r7, r7
	adds	-1, r9, r9
	btne	0, r9, fill8_line
	br	done
	nop

//	ADD8 src spitch dst dpitch w h
add8:
	ld.l	4(r21), r7		// r7: source line
	ld.l	8(r21), r26
	ld.l	12(r21), r13		// r13: destination line
	ld.l	16(r21), r25
	ld.l	20(r21), r8		// r8: width
	ld.l	24(r21), r9		// r9: lines left
	or	7, r0, r6
add8_line:
	or	r7, r0, r11
	or	r13, r0, r14
	or	r8, r0, r12
add8_byte:
	ld.b	0(r11), r4
	and	0xff, r4, r4
	bte	0, r4, add8_next	// adding 0
	ld.b	0(r14), r5
	and	0xff, r5, r5
	addu	r4, r5, r5
	and	0x100, r5, r0		// CC: no more than 255
	bc	add8_store
	or	255, r0, r5
add8_store:
	st.b	r5, 0(r14)
add8_next:
	addu	1, r11, r11
	addu	1, r14, r14
	adds	-1, r12, r12
	btne	0, r12, add8_byte
	addu	r26, r7, r7
	addu	r25, r13, r13
	adds	-1, r9, r9
	btne	0, r9, add8_line
	br	done
	nop

//	OVERMASK color mask mpitch dst dpitch w h: each byte of dst becomes
//	color's * m / 255 + dst's * (255 - t) / 255, where m is the mask's
//	and t = alpha * m / 255.  The table's rows for color's bytes are in
//	r27-r30, alpha's first, as the bytes are in memory.
overmask:
	ld.l	4(r21), r10		// r10: color
	ld.l	8(r21), r7		// r7: mask line
	ld.l	12(r21), r26
	ld.l	16(r21), r13		// r13: destination line
	ld.l	20(r21), r25
	ld.l	24(r21), r8		// r8: width
	ld.l	28(r21), r9		// r9: lines left
	or	8, r0, r6
	orh	h%TAB, r0, r24		// r24: the table
	or	l%TAB, r24, r24
	shr	24, r10, r22		// r22: alpha
	shl	8, r22, r27
	addu	r24, r27, r27
	shr	16, r10, r28
	and	0xff, r28, r28
	shl	8, r28, r28
	addu	r24, r28, r28
	shr	8, r10, r29
	and	0xff, r29, r29
	shl	8, r29, r29
	addu	r24, r29, r29
	and	0xff, r10, r30
	shl	8, r30, r30
	addu	r24, r30, r30
	or	255, r0, r31
om_line:
	or	r7, r0, r11
	or	r13, r0, r14
	or	r8, r0, r12
om_pixel:
	ld.b	0(r11), r4		// r4: m
	and	0xff, r4, r4
	bte	0, r4, om_next
	btne	r31, r4, om_blend	// all of an opaque color: just it
	btne	r31, r22, om_blend
	st.l	r10, 0(r14)
	br	om_next
	nop
om_blend:
	addu	r4, r27, r5		// r5: t, alpha's own term
	ld.b	0(r5), r5
	and	0xff, r5, r5
	subu	r31, r5, r15		// r15: the row for 255 - t
	shl	8, r15, r15
	addu	r24, r15, r15
	ld.b	0(r14), r23
	and	0xff, r23, r23
	addu	r23, r15, r23
	ld.b	0(r23), r23
	and	0xff, r23, r23
	addu	r5, r23, r23
	st.b	r23, 0(r14)
	addu	r4, r28, r5
	ld.b	0(r5), r5
	and	0xff, r5, r5
	ld.b	1(r14), r23
	and	0xff, r23, r23
	addu	r23, r15, r23
	ld.b	0(r23), r23
	and	0xff, r23, r23
	addu	r5, r23, r23
	st.b	r23, 1(r14)
	addu	r4, r29, r5
	ld.b	0(r5), r5
	and	0xff, r5, r5
	ld.b	2(r14), r23
	and	0xff, r23, r23
	addu	r23, r15, r23
	ld.b	0(r23), r23
	and	0xff, r23, r23
	addu	r5, r23, r23
	st.b	r23, 2(r14)
	addu	r4, r30, r5
	ld.b	0(r5), r5
	and	0xff, r5, r5
	ld.b	3(r14), r23
	and	0xff, r23, r23
	addu	r23, r15, r23
	ld.b	0(r23), r23
	and	0xff, r23, r23
	addu	r5, r23, r23
	st.b	r23, 3(r14)
om_next:
	addu	1, r11, r11
	addu	4, r14, r14
	adds	-1, r12, r12
	btne	0, r12, om_pixel
	addu	r26, r7, r7
	addu	r25, r13, r13
	adds	-1, r9, r9
	btne	0, r9, om_line
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
