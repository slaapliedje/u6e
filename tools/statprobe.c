// SPDX-License-Identifier: GPL-2.0-or-later
/* statprobe.c — host diagnostic: dump the U6 objlist's per-actor arrays so we can
 * identify the stat offsets empirically (clean-room: derive from the user's own
 * data, not from anyone's source). Prints, for the first few actor slots, the
 * 1-byte value at each 0x100-aligned array base, plus 2-byte LE candidates.
 * Usage: statprobe <objlist>            (e.g. build/svm.objlist)
 * Known: actors 1=Avatar 2=Dupre 3=Shamino 4=Iolo; ScummVM shows 90 for all 4.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc,char**argv){
    if(argc<2){ printf("usage: %s <objlist>\n",argv[0]); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("cannot open %s\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n) return 2; fclose(f);
    printf("objlist %ld bytes (0x%lx)\n\n", n, n);

    /* 1-byte arrays: value at base+actor for actors 0..7 */
    printf("offset   a0  a1  a2  a3  a4  a5  a6  a7   (a1..a4 = Avatar,Dupre,Shamino,Iolo)\n");
    for(long base=0x000; base<n; base+=0x100){
        if(base+8>n) break;
        /* skip the two multi-byte arrays we already understand */
        if(base>=0x100 && base<0x400){ if(base==0x100) printf("0x100-3ff  <positions, 3 bytes/actor>\n"); continue; }
        if(base>=0x400 && base<0x600){ if(base==0x400) printf("0x400-5ff  <appearance, 2 bytes/actor>\n"); continue; }
        printf("0x%04lx ", base);
        for(int a=0;a<8;a++) printf(" %3u", d[base+a]);
        /* flag a strong hit: actors 1..4 all == 90 (the displayed number) */
        if(d[base+1]==90&&d[base+2]==90&&d[base+3]==90&&d[base+4]==90) printf("   <== 90,90,90,90");
        printf("\n");
    }

    /* 2-byte LE arrays (e.g. experience): value at base+actor*2 */
    printf("\n2-byte LE candidates (base + actor*2):\n");
    for(long base=0xC00; base<=0x1800 && base+16<=n; base+=0x100){
        printf("0x%04lx ", base);
        for(int a=0;a<8;a++){ unsigned v=d[base+a*2]|(d[base+a*2+1]<<8); printf(" %5u", v); }
        printf("\n");
    }

    /* which actors have status flag 0x800 == 0xC0 (candidate "in party")? */
    printf("\n0x800 flag scan: actors with value 0xC0:");
    for(int a=0;a<256;a++) if(d[0x800+a]==0xC0) printf(" %d", a);
    printf("\n0x800 distinct nonzero values:");
    for(int v=1;v<256;v++){ int c=0; for(int a=0;a<256;a++) if(d[0x800+a]==v) c++; if(c) printf(" 0x%02x:%d",v,c); }
    printf("\n");

    /* hunt for a party-order list: a run of small ascending actor IDs (1,2,3,4...) */
    printf("\nsearching for byte run 01 02 03 04 (party-order list):");
    for(long o=0;o+4<=n;o++) if(d[o]==1&&d[o+1]==2&&d[o+2]==3&&d[o+3]==4) printf(" @0x%lx",o);
    printf("\n");
    return 0;
}
