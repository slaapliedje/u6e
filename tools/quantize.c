/* quantize.c — M7 host-side 256->16 palette quantiser (HEADLINE: base ST/STE looks BETTER
 * than the original lazy port). Reduces the 256-colour VGA art to an OPTIMISED 16-colour
 * palette chosen from the REAL ST gamut (512 = 3 bits/ch) or STE gamut (4096 = 4 bits/ch),
 * NOT the fixed EGA 16. Snaps candidate colours to the hardware value BEFORE measuring error
 * (so the optimiser accounts for hardware rounding), remaps, and writes a side-by-side QA PNG
 * plus a per-target RMS error metric — including a naive EGA-16 baseline to show the win.
 *
 * See docs/design/palette_lowcolor.md + docs/design/milestone.md M7. Usage:
 *   quantize <refdata/dos/u6> <out-dir>
 * Reads the user's own data in /refdata; writes only PNG QA into <out-dir>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "u6lzw.h"
#include "u6pal.h"
#include "u6map.h"
#include "u6tile.h"
#include "png.h"

static uint8_t *slurp(const char *dir, const char *name, long *n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) { printf("  (cannot read %s)\n", p); return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f); return b;
}

/* ---------------- median-cut quantiser (operates on RGB8 pixels) ---------------- */
typedef struct { int r, g, b; long w; } CCol;      /* a unique colour + its pixel weight */
static int cmpR(const void *a, const void *b){ return ((const CCol*)a)->r - ((const CCol*)b)->r; }
static int cmpG(const void *a, const void *b){ return ((const CCol*)a)->g - ((const CCol*)b)->g; }
static int cmpB(const void *a, const void *b){ return ((const CCol*)a)->b - ((const CCol*)b)->b; }

/* collect the unique colours of an RGB8 image (source has <=256 distinct palette colours). */
static int build_hist(const uint8_t *rgb, long npix, CCol *out, int cap)
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

typedef struct { int lo, hi; } Box;                /* a contiguous range of the sorted colour list */

