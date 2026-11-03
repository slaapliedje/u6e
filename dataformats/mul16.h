// SPDX-License-Identifier: GPL-2.0-or-later
/* mul16.h — 16-bit multiplies and divides that stay one instruction on the 68000.
 *
 * The 68000 multiplies only 16x16 bits (mulu.w / muls.w) and divides only 32/16 bits (divu.w); a
 * 32-bit `*` or `/` is a call into libgcc (__mulsi3, __udivsi3), several times slower. GCC emits
 * the short forms for 16-bit operands, but where it can see that the values are small it may drop
 * the casts and fall back to the library call. These helpers pin the instruction. Elsewhere (host
 * tools) they are plain C.
 */
#ifndef U6E_MUL16_H
#define U6E_MUL16_H

#include <stdint.h>

static inline uint32_t mulu16(uint16_t a, uint16_t b)
{
#if defined(__m68k__)
    uint32_t r = a;
    __asm__("mulu.w %1,%0" : "+d"(r) : "dmi"(b));
    return r;
#else
    return (uint32_t)a * b;
#endif
}

static inline int32_t muls16(int16_t a, int16_t b)
{
#if defined(__m68k__)
    int32_t r = a;
    __asm__("muls.w %1,%0" : "+d"(r) : "dmi"(b));
    return r;
#else
    return (int32_t)a * b;
#endif
}

/* n / d and n % d for a quotient that fits 16 bits (the caller guarantees n / d < 65536) */
static inline uint16_t divu16(uint32_t n, uint16_t d, uint16_t *rem)
{
#if defined(__m68k__)
    uint32_t r = n;
    __asm__("divu.w %1,%0" : "+d"(r) : "dmi"(d));
    *rem = (uint16_t)(r >> 16);
    return (uint16_t)r;
#else
    *rem = (uint16_t)(n % d);
    return (uint16_t)(n / d);
#endif
}

/* a * b for a 16-bit a and a 64-bit b, from four 16x16 multiplies (a 64-bit `*` is __muldi3) */
static inline uint64_t mulu16x64(uint16_t a, uint64_t b)
{
    return  (uint64_t)mulu16(a, (uint16_t)b)
         + ((uint64_t)mulu16(a, (uint16_t)(b >> 16)) << 16)
         + ((uint64_t)mulu16(a, (uint16_t)(b >> 32)) << 32)
         + ((uint64_t)mulu16(a, (uint16_t)(b >> 48)) << 48);
}

#endif /* U6E_MUL16_H */
