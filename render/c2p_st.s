| SPDX-License-Identifier: GPL-2.0-or-later
| c2p_st.s — hand-written chunky->planar for ST-low (4 INTERLEAVED bitplanes),
| with a 256->16 colour LUT applied per pixel on the way. The ST sibling of the
| TT's c2p_span_asm (render/c2p_asm.s); the sanctioned asm hot path
| (docs/design/performance.md). 68000-class instructions only.
|
| void c2p4_lut_asm(const uint8_t *src, const uint8_t *lut, uint16_t *dst, long ngroups);
|   Convert `ngroups` consecutive 16-pixel groups (src, chunky 8bpp) to plane
|   words (dst, 4 x u16 per group, contiguous), remapping each index through the
|   256-entry `lut` (8bpp -> 4bpp) first. Pixel 0 -> bit 15 of each plane word —
|   BYTE-IDENTICAL to the C reference c2p4_lut() in rdr_st.c.
|
| Method (same carry chain as the TT path): per pixel, LUT the index to a nibble,
| then lsr.b #1 shifts the next plane bit into X and roxl.w #1 rotates it into that
| plane's 16-bit accumulator (so pixel 0 lands at bit 15). Only the low 4 bits of
| the LUT byte are consumed (4 planes), so any high bits are ignored exactly as the
| C's `& 0x0F` — no mask needed. Planes 0-3 accumulate in d1-d4.

	.text
	.globl	_c2p4_lut_asm
_c2p4_lut_asm:
	movem.l	%d2-%d4/%a2-%a4,-(%sp)	| save callee-saved (6 regs = 24 bytes)
	move.l	28(%sp),%a0		| a0 = src
	move.l	32(%sp),%a1		| a1 = lut
	move.l	36(%sp),%a2		| a2 = dst
	move.l	40(%sp),%d0		| d0 = ngroups
	lsl.l	#4,%d0			| ngroups * 16 (pixels)
	lea	(%a0,%d0.l),%a4		| a4 = src end (all groups)
	cmpa.l	%a4,%a0
	beq.s	9f			| ngroups == 0: nothing to do
	moveq	#0,%d0			| keep d0's high bits clear for the LUT index
0:					| ---- per group ----
	moveq	#0,%d1			| plane 0
	moveq	#0,%d2			| plane 1
	moveq	#0,%d3			| plane 2
	moveq	#0,%d4			| plane 3
	lea	16(%a0),%a3		| a3 = end of this 16-pixel group
1:	move.b	(%a0)+,%d0		| raw pixel (d0.w = 0..255, high bits already 0)
	move.b	(%a1,%d0.w),%d0		| LUT: low byte = 4bpp nibble
	lsr.b	#1,%d0
	roxl.w	#1,%d1			| plane 0 bit
	lsr.b	#1,%d0
	roxl.w	#1,%d2			| plane 1 bit
	lsr.b	#1,%d0
	roxl.w	#1,%d3			| plane 2 bit
	lsr.b	#1,%d0
	roxl.w	#1,%d4			| plane 3 bit  (low byte now 0 -> d0 stays clean)
	cmpa.l	%a3,%a0
	bne.s	1b
	move.w	%d1,(%a2)+		| dst[0] = plane 0
	move.w	%d2,(%a2)+		| dst[1] = plane 1
	move.w	%d3,(%a2)+		| dst[2] = plane 2
	move.w	%d4,(%a2)+		| dst[3] = plane 3
	cmpa.l	%a4,%a0
	bne.s	0b
9:	movem.l	(%sp)+,%d2-%d4/%a2-%a4
	rts
