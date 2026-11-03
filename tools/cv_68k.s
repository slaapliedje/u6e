| SPDX-License-Identifier: GPL-2.0-or-later
| cv_68k.s — hot loops of the first-boot converter in 68000 assembly: 68000 instructions only, so it
| runs on the whole line. The C forms are in tools/cvasm.h and quant.h (q_nearest); keep them in line.
| Calling convention: arguments on the stack as longs, d0-d1/a0-a1 scratch, result in d0.

	.text
	.even

| void cv_hist(const uint8_t *p, long n, long *h);
.macro	HIST1
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	addq.l	#1,0(%a1,%d0.w)
.endm
	.globl	_cv_hist
_cv_hist:
	move.l	%d2,-(%sp)
	move.l	8(%sp),%a0
	move.l	12(%sp),%d1
	move.l	16(%sp),%a1
	move.w	%d1,%d2
	and.w	#3,%d2
	subq.w	#1,%d2
	bmi.s	2f
1:	HIST1				| the odd bytes, then 4 at a time
	dbf	%d2,1b
2:	lsr.l	#2,%d1
	beq.s	9f
	subq.l	#1,%d1			| dbf counts the low word; the high word counts 64 K blocks
	move.l	%d1,%d2
	swap	%d2
3:	HIST1
	HIST1
	HIST1
	HIST1
	dbf	%d1,3b
	dbf	%d2,3b
9:	move.l	(%sp)+,%d2
	rts

| long cv_run(const uint8_t *p, long n, int v);   n <= 65536
	.globl	_cv_run
_cv_run:
	move.l	4(%sp),%a0
	move.l	8(%sp),%d1
	beq.s	8f
	move.l	12(%sp),%d0
	move.l	%a0,%a1
	subq.l	#1,%d1
1:	cmp.b	(%a0)+,%d0
	dbne	%d1,1b
	beq.s	2f
	subq.l	#1,%a0			| the byte that differed isn't in the run
2:	move.l	%a0,%d0
	sub.l	%a1,%d0
	rts
8:	moveq	#0,%d0
	rts

| void q_lut_68k(pal8, cand, n, lut, sq6, sq3, sq1);
| Each candidate's three table offsets are worked out once, as words on the stack. Green weighs most,
| so a candidate is dropped as soon as its partial sum reaches the best distance.
	.globl	_q_lut_68k
_q_lut_68k:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)		| 44 bytes
	move.l	48(%sp),%a5			| pal8
	move.l	52(%sp),%a0			| cand
	move.l	56(%sp),%d5			| n
	move.l	60(%sp),%a6			| lut
	move.l	%d5,%d0
	add.l	%d0,%d0
	add.l	%d5,%d0
	add.l	%d0,%d0				| n*6 bytes of offsets
	move.l	%sp,%a1
	sub.l	%d0,%sp
	move.l	%sp,%a2
	move.w	%d5,%d1
	subq.w	#1,%d1
	moveq	#0,%d0
1:	move.b	(%a0)+,%d0			| g, r, b -> byte value * 4
	lsl.w	#2,%d0
	move.w	%d0,(%a2)+
	clr.w	%d0
	move.b	(%a0)+,%d0
	lsl.w	#2,%d0
	move.w	%d0,(%a2)+
	clr.w	%d0
	move.b	(%a0)+,%d0
	lsl.w	#2,%d0
	move.w	%d0,(%a2)+
	clr.w	%d0
	dbf	%d1,1b
	move.l	%a1,%d4				| the frame, for the arguments below
	subq.w	#1,%d5				| n-1, the dbf start and the "best" base
	move.w	#255,%d7			| colours to go
2:	move.l	%d4,%a1
	moveq	#0,%d0				| table pointers: table + (255 - value) * 4
	move.b	(%a5)+,%d0			| r
	neg.w	%d0
	add.w	#255,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	move.l	68(%a1),%a3			| sq3
	add.w	%d0,%a3
	moveq	#0,%d0
	move.b	(%a5)+,%d0			| g
	neg.w	%d0
	add.w	#255,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	move.l	64(%a1),%a2			| sq6
	add.w	%d0,%a2
	moveq	#0,%d0
	move.b	(%a5)+,%d0			| b
	neg.w	%d0
	add.w	#255,%d0
	add.w	%d0,%d0
	add.w	%d0,%d0
	move.l	72(%a1),%a4			| sq1
	add.w	%d0,%a4
	move.l	%sp,%a0
	move.l	#1000000000,%d2			| best distance
	move.w	%d5,%d6				| best: counter value at the winner
	move.w	%d5,%d1
3:	move.w	(%a0),%d0
	move.l	0(%a2,%d0.w),%d3
	cmp.l	%d2,%d3
	bge.s	4f
	move.w	2(%a0),%d0
	add.l	0(%a3,%d0.w),%d3
	cmp.l	%d2,%d3
	bge.s	4f
	move.w	4(%a0),%d0
	add.l	0(%a4,%d0.w),%d3
	cmp.l	%d2,%d3
	bge.s	4f
	move.l	%d3,%d2
	move.w	%d1,%d6
