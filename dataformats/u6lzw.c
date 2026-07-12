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

    /* Dictionary: codes 256.. hold (prefix code, suffix byte). */
    static uint16_t prefix[LZW_MAXCODES];
    static uint8_t  suffix[LZW_MAXCODES];
    uint8_t  stack[LZW_MAXCODES];

    int32_t  inpos  = 0;
    uint32_t bitbuf = 0;
    int      bitcnt = 0;
    int      width  = 9;
    int      next   = LZW_FIRST;
    int      prev   = -1;
    uint8_t  first  = 0;
    int32_t  outpos = 0;

    while (outpos < want) {
        /* read one `width`-bit code, LSB-first */
        while (bitcnt < width) {
            if (inpos >= inlen) return outpos;        /* input exhausted */
            bitbuf |= (uint32_t)in[inpos++] << bitcnt;
            bitcnt += 8;
        }
        int code = (int)(bitbuf & (uint32_t)((1u << width) - 1u));
        bitbuf >>= width;
        bitcnt  -= width;

        if (code == LZW_CLEAR) {
            width = 9; next = LZW_FIRST; prev = -1;
            continue;
        }

        int cur = code;
        int sp  = 0;

        if (prev < 0) {                 /* first symbol after a clear: a literal */
            if (code >= 256) return -1; /* malformed */
            first = (uint8_t)code;
            dst[outpos++] = first;
            prev = code;
            continue;
        }

        if (code > next) return -1;     /* corrupt: code beyond dictionary */
        if (code == next) {             /* KwKwK special case */
            stack[sp++] = first;
            cur = prev;
        }
        while (cur >= 256) {            /* unwind prefix chain (guard corruption) */
            if (sp >= LZW_MAXCODES) return -1;
            stack[sp++] = suffix[cur];
            cur = prefix[cur];
        }
        first = (uint8_t)cur;
        stack[sp++] = first;

        while (sp > 0 && outpos < want) dst[outpos++] = stack[--sp];

        /* add prev + first to the dictionary */
        if (next < LZW_MAXCODES) {
            prefix[next] = (uint16_t)prev;
            suffix[next] = first;
            next++;
            if (next == (1 << width) && width < LZW_MAXBITS) width++;
        }
        prev = code;
    }
    return outpos;
}
