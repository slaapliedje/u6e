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

typedef struct { int r, g, b; long w; } CCol;   /* a unique colour + its pixel weight */

static int q_cmpR(const void *a, const void *b){ return ((const CCol*)a)->r - ((const CCol*)b)->r; }
static int q_cmpG(const void *a, const void *b){ return ((const CCol*)a)->g - ((const CCol*)b)->g; }
static int q_cmpB(const void *a, const void *b){ return ((const CCol*)a)->b - ((const CCol*)b)->b; }

/* collect the unique colours of an RGB8 buffer (<=256 distinct). */
static int q_build_hist(const uint8_t *rgb, long npix, CCol *out, int cap)
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

/* median-cut col[0..ncol) into K weighted boxes; write K centroid colours to pal. Returns box count. */
static int q_median_cut(CCol *col, int ncol, int K, CCol *pal)
{
    typedef struct { int lo, hi; } Box;
    Box box[64]; int nb = 1; box[0].lo = 0; box[0].hi = ncol;
    while (nb < K) {
        int best = -1, bestaxis = 0; long bestscore = -1;
        for (int i = 0; i < nb; i++) {
            if (box[i].hi - box[i].lo < 2) continue;
            int rmn=255,rmx=0,gmn=255,gmx=0,bmn=255,bmx=0; long wsum=0;
            for (int j = box[i].lo; j < box[i].hi; j++) { CCol *c=&col[j]; wsum+=c->w;
                if(c->r<rmn) rmn=c->r; if(c->r>rmx) rmx=c->r;
                if(c->g<gmn) gmn=c->g; if(c->g>gmx) gmx=c->g;
                if(c->b<bmn) bmn=c->b; if(c->b>bmx) bmx=c->b;
            }
            int dr=rmx-rmn, dg=gmx-gmn, db=bmx-bmn, axis=0, rng=dr;     /* longest axis */
            if(dg>rng){rng=dg;axis=1;} if(db>rng){rng=db;axis=2;}
            long score = (long)rng * wsum;                              /* biggest weighted spread */
            if (score > bestscore) { bestscore=score; best=i; bestaxis=axis; }
        }
        if (best < 0) break;
        Box b = box[best];
        qsort(&col[b.lo], (size_t)(b.hi-b.lo), sizeof(CCol),
              bestaxis==0?q_cmpR : bestaxis==1?q_cmpG : q_cmpB);
        long total=0; for (int j=b.lo;j<b.hi;j++) total+=col[j].w;
        long acc=0; int mid=b.lo;
        for (int j=b.lo;j<b.hi;j++){ acc+=col[j].w; if(acc*2>=total){ mid=j+1; break; } }
        if (mid<=b.lo) mid=b.lo+1;
        if (mid>=b.hi) mid=b.hi-1;
        box[best].hi=mid; box[nb].lo=mid; box[nb].hi=b.hi; nb++;
    }
    for (int i=0;i<nb;i++){ double rr=0,gg=0,bb=0; long w=0;
        for (int j=box[i].lo;j<box[i].hi;j++){ CCol*c=&col[j]; rr+=(double)c->r*c->w; gg+=(double)c->g*c->w; bb+=(double)c->b*c->w; w+=c->w; }
        if(w<1)w=1;
        pal[i].r=(int)(rr/w+0.5); pal[i].g=(int)(gg/w+0.5); pal[i].b=(int)(bb/w+0.5); pal[i].w=w;
    }
    return nb;
}

/* snap 8-bit v to a `bits`-per-channel hardware value (ST=3, STE=4): quantise then expand. */
static int q_hw_snap(int v, int bits){ int mx=(1<<bits)-1; int l=(v*mx+127)/255; return (l*255+mx/2)/mx; }
static void q_snap_pal(CCol *pal, int n, int bits){
    for(int i=0;i<n;i++){ pal[i].r=q_hw_snap(pal[i].r,bits); pal[i].g=q_hw_snap(pal[i].g,bits); pal[i].b=q_hw_snap(pal[i].b,bits); }
}

/* nearest palette entry to (r,g,b) by luma-weighted squared distance (~3:6:1 R:G:B). */
static int q_nearest(const CCol *pal, int n, int r, int g, int b){
    int best=0; long bd=1000000000L;
    for(int i=0;i<n;i++){ long dr=pal[i].r-r, dg=pal[i].g-g, db=pal[i].b-b;
        long d = dr*dr*3 + dg*dg*6 + db*db;
        if(d<bd){ bd=d; best=i; } }
    return best;
}

#endif /* U6E_QUANT_H */
