// SPDX-License-Identifier: GPL-2.0-or-later
/* mdump.c — HOST analysis tool: decompress a U6 .M music file (LZW container) and DISASSEMBLE the
 * OPL2 command stream, to pin docs/formats/music.md. Clean-room: format UNDERSTOOD by reading the
 * AdPlug u6m player + our own byte inspection; this disassembler is written from that understanding,
 * not transcribed. Validation = a byte-accurate walk consumes the whole stream and ends cleanly.
 *
 * Command byte = high nibble opcode, low nibble channel (0..8). Operand counts:
 *   0x0n/1n/2n freq(1)  3n/4n vol(1)  5n porta(1)  6n vib(1)  7n instr(1)
 *   0x81 branch(3)  0x82 delay(1)  0x83 definst(1+11)  0x85/86 slide(1)
 *   0xEn looppoint(0)  0xFn return(0). Timing: 0x82 delay counts 60 Hz ticks.
 * Usage: mdump [-q] FILE.M   (-q = summary only)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../dataformats/u6lzw.h"

static uint8_t *slurp(const char *p, long *n){
    FILE *f=fopen(p,"rb"); if(!f){ perror(p); exit(1);}
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *b=malloc(*n); if(fread(b,1,*n,f)!=(size_t)*n){ exit(1);} fclose(f); return b;
}

int main(int argc, char**argv){
    int quiet=0, ai=1;
    if(argc>1 && !strcmp(argv[1],"-q")){ quiet=1; ai=2; }
    if(argc<=ai){ fprintf(stderr,"usage: %s [-q] FILE.M\n",argv[0]); return 1; }
    long n; uint8_t *src=slurp(argv[ai],&n);
    int32_t usz=u6lzw_unpacked_size(src,(int32_t)n);
    int32_t cap = usz>0 ? usz : (int32_t)n;
    uint8_t *d=malloc(cap+16);
    int32_t got=u6lzw_decompress(src,(int32_t)n,d,cap);
    printf("== %s ==  file %ld B, decompressed %d B (declared %d)\n", argv[ai], n, got, usz);
    if(got<=0){ printf("DECODE FAILED\n"); return 2; }

    /* Byte-accurate walk. We disassemble LINEARLY (ignore branch jumps — we just want to prove the
     * operand counts keep us aligned to the end). Count commands, notes, and total 60 Hz ticks. */
    int32_t p=0; long ticks=0; int ncmd=0, nnote=0, ndef=0, ninst[9]={0}, desync=0;
    int op_hist[16]={0};
    while(p<got){
        uint8_t b=d[p]; int hi=b>>4, lo=b&0xF; int32_t start=p; p++;
        op_hist[hi]++;
        int need=0;                                   /* operand bytes for this command */
        const char *name="?";
        switch(hi){
          case 0x0: need=1; name="noteoff  freq"; break;
          case 0x1: need=1; name="noteoff+on freq"; nnote++; break;
          case 0x2: need=1; name="noteon   freq"; nnote++; break;
          case 0x3: need=1; name="vol.carrier"; break;
          case 0x4: need=1; name="vol.modul "; break;
          case 0x5: need=1; name="portamento"; break;
          case 0x6: need=1; name="vibrato   "; break;
          case 0x7: need=1; name="set instr "; if(lo<9) ninst[lo]++; break;
          case 0x8:
            switch(b){
              case 0x81: need=3; name="BRANCH subsong"; break;
              case 0x82: need=1; name="DELAY ticks"; break;
              case 0x83: need=1+11; name="DEFINE instrument"; ndef++; break;
              case 0x85: need=1; name="volslide up"; break;
              case 0x86: need=1; name="volslide down"; break;
              default:   need=0; name="(8x unhandled)"; break;
            } break;
          case 0xE: need=0; name="LOOP point"; break;
          case 0xF: need=0; name="RETURN/loopend"; break;
          default:  need=0; name="(9-D UNDEFINED)"; desync++; break;
        }
        if(p+need>got){ if(!quiet) printf("  @%-5d %02x %-16s <TRUNCATED, need %d>\n",start,b,name,need); p=got; break; }
        if(hi==0x82>>4){} /* noop */
        if(b==0x82) ticks += d[p];                    /* accumulate real-time ticks */
        if(!quiet){
            printf("  @%-5d t=%-6ld %02x %-16s ch%d", start, ticks, b, name, lo);
            for(int k=0;k<need;k++) printf(" %02x", d[p+k]);
            printf("\n");
        }
        p+=need; ncmd++;
        if(desync>3){ if(!quiet) printf("  ... too many undefined opcodes, stopping\n"); break; }
    }
    printf("SUMMARY %s: %d cmds, %d notes, %d instr-defs, %ld ticks (~%.1fs @60Hz), consumed %d/%d B%s%s\n",
           argv[ai], ncmd, nnote, ndef, ticks, ticks/60.0, p, got,
           (p==got)?" [CLEAN END]":" [DID NOT REACH EOF]", desync?" [DESYNC!]":"");
    printf("  opcode-class histogram (hi nibble 0..F): ");
    for(int i=0;i<16;i++) if(op_hist[i]) printf("%X:%d ", i, op_hist[i]);
    printf("\n  instrument-assign per channel 0..8: ");
    for(int i=0;i<9;i++) printf("%d ", ninst[i]);
    printf("\n");
    free(src); free(d);
    return 0;
}
