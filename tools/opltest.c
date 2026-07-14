// SPDX-License-Identifier: GPL-2.0-or-later
/* opltest.c — HOST test for the ON-TARGET OPL2 FM synth (engine/oplsynth.c).
 * Renders a .M to WAV through the SAME engine code the 68000 will run (integer,
 * no libm), so we can confirm it reproduces the validated host proof (tools/m2wav)
 * before wiring it to DMA sound. Ships no game data (WAV -> build/, gitignored).
 * Usage: opltest FILE.M OUT.wav [seconds] [rate] [oversample]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../dataformats/u6lzw.h"
#include "../engine/oplsynth.h"

static uint8_t *slurp(const char *p, long *n){
    FILE *f=fopen(p,"rb"); if(!f){ perror(p); exit(1);}
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc(*n); if(fread(b,1,*n,f)!=(size_t)*n) exit(1); fclose(f); return b;
}

int main(int argc, char**argv){
    if(argc<3){ fprintf(stderr,"usage: %s FILE.M OUT.wav [seconds] [rate] [oversample]\n",argv[0]); return 1; }
    double secs = argc>3 ? atof(argv[3]) : 30.0;
    int32_t rate = argc>4 ? atoi(argv[4]) : 49716;
    int os = argc>5 ? atoi(argv[5]) : 4;

    long n; uint8_t *src=slurp(argv[1],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n), cap=usz>0?usz:(int32_t)n;
    uint8_t *d=malloc(cap+16);
    int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    if(got<=0){ fprintf(stderr,"decode failed\n"); return 2; }

    long total = (long)(secs * rate);
    int16_t *pcm = malloc(total * sizeof *pcm);
    opl_start(d, got, rate, os);
    /* render in blocks, like the DMA fill will */
    for(long off=0; off<total; ){
        int blk = (total-off > 4096) ? 4096 : (int)(total-off);
        opl_render(pcm+off, blk); off += blk;
    }

    FILE *o=fopen(argv[2],"wb"); if(!o){ perror(argv[2]); return 3; }
    long bytes = total*2; uint32_t chunk=36+bytes, byterate=rate*2;
    uint8_t h[44]={'R','I','F','F', chunk,chunk>>8,chunk>>16,chunk>>24, 'W','A','V','E',
      'f','m','t',' ', 16,0,0,0, 1,0, 1,0, rate&0xFF,(rate>>8)&0xFF,(rate>>16)&0xFF,(rate>>24)&0xFF,
      byterate&0xFF,(byterate>>8)&0xFF,(byterate>>16)&0xFF,(byterate>>24)&0xFF, 2,0, 16,0,
      'd','a','t','a', bytes,bytes>>8,bytes>>16,bytes>>24};
    fwrite(h,1,44,o); fwrite(pcm,2,total,o); fclose(o);
    printf("%s -> %s : %ld samples (%.1fs @ %d Hz, %dx OS), engine oplsynth\n",
           argv[1], argv[2], total, (double)total/rate, rate, os);
    free(src); free(d); free(pcm);
    return 0;
}
