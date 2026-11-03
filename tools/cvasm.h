// SPDX-License-Identifier: GPL-2.0-or-later
/* cvasm.h — the converter's byte-scanning helpers: 68000 assembly on the Atari (tools/cv_68k.s), plain
 * C on the host, and the same results from both. A 68000 pays for these loops per pixel or per colour
 * (docs/design/first_boot_convert.md). -DCV_NO_ASM selects the C on the Atari too, to compare them.
 */
#ifndef U6E_CVASM_H
#define U6E_CVASM_H

#include <stdint.h>

#if defined(__m68k__) && !defined(CV_NO_ASM)
#define CV_ASM 1
void cv_hist(const uint8_t *p, long n, long *h);    /* h[p[i]]++ for each of the n bytes */
long cv_run(const uint8_t *p, long n, int v);       /* how many of the first n bytes (at most 65536) are v */
/* the nearest of the n <= 16 candidates (cand: g,r,b bytes each) to each of the 256 colours of pal8
 * (r,g,b bytes each), by the tables of d*d*6 (g), d*d*3 (r), d*d (b), d = -255..255. The lowest index wins a tie. */
void q_lut_68k(const uint8_t *pal8, const uint8_t *cand, int n, uint8_t *lut,
               const long *sq6, const long *sq3, const long *sq1);
/* CCol (quant.h) is r,g,b bytes then a long at +4, 8 bytes: axis is also the key's byte offset.
 * q_sort_68k: the same stable order as q_sort_axis, n <= 1024, keys in mn..mx, tmp holds n records.
 * q_box_scan_68k: n >= 1; mm = lowest and highest of r, of g, of b; returns the total weight. */
void q_sort_68k(void *col, int n, int axis, int mn, int mx, void *tmp);
long q_box_scan_68k(const void *col, int n, uint8_t mm[6]);
#else
static inline void cv_hist(const uint8_t *p, long n, long *h)
{
    for (long i = 0; i < n; i++) h[p[i]]++;
}
static inline long cv_run(const uint8_t *p, long n, int v)
{
    long r = 0;
    while (r < n && p[r] == (uint8_t)v) r++;
    return r;
}
#endif

#endif /* U6E_CVASM_H */
