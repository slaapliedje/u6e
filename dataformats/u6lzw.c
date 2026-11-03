// SPDX-License-Identifier: GPL-2.0-or-later
/* u6lzw.c — Ultima VI LZW decoder. See /docs/formats/lzw.md.
 *
 * Variable-width LZW (9..12 bits), codes packed LSB-first. Code 256 = clear
 * (reset dictionary + width). No end-of-information code: decoding stops when
 * the declared uncompressed size is reached. Container = 4-byte LE size header
 * then the bit stream; size 0 => the rest is stored verbatim.
 */
#include "u6lzw.h"

/* Verified against real U6 files (MASKTYPE/MAPTILES/ANIMMASK.VGA, all decode to
 * their declared sizes): GIF-style variant — 256=clear, 257=end-of-info
 * (reserved, unused here since size is known), first string code = 258, and the
 * code width grows when next == (1<<width) (NO "early change"). */
#define LZW_CLEAR     256
#define LZW_FIRST     258    /* first dictionary code assigned after a clear */
#define LZW_MAXBITS   12
#define LZW_MAXCODES  (1 << LZW_MAXBITS)

int32_t u6lzw_unpacked_size(const uint8_t *src, int32_t srclen)
{
    if (!src || srclen < 4) return -1;
    return (int32_t)((uint32_t)src[0]        | ((uint32_t)src[1] << 8) |
                     ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24));
}

/* On the Atari the loop is hand-written 68000 (u6lzw_68k.s, linked by every Atari target);
 * -DU6LZW_NO_ASM builds the C loop there instead, to compare the two. */
#if defined(__m68k__) && !defined(U6LZW_NO_ASM)
#define U6LZW_ASM 1
int32_t u6lzw_run_68k(const uint8_t *in, int32_t inlen, uint8_t *dst, int32_t want);
#else
static int32_t u6lzw_run(const uint8_t *in, int32_t inlen, uint8_t *dst, int32_t want);
#endif

int32_t u6lzw_decompress(const uint8_t *src, int32_t srclen,
                         uint8_t *dst, int32_t dstcap)
{
    int32_t want = u6lzw_unpacked_size(src, srclen);
    if (want < 0) return -1;

    const uint8_t *in    = src + 4;
    int32_t        inlen = srclen - 4;

    /* Stored: body is the literal data. */
    if (want == 0) {
        if (inlen > dstcap) return -1;
        for (int32_t i = 0; i < inlen; i++) dst[i] = in[i];
        return inlen;
    }
    if (want > dstcap) return -1;

#ifdef U6LZW_ASM
    return u6lzw_run_68k(in, inlen, dst, want);
#else
    return u6lzw_run(in, inlen, dst, want);
#endif
}

/* The LZW loop proper. Every dictionary string is a copy of bytes already decoded (entry `next`
 * is the previous string plus the first byte after it, which is where it sits in dst), so the
 * dictionary holds each string's position in dst and its length, and a string is copied forward
 * from there: no prefix chains, no stack. The KwKwK case (code == next) copies the previous string
 * plus one more byte, whose source is the first byte of that same copy. u6lzw_68k.s follows this
 * function step for step; keep the two in line. */
#ifndef U6LZW_ASM
static int32_t u6lzw_run(const uint8_t *in, int32_t inlen, uint8_t *dst, int32_t want)
{
    static int32_t  start[LZW_MAXCODES];
    static uint16_t length[LZW_MAXCODES];

    int32_t  inpos   = 0;
    uint32_t bitbuf  = 0;
    int      bitcnt  = 0;
    int      width   = 9;
    uint32_t mask    = (1u << 9) - 1u;
    int      next    = LZW_FIRST;
    int      have    = 0;               /* a previous string since the last clear */
    int32_t  prevpos = 0, prevlen = 0;  /* where it was written and its length */
    int32_t  outpos  = 0;

    while (outpos < want) {
        /* read one `width`-bit code, LSB-first */
        while (bitcnt < width) {
            if (inpos >= inlen) return outpos;        /* input exhausted */
            bitbuf |= (uint32_t)in[inpos++] << bitcnt;
            bitcnt += 8;
        }
        int code = (int)(bitbuf & mask);
        bitbuf >>= width;
        bitcnt  -= width;

        if (code == LZW_CLEAR) {
            width = 9; mask = (1u << 9) - 1u; next = LZW_FIRST; have = 0;
            continue;
        }

        int32_t len;
        if (!have) {                    /* first symbol after a clear: a literal */
            if (code >= 256) return -1; /* malformed */
            dst[outpos] = (uint8_t)code;
            len = 1;
            have = 1;
        } else {
            if (code > next || code == 257) return -1;  /* corrupt */
            if (code < 256) {
                dst[outpos] = (uint8_t)code;
                len = 1;
            } else {
                int32_t from = (code == next) ? prevpos : start[code];
                len = (code == next) ? prevlen + 1 : length[code];
                int32_t n = len <= want - outpos ? len : want - outpos;  /* clip the last string */
                for (int32_t i = 0; i < n; i++) dst[outpos + i] = dst[from + i];
            }
            /* add the previous string + this string's first byte to the dictionary */
            if (next < LZW_MAXCODES) {
                start[next]  = prevpos;
                length[next] = (uint16_t)(prevlen + 1);
                next++;
                if (next == (1 << width) && width < LZW_MAXBITS) { width++; mask = (1u << width) - 1u; }
            }
        }
        prevpos = outpos; prevlen = len;
        outpos += len <= want - outpos ? len : want - outpos;
    }
    return outpos;
}
#endif
