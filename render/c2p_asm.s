| SPDX-License-Identifier: GPL-2.0-or-later
| c2p_asm.s — hand-written chunky->planar for TT-low (8 interleaved bitplanes).
| The sole sanctioned asm hot path (docs/design/performance.md): the present is
| compute-bound, and this is the inner conversion. 68000-class instructions only,
| so one routine serves both the 68000 and -m68020-60 builds.
|
| void c2p_span_asm(const uint8_t *src, uint16_t *dst, long ngroups);
|   Convert `ngroups` consecutive 16-pixel groups (src, chunky 8bpp) to plane words
|   (dst, 8 x u16 per group, contiguous). Pixel 0 -> bit 15 of each plane — BYTE-
|   IDENTICAL to the C c2p_group(). movem is amortised across the whole span (one
|   save/restore per call, not per group).
|
| Method: per group, a lsr.b/roxl.w carry chain. lsr.b #1 puts the next plane bit
| into X; roxl.w #1 rotates it into that plane's accumulator (pixel 0 ends at bit 15).
| Planes 0-6 live in d1-d7; the pixel in d0; plane 7 accumulates in dst[7] (memory) —
| 8 planes + pixel won't all fit in the 8 data registers.

	.text
	.globl	_c2p_span_asm
_c2p_span_asm:
	movem.l	%d2-%d7/%a2-%a4,-(%sp)	| save callee-saved (9 regs = 36 bytes)
	move.l	40(%sp),%a0		| a0 = src
	move.l	44(%sp),%a1		| a1 = dst
	move.l	48(%sp),%d0		| d0 = ngroups
	lsl.l	#4,%d0			| ngroups * 16 (pixels per group)
	lea	(%a0,%d0.l),%a4		| a4 = src end
	cmpa.l	%a4,%a0
	beq	9f			| nothing to do
0:					| ---- per group ----
	moveq	#0,%d1
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	moveq	#0,%d6
	moveq	#0,%d7
	lea	14(%a1),%a2		| a2 = &dst[7] (plane 7 accumulator)
	clr.w	(%a2)
	lea	16(%a0),%a3		| a3 = group end (16 pixels)
1:	move.b	(%a0)+,%d0		| next pixel
	lsr.b	#1,%d0
	roxl.w	#1,%d1			| plane 0
	lsr.b	#1,%d0
	roxl.w	#1,%d2			| plane 1
	lsr.b	#1,%d0
	roxl.w	#1,%d3			| plane 2
	lsr.b	#1,%d0
	roxl.w	#1,%d4			| plane 3
	lsr.b	#1,%d0
	roxl.w	#1,%d5			| plane 4
	lsr.b	#1,%d0
	roxl.w	#1,%d6			| plane 5
	lsr.b	#1,%d0
	roxl.w	#1,%d7			| plane 6
	lsr.b	#1,%d0
	roxl.w	(%a2)			| plane 7 (memory; count is implied 1 for mem rotates)
	cmpa.l	%a3,%a0
	bne	1b
	move.w	%d1,(%a1)		| dst[0]
	move.w	%d2,2(%a1)		| dst[1]
	move.w	%d3,4(%a1)		| dst[2]
	move.w	%d4,6(%a1)		| dst[3]
	move.w	%d5,8(%a1)		| dst[4]
	move.w	%d6,10(%a1)		| dst[5]
	move.w	%d7,12(%a1)		| dst[6]  (dst[7] already written in memory)
	lea	16(%a1),%a1		| next group's 8 words
	cmpa.l	%a4,%a0
	bne	0b
9:	movem.l	(%sp)+,%d2-%d7/%a2-%a4
	rts
