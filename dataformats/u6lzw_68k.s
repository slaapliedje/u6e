| SPDX-License-Identifier: GPL-2.0-or-later
| u6lzw_68k.s — the Ultima VI LZW loop in 68000 assembly (docs/formats/lzw.md).
| The sanctioned asm hot path for decompression: the first-boot converter decodes
| megabytes of LZW, its largest single cost on an 8 MHz ST, and the game decodes
| conversations and portraits as it plays. 68000 instructions only (no 32-bit
| multiply or divide, no bitfields), so it runs on the whole line.
|
| int32_t u6lzw_run_68k(const uint8_t *in, int32_t inlen, uint8_t *dst, int32_t want);
|   Decode the bit stream in[0..inlen) into dst until `want` bytes are out or the
|   input runs dry. Returns the bytes written, or -1 for a corrupt stream. Gives
|   the same result as the C loop u6lzw_run() in u6lzw.c (the host and reference
|   version) for every input; keep the two in line.
|
| Method: every dictionary string is a copy of bytes already in dst (entry `next`
| = the previous string plus the byte after it), so the dictionary keeps each
| entry's address in dst and its length, and a string is copied forward from
| there. Differences in the order of work from the C loop, all giving the same
| result:
|  - The new entry is added BEFORE the code is looked up, so the KwKwK case
|    (code == next) is an ordinary lookup: the entry just added is the previous
|    string + its own first byte, which the forward byte copy reads as soon as it
|    has written it. (A clear code adds a stale entry first; the clear drops it.)
|  - Code 257 is rejected by its dictionary length, which is kept 0.
|  - The first code after a clear and the dictionary-full state each have their
|    own copy of the loop, so the main loop tests neither.
|  - A code is read as 3 bytes at the current byte, shifted by the bit offset
|    (0..7): one input-end check per code; the last 2 bytes go the slow way.
|
| Registers:  a0 in   a1 next free dictionary entry   a2 previous string's address
|             a3 out   a4 out end   a5 dictionary   a6 copy source
|             d0 code   d1 scratch   d2 in end - 3   d3 bit offset in (a0), 0..7
|             d4 width   d5 mask = (1 << width) - 1   d6 next code
|             d7 previous string's length (word)
| Dictionary entry (8 bytes): address in dst (long), length (low word of a long).

| one code -> d0.w, advancing a0/d3 (exits through `done` when the input is short)
.macro	READ
	cmpa.l	%d2,%a0
	bls.s	1f
	bsr	readslow
	bra.s	2f
1:	moveq	#0,%d0
	move.b	2(%a0),%d0
	swap	%d0
	move.b	1(%a0),-(%sp)		| a byte push moves sp by 2 and lands in the high byte
	move.w	(%sp)+,%d0
	move.b	(%a0),%d0		| d0 = the 3 bytes, little-endian
	lsr.l	%d3,%d0
	and.w	%d5,%d0
	ADVANCE
2:
.endm

| move past the code just read: a0 += (d3 + width) / 8, d3 = (d3 + width) % 8
.macro	ADVANCE
	add.w	%d4,%d3
	move.w	%d3,%d1
	lsr.w	#3,%d1
	adda.w	%d1,%a0
	and.w	#7,%d3
.endm

| copy the dictionary string for code d0 (>= 258, or 257) to out, clipped at out end
.macro	STRING
	lsl.w	#3,%d0
	move.l	(%a5,%d0.w),%a6		| copy from here
	move.w	6(%a5,%d0.w),%d7	| this many bytes
	jeq	bad			| code 257
	move.l	%a3,%a2
	moveq	#0,%d1
	move.w	%d7,%d1
	move.l	%a4,%d0
	sub.l	%a3,%d0			| room left
	cmp.l	%d0,%d1
	bls.s	1f
	move.l	%d0,%d1			| the last string runs past `want`: clip it
1:	subq.w	#1,%d1
2:	move.b	(%a6)+,(%a3)+		| forward, byte by byte
	dbra	%d1,2b
.endm

	.text
	.globl	_u6lzw_run_68k
_u6lzw_run_68k:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)	| 11 registers = 44 bytes
	move.l	48(%sp),%a0		| in
	move.l	52(%sp),%d2
	lea	-3(%a0,%d2.l),%a6
	move.l	%a6,%d2			| in end - 3
	move.l	56(%sp),%a3		| dst
	move.l	60(%sp),%d0
	lea	(%a3,%d0.l),%a4		| out end
	lea	dict,%a5
	clr.l	257*8+4(%a5)		| code 257 is never a string
	moveq	#0,%d3

clear:	moveq	#9,%d4			| (also the start state)
	move.l	#511,%d5
	move.w	#258,%d6
	lea	258*8(%a5),%a1
					| ---- the first code after a clear: a literal
	cmpa.l	%a4,%a3
	jcc	done
	READ
	cmp.w	#256,%d0
	beq.s	clear
	jhi	bad
	move.l	%a3,%a2
	move.b	%d0,(%a3)+
	moveq	#1,%d7

loop:	cmpa.l	%a4,%a3			| ---- the main loop
	jcc	done
	READ
	cmp.w	%d6,%d0
	jhi	bad			| code > next
	addq.w	#1,%d7			| entry `next` = previous string + 1 byte
	move.l	%a2,(%a1)+
	move.l	%d7,(%a1)+
	cmp.w	%d5,%d6
	beq.s	grow			| next becomes 1 << width
	addq.w	#1,%d6
dispatch:
	cmp.w	#256,%d0
	bcs.s	literal
	jeq	clear
	STRING
	jra	loop
literal:
	move.l	%a3,%a2
	move.b	%d0,(%a3)+
	moveq	#1,%d7
	jra	loop

grow:	addq.w	#1,%d6
	cmp.w	#12,%d4
	bcc.s	full			| 12 bits and next = 4096: the dictionary is full
	addq.w	#1,%d4
	add.w	%d5,%d5
	addq.w	#1,%d5
	bra.s	dispatch

full:	jra	fdispatch		| ---- dictionary full: no more entries, next stays 4096
floop:	cmpa.l	%a4,%a3			| (no code can exceed next now)
	jcc	done
	READ
fdispatch:
	cmp.w	#256,%d0
	bcs.s	fliteral
	jeq	clear
	STRING
	jra	floop
fliteral:
	move.b	%d0,(%a3)+
	jra	floop

done:	move.l	%a3,%d0
	sub.l	56(%sp),%d0		| bytes written
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
bad:	moveq	#-1,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| READ near the end of the input (fewer than 3 bytes left at a0): exactly the C
| loop's rule — a code needs offset + width bits in the bytes that are left.
readslow:
	move.l	%d2,%d1
	addq.l	#3,%d1
	sub.l	%a0,%d1			| bytes left (0..2)
	lsl.w	#3,%d1
	sub.w	%d3,%d1			| bits left
	cmp.w	%d4,%d1
	blt.s	1f
	moveq	#0,%d0			| 2 bytes left: the code ends inside them
	move.b	1(%a0),%d0
	lsl.w	#8,%d0
	move.b	(%a0),%d0
	lsr.l	%d3,%d0
	and.w	%d5,%d0
	ADVANCE
	rts
1:	addq.l	#4,%sp			| not a whole code left: stop
	jra	done

	.bss
	.even
dict:	.space	4096*8
