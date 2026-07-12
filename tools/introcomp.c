// SPDX-License-Identifier: GPL-2.0-or-later
/* introcomp.c — HOST: composite a list of U6 intro SHAPE indices onto a shared
 * 320x200 canvas at each shape's ABSOLUTE coordinates (hotspot leftX/upperY +
 * per-block x/y), index 0 transparent. Tests the hypothesis that intro overlay
 * sprites (avatar, etc.) self-position onto the base scene when drawn at a common
 * screen origin. Clean-room: block layout per docs/formats/shapes.md.
 *
 * Usage: introcomp <file.shp> <U6PAL|pal.bin> <out.png> <idx> [idx...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "u6lzw.h"
#include "u6pal.h"
#include "png.h"

#define CW 320
#define CH 200

static uint8_t *slurp(const char*p,long*n){ FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);exit(2);}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET); uint8_t*b=malloc((size_t)*n);
  if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);} fclose(f); return b; }

static uint32_t le32(const uint8_t*p){ return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24)); }
static uint16_t le16(const uint8_t*p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static int16_t  le16s(const uint8_t*p){ return (int16_t)(p[0]|(p[1]<<8)); }

/* paint one shape onto canvas at absolute coords + (ox,oy); returns 0 on bad idx */
static int paint(const uint8_t*d,int32_t dl,int idx,int ox,int oy,uint8_t*canvas)
{
    uint32_t off0=le32(d+4); int N=(int)((off0-4)/4);
    if(idx<0||idx>=N) return 0;
    uint32_t off=le32(d+4+idx*4);
    if(off+8>(uint32_t)dl) return 0;
    const uint8_t*sh=d+off,*end=d+dl;
    int lX=le16(sh+2), uY=le16(sh+4);
    const uint8_t*p=sh+8;
    while(p+6<=end){
        uint16_t dv=le16(p); p+=2; if(dv==0) break;
        int blen=dv>>1, flag=dv&1;
        int px=ox+lX+le16s(p); p+=2;
        int py=oy+uY+le16s(p); p+=2;
        int wrote=0;
        while(wrote<blen && p<end){
            if(flag){ uint8_t b=*p++; int cnt=b>>1;
                if(b&1){ uint8_t v=(p<end)?*p++:0;
                    for(int i=0;i<cnt;i++){ if(v&&(unsigned)px<CW&&(unsigned)py<CH) canvas[py*CW+px]=v; px++; } }
                else   { for(int i=0;i<cnt&&p<end;i++){ uint8_t v=*p++;
                    if(v&&(unsigned)px<CW&&(unsigned)py<CH) canvas[py*CW+px]=v; px++; } }
                wrote+=cnt;
            } else { uint8_t v=*p++; if(v&&(unsigned)px<CW&&(unsigned)py<CH) canvas[py*CW+px]=v; px++; wrote++; }
        }
    }
    printf("  painted shape %d (hotspot %d,%d)\n",idx,lX,uY);
    return 1;
}

/* load a 256-colour palette. path may be "file:k" to select sub-palette k (768-byte
 * stride, for PALETTES.INT). Auto-detect encoding: if every byte <=63 it's 6-bit VGA
 * (expand to 8-bit); otherwise raw 8-bit RGB (PALETTES.INT). */
static void load_pal(const char*spec,u6pal_t*pal){
    char path[512]; int selp=0; const char*c=strrchr(spec,':');
    if(c && c>spec+1){ size_t n=(size_t)(c-spec); if(n>=sizeof path) n=sizeof path-1;
        memcpy(path,spec,n); path[n]=0; selp=atoi(c+1); }
    else { snprintf(path,sizeof path,"%s",spec); }
    long pn; uint8_t*pb=slurp(path,&pn);
    /* PALETTES.INT: 8 palettes of 0x240 bytes, each 256 colours x 3 chan x 6 bits,
     * BIT-PACKED (not byte-aligned), then value<<2 to 8-bit. (per nuvie
     * ScriptCutscene::set_palette). Detected by the .INT extension. */
    const char*dot=strrchr(path,'.');
    if(dot && (dot[1]=='i'||dot[1]=='I') && (dot[2]=='n'||dot[2]=='N') && (dot[3]=='t'||dot[3]=='T')){
        const uint8_t*buf=pb+(long)selp*0x240;
        if((long)selp*0x240+0x240>pn){ fprintf(stderr,"palette %d out of range\n",selp); exit(2); }
        for(int i=0;i<256;i++) for(int j=0;j<3;j++){
            int bit=i*18+j*6, bp=bit/8, sh=bit%8;
            int c=((buf[bp]+(buf[bp+1]<<8))>>sh)&0x3F;
            uint8_t v=(uint8_t)(c<<2);
            if(j==0) pal->color[i].r=v; else if(j==1) pal->color[i].g=v; else pal->color[i].b=v; }
        fprintf(stderr,"palette %s sub %d: 6-bit bit-packed (INT)\n",path,selp);
        return;
    }
    const uint8_t*p=pb+(long)selp*768;
    if((long)selp*768+768>pn){ fprintf(stderr,"palette %d out of range\n",selp); exit(2); }
    int sixbit=1; for(int i=0;i<768;i++) if(p[i]>63){ sixbit=0; break; }
    for(int i=0;i<256;i++){ int r=p[i*3],g=p[i*3+1],b=p[i*3+2];
        if(sixbit){ r=r*255/63; g=g*255/63; b=b*255/63; }
        pal->color[i].r=(uint8_t)r; pal->color[i].g=(uint8_t)g; pal->color[i].b=(uint8_t)b; }
    fprintf(stderr,"palette %s sub %d: %s\n",path,selp,sixbit?"6-bit":"8-bit");
}

int main(int argc,char**argv){
    if(argc<5){ fprintf(stderr,"usage: introcomp <file.shp> <pal[:k]> <out.png> <idx>...\n"); return 2; }
    long fn; uint8_t*f=slurp(argv[1],&fn);
    int32_t un=u6lzw_unpacked_size(f,(int32_t)fn); uint8_t*d; int32_t dl;
    if(un>0){ d=malloc((size_t)un); dl=u6lzw_decompress(f,(int32_t)fn,d,un); } else { d=f; dl=(int32_t)fn; }
    u6pal_t pal; load_pal(argv[2],&pal);
    static uint8_t canvas[CW*CH];
    for(int i=0;i<CW*CH;i++) canvas[i]=0;
    for(int a=4;a<argc;a++){ int idx=0,ox=0,oy=0;
        sscanf(argv[a],"%d,%d,%d",&idx,&ox,&oy);   /* "idx" or "idx,dx,dy" */
        paint(d,dl,idx,ox,oy,canvas); }
    uint8_t*img=malloc(CW*CH*3);
    for(int i=0;i<CW*CH;i++){ u6_rgb c=pal.color[canvas[i]]; img[i*3]=c.r; img[i*3+1]=c.g; img[i*3+2]=c.b; }
    png_write_rgb(argv[3],CW,CH,img);
    printf("wrote %s\n",argv[3]);
    return 0;
}
