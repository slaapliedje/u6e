// SPDX-License-Identifier: GPL-2.0-or-later
/* splitcheck.c — HOST experiment answering "is a map/panel MID-LINE palette split worth it on the
 * ST?" (docs/design/palette_lowcolor.md). A mid-line split would let the map keep its 16 while the
 * right PANEL gets a different 16 on the same scanlines. But the MAP has no headroom: band 0 IS the
 * tileset palette (built from exactly what the map shows). So the only thing that could benefit is
 * the panel — parchment (PAPER.BMP) and, during conversation, a PORTRAIT (skin tones). This tool
 * measures how those render through the shipped terrain-16 (U6WW) vs their OWN optimal 16 (same ST
 * 3-bit gamut), and writes a side-by-side portrait PNG. Big gap => a split (or a conversation-mode
 * palette) helps; small gap => not worth the cycle-exact raster.
 * Usage: splitcheck <refdata/dos/u6> <build/U6.PAK> <out.png>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "quant.h"
#include "u6lzw.h"
#include "u6lib.h"
#include "png.h"

static uint8_t *slurp(const char *d, const char *name, long *n){
    char p[1024]; snprintf(p,sizeof p,"%s/%s",d,name);
    FILE *f=fopen(p,"rb"); if(!f){ printf("cannot read %s\n",p); exit(2);}
    fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t *b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){exit(2);} fclose(f); return b;
}
static uint8_t *slurp_file(const char *path, long *n){
    FILE *f=fopen(path,"rb"); if(!f){ printf("cannot read %s\n",path); exit(2);}
    fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t *b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){exit(2);} fclose(f); return b;
}
#define R8(v) (((v)*255+31)/63)      /* RGB6 (0..63) -> RGB8 */
#define L8(v) (((v)*255)/7)          /* ST 3-bit level (0..7) -> RGB8 */

/* find "U6WW" in the pak -> term_lvl[48] (3-bit levels) + term_lut[256]. */
static int read_terrain16(const uint8_t *pak, long n, uint8_t lvl[48], uint8_t lut[256]){
    for(long i=n-308;i>=0;i--) if(pak[i]=='U'&&pak[i+1]=='6'&&pak[i+2]=='W'&&pak[i+3]=='W'){
        memcpy(lvl,pak+i+4,48); memcpy(lut,pak+i+52,256); return 1; }
    return 0;
}
/* quantise an index image's own colours -> lut_out[256] + pal_out[16][3] (RGB8, snapped to 3-bit). */
static void own16(const uint8_t *idx, int npx, const uint8_t *palb, uint8_t lut_out[256], int pal_out[16][3]){
    static long freq[256]; for(int i=0;i<256;i++) freq[i]=0;
    for(int i=0;i<npx;i++){ int c=idx[i]; if(c!=255) freq[c]++; }
    static CCol col[256]; int nc=0;
    for(int i=0;i<256;i++){ if(!freq[i])continue; col[nc].r=R8(palb[i*3]);col[nc].g=R8(palb[i*3+1]);col[nc].b=R8(palb[i*3+2]);col[nc].w=freq[i];nc++; }
    CCol pal[16]; int np=q_median_cut(col,nc,16,pal); q_snap_pal(pal,np,3);
    for(int i=0;i<16;i++){ pal_out[i][0]=i<np?pal[i].r:0; pal_out[i][1]=i<np?pal[i].g:0; pal_out[i][2]=i<np?pal[i].b:0; }
    for(int i=0;i<256;i++) lut_out[i]=(uint8_t)q_nearest(pal,np,R8(palb[i*3]),R8(palb[i*3+1]),R8(palb[i*3+2]));
}
/* weighted RMS of an index image through lut -> pal16 (RGB8), vs the true U6PAL colour. */
static double img_rms(const uint8_t *idx,int npx,const uint8_t *palb,const uint8_t *lut,const int pal16[16][3]){
    double se=0; long nn=0;
    for(int i=0;i<npx;i++){ int c=idx[i]; if(c==255)continue;
        int r=R8(palb[c*3]),g=R8(palb[c*3+1]),b=R8(palb[c*3+2]); int j=lut[c];
        long dr=pal16[j][0]-r,dg=pal16[j][1]-g,db=pal16[j][2]-b; se+=(double)(dr*dr+dg*dg+db*db); nn++; }
    return nn? sqrt(se/(3.0*(double)nn)):0.0;
}

