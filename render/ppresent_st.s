| SPDX-License-Identifier: GPL-2.0-or-later
| ppresent_st.s — hand-written +8px-shifted planar present for ST-low (4 INTERLEAVED
| bitplanes). The ST sibling of c2p_st.s; the sanctioned asm hot path for the native-
| planar present (docs/design/performance.md). 68000-class instructions only.
|
| void ppresent_st_asm(const uint16_t *src, uint8_t *dst,
|                      long src_stride_b, long dst_stride_b, long nrows);
|   For each of nrows rows, copy 10 source GROUPS (4 interleaved plane words each) from
|   src to dst, shifted RIGHT by 8 px (== 1 byte). This spreads the 10 src groups across
|   11 dst groups (the +8px MAP_INSET). Per plane:
|     dst group g (1..9) = (src[g-1] << 8) | (src[g] >> 8)      (byte merge: prev_lo:cur_hi)
|     dst group 0        = (dst[0] & 0xFF00) | (src[0] >> 8)    (RMW: keep border px 0..7)
|     dst group 10       = (dst[10] & 0x00FF) | (src[9] << 8)   (RMW: keep border px 168..)
|   BYTE-IDENTICAL to the C reference ppresent_cpu() in rdr_st.c (rdr_ppresent_selftest).
|
| Registers: d0-d3 = the previous group's 4 plane words (the byte-merge carry, one per
| plane), d4/d5 = merge temps, d6 = group counter, d7 = row (dbra) counter. a0/a1 walk
| the current src/dst row; a2/a3 hold the row bases and advance by the strides.

	.text
	.globl	_ppresent_st_asm
_ppresent_st_asm:
	movem.l	%d2-%d7/%a2-%a4,-(%sp)	| save callee-saved (9 regs = 36 bytes)
	move.l	40(%sp),%a2		| a2 = src base
	move.l	44(%sp),%a3		| a3 = dst base
	move.l	56(%sp),%d7		| d7 = nrows
	subq.l	#1,%d7			| dbra runs nrows..0 inclusive
	tst.l	%d7
	bmi	9f			| nrows <= 0: nothing (word branch: 9f is past the body)
rowloop:
	move.l	%a2,%a0			| a0 = this src row
	move.l	%a3,%a1			| a1 = this dst row
	| ---- group 0: RMW keep high byte (border px 0..7); load prev planes d0-d3 ----
	move.w	(%a0)+,%d0		| prev0 = src[0]
	move.w	%d0,%d4
	lsr.w	#8,%d4			| src[0] >> 8
	move.w	(%a1),%d5
	and.w	#0xFF00,%d5		| keep border high byte
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	(%a0)+,%d1		| prev1 = src[1]
	move.w	%d1,%d4
	lsr.w	#8,%d4
	move.w	(%a1),%d5
	and.w	#0xFF00,%d5
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	(%a0)+,%d2		| prev2 = src[2]
	move.w	%d2,%d4
	lsr.w	#8,%d4
	move.w	(%a1),%d5
	and.w	#0xFF00,%d5
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	(%a0)+,%d3		| prev3 = src[3]
	move.w	%d3,%d4
	lsr.w	#8,%d4
	move.w	(%a1),%d5
	and.w	#0xFF00,%d5
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	| ---- groups 1..9: dst = (prev << 8) | (cur >> 8) per plane; prev := cur ----
	moveq	#9-1,%d6		| 9 groups, dbra
grploop:
	move.w	(%a0)+,%d4		| cur0
	move.w	%d0,%d5
	lsl.w	#8,%d5			| prev0 << 8
	move.w	%d4,%d0			| prev0 := cur0
	lsr.w	#8,%d4			| cur0 >> 8
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	(%a0)+,%d4		| cur1
	move.w	%d1,%d5
	lsl.w	#8,%d5
	move.w	%d4,%d1
	lsr.w	#8,%d4
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	(%a0)+,%d4		| cur2
	move.w	%d2,%d5
	lsl.w	#8,%d5
	move.w	%d4,%d2
	lsr.w	#8,%d4
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	(%a0)+,%d4		| cur3
	move.w	%d3,%d5
	lsl.w	#8,%d5
	move.w	%d4,%d3
	lsr.w	#8,%d4
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	dbra	%d6,grploop
	| ---- group 10: RMW keep low byte (border px 168..); prev holds src group 9 ----
	move.w	%d0,%d4
	lsl.w	#8,%d4			| prev0 << 8
	move.w	(%a1),%d5
	and.w	#0x00FF,%d5		| keep border low byte
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	%d1,%d4
	lsl.w	#8,%d4
	move.w	(%a1),%d5
	and.w	#0x00FF,%d5
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	%d2,%d4
	lsl.w	#8,%d4
	move.w	(%a1),%d5
	and.w	#0x00FF,%d5
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	move.w	%d3,%d4
	lsl.w	#8,%d4
	move.w	(%a1),%d5
	and.w	#0x00FF,%d5
	or.w	%d4,%d5
	move.w	%d5,(%a1)+
	| ---- next row: advance the row bases by their strides ----
	adda.l	48(%sp),%a2		| src base += src_stride_b
	adda.l	52(%sp),%a3		| dst base += dst_stride_b
	dbra	%d7,rowloop		| NOTE: nrows <= 65536 (160 here) so a word dbra is exact
9:	movem.l	(%sp)+,%d2-%d7/%a2-%a4
	rts
