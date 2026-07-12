/* palhist.c — throwaway: decode a SHAPE and print a palette-index histogram,
 * optionally restricted to a y-band. Used to find the gold menu-text indices so
 * the front-end can color-cycle the selected option. Build: see Makefile palhist. */
#include <stdio.h>
#include <stdlib.h>
#include "u6shape.h"

int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"usage: palhist <file.shp> <idx> [y0 y1]\n"); return 2; }
    const char*fn=argv[1]; int idx=atoi(argv[2]);
    long len; FILE*f=fopen(fn,"rb"); if(!f){perror(fn);return 2;}
    fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*raw=malloc(len); if(fread(raw,1,len,f)!=(size_t)len){return 2;} fclose(f);
    /* whole-file LZW first (shapes are packed) */
    extern int32_t u6lzw_unpacked_size(const uint8_t*,int32_t);
    extern int32_t u6lzw_decompress(const uint8_t*,int32_t,uint8_t*,int32_t);
    int32_t usz=u6lzw_unpacked_size(raw,(int32_t)len);
    uint8_t*d; int32_t dl;
    if(usz>0){ d=malloc(usz); u6lzw_decompress(raw,(int32_t)len,d,usz); dl=usz; }
    else { d=raw; dl=(int32_t)len; }
    int w=0,h=0; static uint8_t px[2048*2048];
    if(!u6shape_decode(d,dl,idx,&w,&h,px,sizeof px)){ fprintf(stderr,"decode failed\n"); return 2; }
    int y0=0,y1=h; if(argc>=5){ y0=atoi(argv[3]); y1=atoi(argv[4]); }
    long hist[256]={0};
    for(int y=y0;y<y1&&y<h;y++) for(int x=0;x<w;x++) hist[px[y*w+x]]++;
    printf("shape %d: %dx%d, band y=%d..%d\n",idx,w,h,y0,y1);
    for(int i=0;i<256;i++) if(hist[i]) printf("  idx %3d: %ld\n",i,hist[i]);
    return 0;
}
