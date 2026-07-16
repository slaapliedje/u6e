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
 *   12 u32     loop_start (frame the DMA loops back to; 0 = loop the whole clip)
 *   16 int8[nframes]  signed 8-bit mono PCM
 *
 * SEAMLESS LOOP: a U6 `.M` is an intro (played once) then a body that repeats
 * from a loop point (opcode 0xE, or the top). We render intro + TWO passes of
 * the body and set loop_start to the start of the SECOND body. The DMA plays the
 * whole clip once, then loops just that second body — whose synth state (envelope
 * phases etc.) is warmed up by the first pass, so it matches the state at the end
 * of the body and the loop seam has no click. The loop boundaries come from the
 * synth's opl_wrap_* introspection (engine/oplsynth.h).
 *
 * Clean-room: renders the user's own `.M`; ships NO game data (PCM of the
 * copyrighted score -> build/, gitignored). Usage: m2pcm FILE.M OUT.PCM [maxsecs] [rate]
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
    if(argc<3){ fprintf(stderr,"usage: %s FILE.M OUT.PCM [maxseconds] [rate]\n",argv[0]); return 1; }
    double maxsecs = argc>3 ? atof(argv[3]) : 240.0;  /* SAFETY CAP, not a fixed length */
    int32_t rate = argc>4 ? atoi(argv[4]) : 12517;   /* STE native DMA rate      */
    const int OS = 4;                                /* 4x = the clean host sound */

    long n; uint8_t *src=slurp(argv[1],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n), cap=usz>0?usz:(int32_t)n;
    uint8_t *d=malloc(cap+16);
    int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    if(got<=0){ fprintf(stderr,"decode failed\n"); return 2; }

    long capframes = (long)(maxsecs * rate);
    int16_t *pcm16 = malloc(capframes * sizeof *pcm16);
    int8_t  *pcm8  = malloc(capframes);
    if(!pcm16 || !pcm8){ fprintf(stderr,"out of memory\n"); return 4; }

    opl_start(d, got, rate, OS);
    /* Render until playback has returned to its loop point TWICE (intro + two
     * passes of the loop body) or we hit the safety cap. w1/w2 = the sample where
     * the 1st/2nd wrap happened; body2 = [w1, w2). */
    long off = 0, w1 = -1, w2 = -1;
    while(off < capframes){
        int blk = (capframes-off > 4096) ? 4096 : (int)(capframes-off);
        opl_render(pcm16+off, blk);
        off += blk;
        int wc = opl_wrap_count();
        if(w1 < 0 && wc >= 1) w1 = opl_wrap_sample(1);
        if(w2 < 0 && wc >= 2){ w2 = opl_wrap_sample(2); break; }
        if(!opl_playing()) break;            /* one-shot song stopped: trim tail   */
    }

    /* Decide the clip length + loop point. Best case = both wraps seen: loop the
     * warmed-up second body [w1,w2). If only one body fits the cap, trim EXACTLY at
     * the first wrap and loop the whole body from its loop point (musically aligned,
     * just no envelope warmup) — never an arbitrary tail. If the song never wrapped
     * at all within the cap, loop the whole clip. */
    long total, loop_start; const char *loopdesc;
    if(w2 > 0){        total = w2; loop_start = w1;                     loopdesc = "loop=body2 (warmed, seamless)"; }
    else if(w1 > 0){   total = w1; loop_start = opl_loop_point_sample(); loopdesc = "loop=body1 (aligned, no warmup)"; }
    else {             total = off; loop_start = 0;                     loopdesc = "loop=whole clip (no loop point)"; }

    /* pass 1: per-song peak over the final clip */
    long peak16 = 1;
    for(long i=0;i<total;i++){ int a=pcm16[i]; if(a<0)a=-a; if(a>peak16) peak16=a; }
    /* pass 2: peak-normalize into the full signed-8-bit range (target peak ~120 to
     * leave headroom). FM synth silence is exact 0, so lifting a quiet song's level
     * just uses the 8-bit range better -- no noise to amplify. Gain is capped so a
     * near-silent file isn't blown up absurdly. */
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
    fwrite("U6PC",1,4,o); put_be32(o,(uint32_t)rate); put_be32(o,(uint32_t)total);
    put_be32(o,(uint32_t)loop_start);
    fwrite(pcm8,1,total,o); fclose(o);
    printf("%s -> %s : %ld frames (%.1fs @ %d Hz, 8-bit signed mono, 4x OS), "
           "loop_start=%ld (%.1fs), %s, peak %ld/127\n",
           argv[1], argv[2], total, (double)total/rate, rate,
           loop_start, (double)loop_start/rate, loopdesc, peak);
    free(src); free(d); free(pcm16); free(pcm8);
    return 0;
}
