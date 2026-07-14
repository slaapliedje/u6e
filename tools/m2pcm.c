// SPDX-License-Identifier: GPL-2.0-or-later
/* m2pcm.c — HOST pre-renderer for the M10 OPL2->DMA music backend.
 *
 * Real-time FM synthesis is far too slow on the 68000 (measured: even a 32 MHz TT
 * is ~10x short; see `make sbench`). So we render each U6 `.M` to PCM ONCE here on
 * the dev PC, using the SAME engine synth (engine/oplsynth.c) at 4x oversampling
 * for the clean AdLib sound, and the Atari just STREAMS the PCM to its DMA sound
 * chip at ~0% CPU. Output = signed 8-bit mono (what STE/TT DMA sound plays).
 *
 * On-disk format (all multi-byte fields BIG-ENDIAN — the engine is big-endian 68000):
 *   0  char[4] "U6PC"
 *   4  u32     rate (samples/sec)
 *   8  u32     nframes (number of 8-bit samples that follow)
 *   12 u32     loop_start (frame to loop back to; 0 = loop the whole clip)
 *   16 int8[nframes]  signed 8-bit mono PCM
 *
 * Clean-room: renders the user's own `.M`; ships NO game data (PCM of the
 * copyrighted score -> build/, gitignored). Usage: m2pcm FILE.M OUT.PCM [secs] [rate]
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
static void put_be32(FILE *f, uint32_t v){ uint8_t b[4]={v>>24,v>>16,v>>8,v}; fwrite(b,1,4,f); }

int main(int argc, char**argv){
    if(argc<3){ fprintf(stderr,"usage: %s FILE.M OUT.PCM [seconds] [rate]\n",argv[0]); return 1; }
    double secs = argc>3 ? atof(argv[3]) : 30.0;
    int32_t rate = argc>4 ? atoi(argv[4]) : 12517;   /* STE native DMA rate      */
    const int OS = 4;                                /* 4x = the clean host sound */

    long n; uint8_t *src=slurp(argv[1],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n), cap=usz>0?usz:(int32_t)n;
    uint8_t *d=malloc(cap+16);
    int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    if(got<=0){ fprintf(stderr,"decode failed\n"); return 2; }

    long total = (long)(secs * rate);
    int16_t *pcm16 = malloc(total * sizeof *pcm16);
    int8_t  *pcm8  = malloc(total);
    opl_start(d, got, rate, OS);
    /* pass 1: render the whole clip to int16, find the peak */
    long peak16 = 1;
    for(long off=0; off<total; ){
        int blk = (total-off > 4096) ? 4096 : (int)(total-off);
        opl_render(pcm16+off, blk);
        for(int i=0;i<blk;i++){ int a = pcm16[off+i]; if(a<0)a=-a; if(a>peak16) peak16=a; }
        off += blk;
    }
    /* pass 2: per-song peak-normalize into the full signed-8-bit range (target
     * peak ~120 to leave headroom). FM synth silence is exact 0, so lifting a
     * quiet song's level just uses the 8-bit range better -- no noise to amplify.
     * Gain is capped so a near-silent file isn't blown up absurdly. */
    double gain = 120.0 * 256.0 / (double)peak16;
    if(gain > 64.0) gain = 64.0;
    if(gain < 1.0) gain = 1.0;
    long peak = 0;
    for(long i=0;i<total;i++){
        int v = (int)(pcm16[i] * gain / 256.0);
        if(v>127)v=127; if(v<-128)v=-128;
        pcm8[i]=(int8_t)v; int a=v<0?-v:v; if(a>peak) peak=a;
    }

    FILE *o=fopen(argv[2],"wb"); if(!o){ perror(argv[2]); return 3; }
    fwrite("U6PC",1,4,o); put_be32(o,(uint32_t)rate); put_be32(o,(uint32_t)total); put_be32(o,0);
    fwrite(pcm8,1,total,o); fclose(o);
    printf("%s -> %s : %ld frames (%.1fs @ %d Hz, 8-bit signed mono, 4x OS), peak %ld/127\n",
           argv[1], argv[2], total, (double)total/rate, rate, peak);
    free(src); free(d); free(pcm16); free(pcm8);
    return 0;
}