int main(int argc,char**argv){
    const char *dir = argc>1?argv[1]:"refdata/dos/u6";
    const char *pakp= argc>2?argv[2]:"build/U6.PAK";
    const char *outp= argc>3?argv[3]:"build/splitcheck.png";

    long pn; uint8_t *palb=slurp(dir,"U6PAL",&pn);                 /* 768B RGB6 */
    long kn; uint8_t *pak=slurp_file(pakp,&kn);
    uint8_t tlvl[48], tlut[256];
    if(!read_terrain16(pak,kn,tlvl,tlut)){ printf("no U6WW in %s\n",pakp); return 2; }
    int tpal[16][3]; for(int i=0;i<16;i++){ tpal[i][0]=L8(tlvl[i*3]); tpal[i][1]=L8(tlvl[i*3+1]); tpal[i][2]=L8(tlvl[i*3+2]); }

    /* ---- PARCHMENT (PAPER.BMP, LZW: [u16 w][u16 h][w*h idx]) ---- */
    long ppn; uint8_t *pc=slurp(dir,"PAPER.BMP",&ppn);
    int32_t pu=u6lzw_unpacked_size(pc,(int32_t)ppn); uint8_t *pd=malloc((size_t)pu);
    u6lzw_decompress(pc,(int32_t)ppn,pd,pu);
    uint8_t *paper=pd+4; int ppx=320*200;
    static uint8_t plut[256]; int ppal[16][3]; own16(paper,ppx,palb,plut,ppal);
    printf("PARCHMENT (PAPER.BMP): through terrain-16 RMS %.2f  vs its own-16 RMS %.2f\n",
           img_rms(paper,ppx,palb,tlut,tpal), img_rms(paper,ppx,palb,plut,ppal));

    /* ---- PORTRAIT 0 (PORTRAIT.A: 4-byte-offset lib of LZW 56x64) ---- */
    long prn; uint8_t *pra=slurp(dir,"PORTRAIT.A",&prn);
    u6lib_t lib; if(!u6lib_open(&lib,pra,(int32_t)prn,4,false)){ printf("PORTRAIT.A open failed\n"); return 2; }
    int PW=56,PH=64,PPX=PW*PH; static uint8_t port[56*64];
    int32_t sz; const uint8_t *blob=u6lib_entry(&lib,0,&sz);
    if(!blob||u6lzw_unpacked_size(blob,sz)!=PPX){ printf("portrait 0 bad\n"); return 2; }
    u6lzw_decompress(blob,sz,port,PPX);
    static uint8_t olut[256]; int opal[16][3]; own16(port,PPX,palb,olut,opal);
    double rt=img_rms(port,PPX,palb,tlut,tpal), ro=img_rms(port,PPX,palb,olut,opal);
    printf("PORTRAIT 0:            through terrain-16 RMS %.2f  vs its own-16 RMS %.2f  (%.0f%% worse via terrain)\n",
           rt, ro, ro>0? (rt-ro)/ro*100.0 : 0.0);

    /* side-by-side PNG: [orig RGB] | [via terrain-16] | [via portrait-16], 3x upscale */
    int SC=3, gap=8, colw=PW*SC, W=colw*3+gap*2, H=PH*SC;
    uint8_t *rgb=calloc((size_t)W*H,3);
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        int col=x<colw?0 : x<colw+gap?-1 : x<colw*2+gap?1 : x<colw*2+gap*2?-1 : 2;
        if(col<0) continue;
        int sx=(col==0?x : col==1?x-colw-gap : x-colw*2-gap*2)/SC, sy=y/SC;
        if(sx<0||sx>=PW||sy<0||sy>=PH) continue;
        int c=port[sy*PW+sx]; int r,g,b;
        if(col==0){ r=R8(palb[c*3]); g=R8(palb[c*3+1]); b=R8(palb[c*3+2]); }
        else if(col==1){ int j=tlut[c]; r=tpal[j][0]; g=tpal[j][1]; b=tpal[j][2]; }
        else { int j=olut[c]; r=opal[j][0]; g=opal[j][1]; b=opal[j][2]; }
        uint8_t *o=rgb+((size_t)y*W+x)*3; o[0]=(uint8_t)r; o[1]=(uint8_t)g; o[2]=(uint8_t)b;
    }
    png_write_rgb(outp,W,H,rgb);
    printf("wrote %s  (portrait: VGA original | via map's terrain-16 | via a portrait-16)\n", outp);
    return 0;
}
