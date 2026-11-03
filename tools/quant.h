// SPDX-License-Identifier: GPL-2.0-or-later
/* quant.h — shared median-cut colour quantiser core (host-side, build-time only).
 *
 * The 256-colour VGA art is reduced to an OPTIMISED 16 chosen from the ST/STE
 * hardware gamut (NOT the fixed EGA 16) — see docs/design/palette_lowcolor.md.
 * Used by tools/quantize.c (QA) and tools/mkworld.c (ships the low-colour pack).
 * All quantisation happens HERE on the host; the ST does only a LUT lookup.
 */
#ifndef U6E_QUANT_H
#define U6E_QUANT_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mul16.h"
#include "cvasm.h"

typedef struct { uint8_t r, g, b; long w; } CCol;   /* a unique colour + its pixel weight */

/* stable sort of col[0..n) by one channel (0=r 1=g 2=b), whose values are all in mn..mx: the same
 * order a stable merge-sort qsort gives, without its per-element multiplies, which are a library call
 * each on a 68000 (the quantiser also runs in the Atari-side converter). Small ranges (most of them,
 * once the boxes split) by insertion, big ones by counting over just mn..mx. */
static int q_key(const CCol *c, int axis){ return axis == 0 ? c->r : axis == 1 ? c->g : c->b; }
static void q_sort_axis(CCol *col, int n, int axis, int mn, int mx)
{
    static CCol tmp[256 * 4];
    if (n < 2) return;
#ifdef CV_ASM
    if (n <= (int)(sizeof tmp / sizeof tmp[0])) { q_sort_68k(col, n, axis, mn, mx, tmp); return; }
#endif
    if (n <= 24) {
        for (int i = 1; i < n; i++) {
            CCol c = col[i]; int v = q_key(&c, axis), j = i;
            while (j > 0 && q_key(&col[j-1], axis) > v) { col[j] = col[j-1]; j--; }
            col[j] = c;
        }
        return;
    }
    CCol *t = n <= (int)(sizeof tmp / sizeof tmp[0]) ? tmp : (CCol *)malloc((size_t)n * sizeof(CCol));
    if (!t) return;
    int start[257], range = mx - mn + 1;
    for (int v = 0; v <= range; v++) start[v] = 0;
    for (int i = 0; i < n; i++) start[q_key(&col[i], axis) - mn + 1]++;
    for (int v = 0; v < range; v++) start[v + 1] += start[v];
    for (int i = 0; i < n; i++) t[start[q_key(&col[i], axis) - mn]++] = col[i];
    for (int i = 0; i < n; i++) col[i] = t[i];
    if (t != tmp) free(t);
}

/* collect the unique colours of an RGB8 buffer (<=256 distinct). */
static inline int q_build_hist(const uint8_t *rgb, long npix, CCol *out, int cap)
{
    int n = 0;
    for (long i = 0; i < npix; i++) {
        int r = rgb[i*3], g = rgb[i*3+1], b = rgb[i*3+2], f = -1;
        for (int j = 0; j < n; j++) if (out[j].r==r && out[j].g==g && out[j].b==b) { f = j; break; }
        if (f < 0) { if (n < cap) { out[n].r=r; out[n].g=g; out[n].b=b; out[n].w=1; n++; } }
        else out[f].w++;
    }
    return n;
}

/* v*w for a channel value v (0..255) and a weight w >= 0, from 16x16 multiplies (mulu.w on the
 * 68000) when w < 2^24, so the product fits 32 bits; a 32- or 64-bit multiply is a library call. */
static inline int64_t q_mul8(int v, long w)
{
    if ((unsigned)v > 255 || w < 0 || w >= (1L << 24)) return (int64_t)v * w;
    uint32_t lo = mulu16((uint16_t)v, (uint16_t)w);
    if (w < 65536) return (int64_t)lo;
    uint32_t hi = mulu16((uint16_t)v, (uint16_t)((uint32_t)w >> 16));
    return (int64_t)(lo + (hi << 16));
}