4:	addq.l	#6,%a0
	dbf	%d1,3b
	move.w	%d5,%d0
	sub.w	%d6,%d0				| index = (n-1) - counter
	move.b	%d0,(%a6)+
	dbf	%d7,2b
	move.l	%d4,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| long q_box_scan_68k(const CCol *col, int n, uint8_t mm[6]);
	.globl	_q_box_scan_68k
_q_box_scan_68k:
	movem.l	%d2-%d7,-(%sp)			| 24 bytes
	move.l	28(%sp),%a0
	move.l	32(%sp),%d1
	subq.w	#1,%d1
	moveq	#0,%d0				| total weight
	moveq	#-1,%d2				| r low, high
	moveq	#0,%d3
	moveq	#-1,%d4				| g
	moveq	#0,%d5
	moveq	#-1,%d6				| b
	moveq	#0,%d7
.macro	MINMAX lo, hi
	move.b	(%a0)+,%d0
	cmp.b	\lo,%d0
	bcc.s	1f
	move.b	%d0,\lo
1:	cmp.b	\hi,%d0
	bls.s	2f
	move.b	%d0,\hi
2:
.endm
	move.l	%d0,%a1
3:	MINMAX	%d2,%d3
	MINMAX	%d4,%d5
	MINMAX	%d6,%d7
	addq.l	#1,%a0				| the pad byte
	adda.l	(%a0)+,%a1
	dbf	%d1,3b
	move.l	36(%sp),%a0
	move.b	%d2,(%a0)+
	move.b	%d3,(%a0)+
	move.b	%d4,(%a0)+
	move.b	%d5,(%a0)+
	move.b	%d6,(%a0)+
	move.b	%d7,(%a0)
	move.l	%a1,%d0
	movem.l	(%sp)+,%d2-%d7
	rts

| void q_sort_68k(CCol *col, int n, int axis, int mn, int mx, CCol *tmp);
| Up to 24 records by insertion, more by counting. Stable, as q_sort_axis.
	.globl	_q_sort_68k
_q_sort_68k:
	movem.l	%d2-%d7/%a2-%a4,-(%sp)		| 36 bytes
	move.l	40(%sp),%a0			| col
	move.l	44(%sp),%d7			| n
	move.l	48(%sp),%d6			| axis = key offset
	cmp.w	#24,%d7
	bgt.s	4f
	move.l	%a0,%a2				| ---- insertion
	subq.w	#2,%d7
	bmi.s	9f
	lea	8(%a0),%a0
1:	moveq	#0,%d2				| key of the next record
	move.b	0(%a0,%d6.w),%d2
	cmp.b	-8(%a0,%d6.w),%d2
	bcc.s	3f				| already in place
	move.l	(%a0),%d3
	move.l	4(%a0),%d4
	move.l	%a0,%a1
2:	move.l	-4(%a1),4(%a1)			| move the bigger key up
	move.l	-8(%a1),(%a1)
	subq.l	#8,%a1
	cmpa.l	%a2,%a1
	bls.s	21f
	cmp.b	-8(%a1,%d6.w),%d2
	bcs.s	2b
21:	move.l	%d3,(%a1)
	move.l	%d4,4(%a1)
3:	addq.l	#8,%a0
	dbf	%d7,1b
9:	movem.l	(%sp)+,%d2-%d7/%a2-%a4
	rts
4:	move.l	52(%sp),%d5			| ---- counting: mn
	move.l	56(%sp),%d4			| mx
	move.l	60(%sp),%a3			| tmp
	lea	-516(%sp),%sp			| 258 words, offset by a record's 8 bytes
	sub.w	%d5,%d4				| range - 1
	move.l	%sp,%a2
	move.w	%d4,%d1
	addq.w	#1,%d1				| range + 1 words
5:	clr.w	(%a2)+
	dbf	%d1,5b
	move.l	%sp,%a2
	move.l	%a0,%a1
	move.w	%d7,%d1
	subq.w	#1,%d1
6:	moveq	#0,%d0				| starts[key - mn + 1] += 8
	move.b	0(%a1,%d6.w),%d0
	sub.w	%d5,%d0
	add.w	%d0,%d0
	addq.w	#8,2(%a2,%d0.w)
	addq.l	#8,%a1
	dbf	%d1,6b
	move.w	%d4,%d1				| running sum: starts[v+1] += starts[v]
	moveq	#0,%d0
	lea	2(%a2),%a1
7:	add.w	(%a1),%d0
	move.w	%d0,(%a1)+
	dbf	%d1,7b
	move.l	%a0,%a1				| each record to its place, in order
	move.w	%d7,%d1
	subq.w	#1,%d1
8:	moveq	#0,%d0
	move.b	0(%a1,%d6.w),%d0
	sub.w	%d5,%d0
	add.w	%d0,%d0
	move.w	0(%a2,%d0.w),%d3
	addq.w	#8,0(%a2,%d0.w)
	move.l	(%a1)+,0(%a3,%d3.w)
	move.l	(%a1)+,4(%a3,%d3.w)
	dbf	%d1,8b
	move.w	%d7,%d1				| and back
	subq.w	#1,%d1
	move.l	%a0,%a1
10:	move.l	(%a3)+,(%a1)+
	move.l	(%a3)+,(%a1)+
	dbf	%d1,10b
	lea	516(%sp),%sp
	bra	9b