/* median-cut `col[0..ncol)` into K boxes; write K centroid colours to pal. Returns box count. */
static int median_cut(CCol *col, int ncol, int K, CCol *pal)
{
    Box box[64]; int nb = 1; box[0].lo = 0; box[0].hi = ncol;
    while (nb < K) {
        int best = -1, bestaxis = 0; long bestscore = -1;
        for (int i = 0; i < nb; i++) {
            if (box[i].hi - box[i].lo < 2) continue;
            int rmn=255,rmx=0,gmn=255,gmx=0,bmn=255,bmx=0; long wsum=0;
            for (int j = box[i].lo; j < box[i].hi; j++) { CCol *c=&col[j]; wsum+=c->w;
                if(c->r<rmn) rmn=c->r;
                if(c->r>rmx) rmx=c->r;
                if(c->g<gmn) gmn=c->g;
                if(c->g>gmx) gmx=c->g;
                if(c->b<bmn) bmn=c->b;
                if(c->b>bmx) bmx=c->b;
            }
            int dr=rmx-rmn, dg=gmx-gmn, db=bmx-bmn, axis=0, rng=dr;   /* longest axis */
            if(dg>rng){rng=dg;axis=1;} if(db>rng){rng=db;axis=2;}
            long score = (long)rng * wsum;                            /* split the biggest weighted spread */
            if (score > bestscore) { bestscore=score; best=i; bestaxis=axis; }
        }
        if (best < 0) break;                                          /* nothing left to split */
        Box b = box[best];
        qsort(&col[b.lo], (size_t)(b.hi-b.lo), sizeof(CCol),
              bestaxis==0?cmpR : bestaxis==1?cmpG : cmpB);
        long total=0; for (int j=b.lo;j<b.hi;j++) total+=col[j].w;
        long acc=0; int mid=b.lo;
        for (int j=b.lo;j<b.hi;j++){ acc+=col[j].w; if(acc*2>=total){ mid=j+1; break; } }
        if (mid<=b.lo) mid=b.lo+1;
        if (mid>=b.hi) mid=b.hi-1;                                    /* keep both halves non-empty */
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
static int hw_snap(int v, int bits){ int mx=(1<<bits)-1; int l=(v*mx+127)/255; return (l*255+mx/2)/mx; }
static void snap_pal(CCol *pal, int n, int bits){
    for(int i=0;i<n;i++){ pal[i].r=hw_snap(pal[i].r,bits); pal[i].g=hw_snap(pal[i].g,bits); pal[i].b=hw_snap(pal[i].b,bits); }
}

/* nearest palette entry to (r,g,b) by luma-weighted squared distance (~3:6:1 R:G:B). */
static int nearest(const CCol *pal, int n, int r, int g, int b){
    int best=0; long bd=1000000000L;                 /* > max luma-weighted distance (255^2*10) */
    for(int i=0;i<n;i++){ long dr=pal[i].r-r, dg=pal[i].g-g, db=pal[i].b-b;
        long d = dr*dr*3 + dg*dg*6 + db*db;
        if(d<bd){ bd=d; best=i; } }
    return best;
}

/* remap src (RGB8) through pal into dst; return RMS per-channel error vs the true colours. */
static double remap_rms(const uint8_t *src, uint8_t *dst, long npix, const CCol *pal, int n){
    double se=0;
    for(long i=0;i<npix;i++){ int r=src[i*3],g=src[i*3+1],b=src[i*3+2], k=nearest(pal,n,r,g,b);
        dst[i*3]=(uint8_t)pal[k].r; dst[i*3+1]=(uint8_t)pal[k].g; dst[i*3+2]=(uint8_t)pal[k].b;
        double dr=pal[k].r-r, dg=pal[k].g-g, db=pal[k].b-b; se += dr*dr+dg*dg+db*db;
    }
    return sqrt(se/(3.0*(double)npix));
}

/* the classic fixed EGA-16 palette (RGB8) — the "lazy port" baseline we beat. */
static const CCol EGA16[16] = {
    {0x00,0x00,0x00,0},{0x00,0x00,0xAA,0},{0x00,0xAA,0x00,0},{0x00,0xAA,0xAA,0},
    {0xAA,0x00,0x00,0},{0xAA,0x00,0xAA,0},{0xAA,0x55,0x00,0},{0xAA,0xAA,0xAA,0},
    {0x55,0x55,0x55,0},{0x55,0x55,0xFF,0},{0x55,0xFF,0x55,0},{0x55,0xFF,0xFF,0},
    {0xFF,0x55,0x55,0},{0xFF,0x55,0xFF,0},{0xFF,0xFF,0x55,0},{0xFF,0xFF,0xFF,0},
};

/* quantise `src` to 16 colours for one hw target, print the RMS + palette, return the remap. */
static double do_target(const char *label, const uint8_t *src, long npix, int bits,
                        uint8_t *dst, CCol *pal_out)
{
    static CCol uniq[4096];
    int nu = build_hist(src, npix, uniq, 4096);
    CCol pal[16]; int np = median_cut(uniq, nu, 16, pal);
    snap_pal(pal, np, bits);                                  /* snap BEFORE measuring error */
    double rms = remap_rms(src, dst, npix, pal, np);
    printf("  %-10s %2d-bit/ch gamut, %2d colours, RMS err = %6.2f\n", label, bits, np, rms);
    for(int i=0;i<16 && pal_out;i++) pal_out[i] = i<np?pal[i]:(CCol){0,0,0,0};
    return rms;
}

/* stack the panels [src | dst...] horizontally (with a gap), 2x-upscaled, into one QA PNG. */
static void write_qa(const char *path, int w, int h, int npanels, uint8_t **panels)
{
    int gap=8, S=2, pw=w*S, ph=h*S, W = npanels*pw + (npanels-1)*gap, Hh = ph;
    uint8_t *out = calloc((size_t)W*Hh*3, 1);
    for(int pi=0; pi<npanels; pi++){ int ox=pi*(pw+gap);
        for(int y=0;y<h;y++) for(int x=0;x<w;x++){ const uint8_t*s=panels[pi]+((size_t)y*w+x)*3;
            for(int dy=0;dy<S;dy++) for(int dx=0;dx<S;dx++){
                uint8_t*d=out+(((size_t)(y*S+dy))*W + (ox + x*S+dx))*3; d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; } } }
    png_write_rgb(path, W, Hh, out);
    free(out);
}

/* quantise src (RGB8) to <=16 colours for a `bits`-per-channel target; fill pal, return count. */
static int quant16(const uint8_t *src, long npix, int bits, CCol *pal)
{
    static CCol uniq[4096];
    int nu = build_hist(src, npix, uniq, 4096);
    int np = median_cut(uniq, nu, 16, pal);
    snap_pal(pal, np, bits);
    return np;
}

/* ---------------- per-region palettes (U6 swaps palette by context) ---------------- */

/* render an rw x rh TILE region of the surface at full 16x16 into an RGB8 buffer. */
static void render_region(const u6map_t *map, const u6tileset_t *ts, const u6pal_t *pal,
                          int ox, int oy, int rw, int rh, uint8_t *dst)
{
    int W = rw*U6_TILE_W;
    for (int ty=0; ty<rh; ty++) for (int tx=0; tx<rw; tx++) {
        uint8_t tile = u6map_surface_tile((u6map_t*)map,
                          (ox+tx)&(U6_SURFACE_TILES-1), (oy+ty)&(U6_SURFACE_TILES-1));
        int gx=tx*U6_TILE_W, gy=ty*U6_TILE_H;
        for (int yy=0; yy<U6_TILE_H; yy++) for (int xx=0; xx<U6_TILE_W; xx++) {
            u6_rgb c = pal->color[u6tile_pixel((u6tileset_t*)ts, tile, xx, yy)];
            uint8_t *p = dst + ((size_t)(gy+yy)*W + (gx+xx))*3; p[0]=c.r; p[1]=c.g; p[2]=c.b;
        }
    }
}

/* score a region by how many DISTINCT surface tiles it uses (ocean = ~1; varied land = many). */
static int region_variety(const u6map_t *map, int ox, int oy, int rw, int rh)
{
    static uint8_t seen[2048]; memset(seen, 0, sizeof seen); int n=0;
    for (int ty=0; ty<rh; ty++) for (int tx=0; tx<rw; tx++) {
        uint16_t t = u6map_surface_tile((u6map_t*)map,
                        (ox+tx)&(U6_SURFACE_TILES-1), (oy+ty)&(U6_SURFACE_TILES-1));
        if (t<2048 && !seen[t]) { seen[t]=1; n++; }
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *dir = (argc>1)?argv[1]:"refdata/dos/u6";
    const char *out = (argc>2)?argv[2]:"build";
    printf("== quantize (M7): 256-colour VGA -> optimised 16-colour ST/STE, from %s ==\n", dir);

    long pn, tn, in;
    uint8_t *palb = slurp(dir, "U6PAL", &pn);
    uint8_t *mtc  = slurp(dir, "MAPTILES.VGA", &tn);
    uint8_t *tib  = slurp(dir, "TILEINDX.VGA", &in);
    if(!palb || !mtc || !tib) return 2;

    u6pal_t pal; if(!u6pal_parse(&pal, palb, (int32_t)pn)){ printf("U6PAL parse failed\n"); return 2; }
    int32_t musz = u6lzw_unpacked_size(mtc,(int32_t)tn);
    uint8_t *mt = malloc((size_t)musz);
    u6lzw_decompress(mtc,(int32_t)tn,mt,musz);
    u6tileset_t ts; u6tile_init(&ts, mt, musz, tib,(int32_t)in);

    /* map (for the per-region palette demo below) */
    long mn=0, cn=0; uint8_t *mapb=slurp(dir,"MAP",&mn), *chkb=slurp(dir,"CHUNKS",&cn);
    u6map_t map; int have_map = mapb && chkb && u6map_load(&map, mapb,(int32_t)mn, chkb,(int32_t)cn);

    /* SOURCE = the map-tile sheet (tiles 0..511) at full 16x16 in a 32-wide grid = 512x256.
     * A broad, deterministic sample of the gameplay palette (terrain + object base tiles). */
    int NT=512, GW=32, tw=U6_TILE_W, th=U6_TILE_H, w=GW*tw, h=(NT/GW)*th; long npix=(long)w*h;
    uint8_t *src = malloc((size_t)npix*3);
    for(int t=0;t<NT;t++){ int gx=(t%GW)*tw, gy=(t/GW)*th;
        for(int yy=0;yy<th;yy++) for(int xx=0;xx<tw;xx++){ u6_rgb c=pal.color[u6tile_pixel(&ts,t,xx,yy)];
            uint8_t*p=src+((size_t)(gy+yy)*w+(gx+xx))*3; p[0]=c.r; p[1]=c.g; p[2]=c.b; } }

    int nuq; { static CCol uniq[4096]; nuq=build_hist(src,npix,uniq,4096); }
    printf("  source: %dx%d (%d map tiles), %d distinct colours\n", w,h,NT,nuq);

    uint8_t *st16=malloc((size_t)npix*3), *ste16=malloc((size_t)npix*3), *ega=malloc((size_t)npix*3);
    do_target("ST",  src, npix, 3, st16,  NULL);
    do_target("STE", src, npix, 4, ste16, NULL);
    { double rms=remap_rms(src, ega, npix, EGA16, 16);        /* fixed EGA-16 baseline */
      printf("  %-10s (fixed)          16 colours, RMS err = %6.2f  <- the naive 'lazy port' baseline\n","EGA16",rms); }

    char path[1024]; snprintf(path,sizeof path,"%s/quant_qa.png",out);
    uint8_t *panels[4]={src,st16,ste16,ega};
    write_qa(path, w, h, 4, panels);
    printf("  -> %s  (2x; panels: VGA-256 | our ST-16 | our STE-16 | naive EGA-16)\n", path);

    /* ---- PER-REGION palettes: U6 swaps palette by context, so a palette fit to each REGION
     * beats one global 16 applied everywhere. Pick the 4 most tile-varied, spread-out regions,
     * quantise each to its own STE-16, and compare against a single global STE-16. ---- */
    if(have_map){
        enum { RTW=32, RTH=32, NREG=4 };
        int rpx=RTW*U6_TILE_W, rpy=RTH*U6_TILE_H; long rpix=(long)rpx*rpy;
        int rox[NREG], roy[NREG], nsel=0;
        for(int s=0;s<NREG;s++){                                  /* greedily pick most-varied, spread out */
            int bx=0,by=0,bs=-1;
            for(int gy=0; gy<U6_SURFACE_TILES; gy+=RTH) for(int gx=0; gx<U6_SURFACE_TILES; gx+=RTW){
                int close=0; for(int k=0;k<nsel;k++){ if(abs(gx-rox[k])<RTW*2 && abs(gy-roy[k])<RTH*2){ close=1; break; } }
                if(close) continue;
                int v=region_variety(&map,gx,gy,RTW,RTH);
                if(v>bs){ bs=v; bx=gx; by=gy; }
            }
            rox[nsel]=bx; roy[nsel]=by; nsel++;
        }
        uint8_t *reg[NREG], *all=malloc((size_t)rpix*NREG*3);
        for(int r=0;r<NREG;r++){ reg[r]=malloc((size_t)rpix*3);
            render_region(&map,&ts,&pal,rox[r],roy[r],RTW,RTH,reg[r]);
            memcpy(all+(size_t)r*rpix*3, reg[r], (size_t)rpix*3); }
        CCol gpal[16]; int gn=quant16(all,(long)rpix*NREG,4,gpal);   /* one GLOBAL palette from all regions */
        free(all);
        printf("\n  per-region STE-16 palettes vs one GLOBAL STE-16 (lower RMS = closer to VGA):\n");
        uint8_t *pr[NREG], *pg[NREG]; double sumr=0,sumg=0;
        for(int r=0;r<NREG;r++){
            CCol rp[16]; int rn=quant16(reg[r],rpix,4,rp);
            pr[r]=malloc((size_t)rpix*3); pg[r]=malloc((size_t)rpix*3);
            double er=remap_rms(reg[r],pr[r],rpix,rp,rn), eg=remap_rms(reg[r],pg[r],rpix,gpal,gn);
            printf("    region %d @ tile(%4d,%4d): per-region %5.2f | global %5.2f  (%2.0f%% better)\n",
                   r, rox[r],roy[r], er, eg, eg>0?100.0*(eg-er)/eg:0);
            sumr+=er; sumg+=eg;
        }
        printf("    TOTAL: per-region %.2f | global %.2f  -> per-region is %.0f%% lower error\n",
               sumr,sumg, sumg>0?100.0*(sumg-sumr)/sumg:0);
        /* QA: NREG rows x 3 cols (VGA | per-region-16 | global-16), no upscale. */
        int cols=3, gap=6, W=cols*rpx+(cols-1)*gap, H=NREG*rpy+(NREG-1)*gap;
        uint8_t *qa=calloc((size_t)W*H*3,1);
        for(int r=0;r<NREG;r++){ uint8_t *col3[3]={reg[r],pr[r],pg[r]};
            for(int c=0;c<3;c++){ int oxp=c*(rpx+gap), oyp=r*(rpy+gap);
                for(int y=0;y<rpy;y++) for(int x=0;x<rpx;x++){ uint8_t*s2=col3[c]+((size_t)y*rpx+x)*3;
                    uint8_t*d=qa+((size_t)(oyp+y)*W+(oxp+x))*3; d[0]=s2[0];d[1]=s2[1];d[2]=s2[2]; } } }
        snprintf(path,sizeof path,"%s/quant_regions.png",out);
        png_write_rgb(path,W,H,qa);
        printf("  -> %s  (rows = 4 regions; cols: VGA-256 | per-region STE-16 | global STE-16)\n", path);
        free(qa); for(int r=0;r<NREG;r++){ free(reg[r]); free(pr[r]); free(pg[r]); }
    }
    return 0;
}