/* round(sum / w) half up, for sum <= 255*w: the quotient is under 256, so when 2*sum + w fits 32
 * bits it takes 8 shift-and-subtract steps instead of a 64-bit divide (a library call on a 68000) */
static int q_mean(int64_t sum, long w)
{
    if (sum < 0 || w < 1 || w >= (1L << 23) || sum > (int64_t)255 * w) return (int)((2*sum+w)/(2*(int64_t)w));
    uint32_t n = 2u*(uint32_t)sum + (uint32_t)w, d = 2u*(uint32_t)w, q = 0;
    for (int b = 7; b >= 0; b--) { uint32_t t = d << b; if (n >= t) { n -= t; q |= 1u << b; } }
    return (int)q;
}

/* the split score of box col[lo..hi): its longest axis (0=r 1=g 2=b), that axis's lowest and highest
 * value, and range * total weight; or -1 for a box of one colour (never split) */
static long q_box_score(const CCol *col, int lo, int hi, int *axis_out, int *mn_out, int *mx_out)
{
    *axis_out = 0; *mn_out = 0; *mx_out = 255;
    if (hi - lo < 2) return -1;
    int rmn=255,rmx=0,gmn=255,gmx=0,bmn=255,bmx=0; long wsum=0;
#ifdef CV_ASM
    { uint8_t mm[6]; wsum = q_box_scan_68k(&col[lo], hi - lo, mm);
      rmn=mm[0]; rmx=mm[1]; gmn=mm[2]; gmx=mm[3]; bmn=mm[4]; bmx=mm[5]; }
#else
    for (const CCol *c = &col[lo], *e = &col[hi]; c < e; c++) { wsum+=c->w;
        if(c->r<rmn) rmn=c->r; if(c->r>rmx) rmx=c->r;
        if(c->g<gmn) gmn=c->g; if(c->g>gmx) gmx=c->g;
        if(c->b<bmn) bmn=c->b; if(c->b>bmx) bmx=c->b;
    }
#endif
    int dr=rmx-rmn, dg=gmx-gmn, db=bmx-bmn, axis=0, rng=dr;     /* longest axis */
    if(dg>rng){rng=dg;axis=1;} if(db>rng){rng=db;axis=2;}
    *axis_out = axis;
    *mn_out = axis == 0 ? rmn : axis == 1 ? gmn : bmn; *mx_out = *mn_out + rng;
    return (long)q_mul8(rng, wsum);                             /* biggest weighted spread */
}

/* median-cut col[0..ncol) into K weighted boxes; write K centroid colours to pal. Returns box count.
 * Each box's score is worked out once, when the box is made: a split leaves the others alone. */
static int q_median_cut(CCol *col, int ncol, int K, CCol *pal)
{
    typedef struct { int lo, hi, axis, mn, mx; long score; } Box;
    Box box[64]; int nb = 1; box[0].lo = 0; box[0].hi = ncol;
    box[0].score = q_box_score(col, 0, ncol, &box[0].axis, &box[0].mn, &box[0].mx);
    while (nb < K) {
        int best = -1; long bestscore = -1;
        for (int i = 0; i < nb; i++)
            if (box[i].score > bestscore) { bestscore = box[i].score; best = i; }
        if (best < 0) break;
        Box b = box[best];
        q_sort_axis(&col[b.lo], b.hi-b.lo, b.axis, b.mn, b.mx);
        long total=0; for (int j=b.lo;j<b.hi;j++) total+=col[j].w;
        long acc=0; int mid=b.lo;
        for (int j=b.lo;j<b.hi;j++){ acc+=col[j].w; if(acc*2>=total){ mid=j+1; break; } }
        if (mid<=b.lo) mid=b.lo+1;
        if (mid>=b.hi) mid=b.hi-1;
        box[best].hi=mid; box[nb].lo=mid; box[nb].hi=b.hi;
        box[best].score = q_box_score(col, box[best].lo, mid, &box[best].axis, &box[best].mn, &box[best].mx);
        box[nb].score = q_box_score(col, mid, b.hi, &box[nb].axis, &box[nb].mn, &box[nb].mx);
        nb++;
    }
    for (int i=0;i<nb;i++){ int64_t rr=0,gg=0,bb=0; long w=0;  /* weighted mean, rounded half up */
        for (int j=box[i].lo;j<box[i].hi;j++){ CCol*c=&col[j]; rr+=q_mul8(c->r,c->w); gg+=q_mul8(c->g,c->w); bb+=q_mul8(c->b,c->w); w+=c->w; }
        if(w<1)w=1;
        pal[i].r=q_mean(rr,w); pal[i].g=q_mean(gg,w); pal[i].b=q_mean(bb,w); pal[i].w=w;
    }
    return nb;
}

