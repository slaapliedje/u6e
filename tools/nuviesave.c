// SPDX-License-Identifier: GPL-2.0-or-later
/* nuviesave.c — host: decode a ScummVM/Nuvie U6 save (see docs/formats/nuvie_save.md).
 * Verifies the layout and extracts the 64 surface super-chunks (== an OBJBLK
 * stream) + the objlist, so mkworld can build the world from a ScummVM save state.
 * Usage: nuviesave <save.000> [out.objblk] [out.objlist]
 * All multi-byte values are little-endian (ScummVM/DOS lineage).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint16_t rd2(const uint8_t*p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t rd4(const uint8_t*p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

/* advance past one super-chunk ([u16 count][count*8]); returns object count */
static long skip_chunk(const uint8_t*d,long n,long*pos){
    if(*pos+2>n) { *pos=n; return 0; }
    uint16_t c=rd2(d+*pos); long need=2+(long)c*8;
    if(*pos+need>n){ *pos=n; return -1; }
    *pos+=need; return c;
}

int main(int argc,char**argv){
    if(argc<2){ printf("usage: %s <save.000> [out.objblk] [out.objlist]\n",argv[0]); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("cannot open %s\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n){ return 2; } fclose(f);

    long pos=0;
    uint16_t version=rd2(d+pos); pos+=2;
    uint16_t gt=rd2(d+pos); pos+=2;
    printf("save %ld bytes: version=%u (expect 256=v1.0), game_tag=%u\n", n, version, gt);

    long inv_start=pos;                            /* actor-inventory chunk (incl readied gear) */
    long inv=skip_chunk(d,n,&pos);
    long inv_end=pos;
    long eggs=skip_chunk(d,n,&pos);                /* eggs */
    printf("inventory objs=%ld, egg objs=%ld\n", inv, eggs);

    long surf_start=pos, total=0;
    for(int i=0;i<64;i++){ long c=skip_chunk(d,n,&pos); if(c<0){printf("surface chunk %d overran\n",i);return 2;} total+=c; }
    long surf_end=pos;
    for(int i=0;i<5;i++) skip_chunk(d,n,&pos);     /* dungeon levels */

    if(pos+4>n){ printf("no objlist size (pos=%ld n=%ld)\n",pos,n); return 2; }
    uint32_t objlist_size=rd4(d+pos); pos+=4;
    const uint8_t*objlist=d+pos;
    printf("surface: %ld objs in %ld bytes (chunks @ %ld..%ld); objlist_size=%u (avail %ld)\n",
           total, surf_end-surf_start, surf_start, surf_end, objlist_size, n-pos);

    /* sanity: decode a few actor appearances from the objlist (LITTLE-endian) */
    if(pos+0x400+12<=n){
        printf("actors (objlist 0x400, LE): ");
        for(int i=0;i<6;i++){ const uint8_t*q=objlist+0x400+i*2;
            int objn=q[0]|((q[1]&3)<<8); int fr=(q[1]&0xfc)>>2;
            printf("a%d=obj_n %d(f%d) ", i, objn, fr); }
        printf("\n  (avatar should be actor 1 = obj_n 410)\n");
    }

    /* extract the surface super-chunks as an OBJBLK stream + the objlist */
    if(argc>2){ FILE*o=fopen(argv[2],"wb"); if(o){ fwrite(d+surf_start,1,surf_end-surf_start,o); fclose(o);
        printf("wrote %s (%ld bytes, surface OBJBLK stream)\n", argv[2], surf_end-surf_start); } }
    if(argc>3){ FILE*o=fopen(argv[3],"wb"); if(o){ fwrite(objlist,1,objlist_size<=(uint32_t)(n-pos)?objlist_size:(uint32_t)(n-pos),o); fclose(o);
        printf("wrote %s (%u bytes, objlist)\n", argv[3], objlist_size); } }
    if(argc>4){ FILE*o=fopen(argv[4],"wb"); if(o){ fwrite(d+inv_start,1,inv_end-inv_start,o); fclose(o);
        printf("wrote %s (%ld bytes, actor-inventory chunk)\n", argv[4], inv_end-inv_start); } }
    free(d);
    return 0;
}