/* snap 8-bit v to a `bits`-per-channel hardware value (ST=3, STE=4): quantise then expand. */
static int q_hw_snap(int v, int bits){ int mx=(1<<bits)-1; int l=(v*mx+127)/255; return (l*255+mx/2)/mx; }

/* the same sums by table, since each palette asks 48 times per form and each is a library divide on
 * a 68000: q_lv3[v], q_lv4[v] = the level (0..7, 0..15) of 8-bit v; q_snap3[v] = q_hw_snap(v,3) */
static uint8_t q_lv3[256], q_lv4[256], q_snap3[256];
static void q_lv_init(void){
    static int init;
    if (!init) { for (int v = 0; v < 256; v++) { q_lv3[v] = (uint8_t)((v*7+127)/255); q_lv4[v] = (uint8_t)((v*15+127)/255); q_snap3[v] = (uint8_t)q_hw_snap(v,3); } init = 1; }
}

static void q_snap_pal(CCol *pal, int n, int bits){
    if (bits == 3) { q_lv_init(); for(int i=0;i<n;i++){ pal[i].r=q_snap3[pal[i].r]; pal[i].g=q_snap3[pal[i].g]; pal[i].b=q_snap3[pal[i].b]; } return; }
    for(int i=0;i<n;i++){ pal[i].r=q_hw_snap(pal[i].r,bits); pal[i].g=q_hw_snap(pal[i].g,bits); pal[i].b=q_hw_snap(pal[i].b,bits); }
}

/* the tables of the nearest-colour search below (index = difference + 255): a muls.w costs up to 70
 * cycles on the 68000, a table read a tenth of that, and the sums are the same */
static long q_sq3[511], q_sq6[511], q_sq1[511];
static void q_sq_init(void){
    static int init;
    if (!init) { for (int i = 0; i < 511; i++) { long d = (long)(i-255)*(i-255); q_sq1[i] = d; q_sq3[i] = d*3; q_sq6[i] = d*6; } init = 1; }
}

/* nearest palette entry to (r,g,b) by luma-weighted squared distance (~3:6:1 R:G:B). */
static __attribute__((unused)) int q_nearest(const CCol *pal, int n, int r, int g, int b){
    q_sq_init();
    const long *tr = q_sq3 + 255 - r, *tg = q_sq6 + 255 - g, *tb = q_sq1 + 255 - b;
    int best=0; long bd=1000000000L;
    for(int i=0;i<n;i++){                       /* green weighs most: a worse partial sum stops early */
        long d = tg[pal[i].g]; if (d >= bd) continue;
        d += tr[pal[i].r];     if (d >= bd) continue;
        d += tb[pal[i].b];
        if(d<bd){ bd=d; best=i; } }
    return best;
}

/* lut[i] = the nearest of the n palette entries (1..16) to colour i of pal8 (256 r,g,b bytes) */
static void q_lut(const CCol *pal, int n, const uint8_t *pal8, uint8_t lut[256]){
#ifdef CV_ASM
    uint8_t cand[16*3];
    q_sq_init();
    for (int i = 0; i < n; i++) { cand[i*3] = pal[i].g; cand[i*3+1] = pal[i].r; cand[i*3+2] = pal[i].b; }
    q_lut_68k(pal8, cand, n, lut, q_sq6, q_sq3, q_sq1);
#else
    for (int i = 0; i < 256; i++) lut[i] = (uint8_t)q_nearest(pal, n, pal8[i*3], pal8[i*3+1], pal8[i*3+2]);
#endif
}

#endif /* U6E_QUANT_H */
