// SPDX-License-Identifier: GPL-2.0-or-later
/* objprobe.c — host diagnostic: analyse the 64 surface super-chunks of a ScummVM
 * U6 save to discover (a) the super-chunk partitioning formula (which chunk holds
 * an object at x,y) and (b) how contained objects (status != on-map) are grouped.
 * Clean-room: derived from the user's own save, not from anyone's source.
 * Usage: objprobe <save.000>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static uint16_t rd2(const uint8_t*p){ return (uint16_t)(p[0]|(p[1]<<8)); }

int main(int argc,char**argv){
    if(argc<2){ printf("usage: %s <save.000>\n",argv[0]); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("cannot open %s\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n) return 2; fclose(f);

    long pos=4;
    for(int k=0;k<2;k++){ uint16_t c=rd2(d+pos); pos+=2+(long)c*8; }  /* inv + eggs */

    long total=0; int status_hist[4]={0,0,0,0};
    long fmt_match=0, fmt_total=0; int shown_mm=0, shown_cont=0;
    int prev_chunk_of_container=-1;
    for(int chunk=0;chunk<64;chunk++){
        uint16_t cnt=rd2(d+pos); pos+=2;
        const uint8_t*rec=d+pos;
        if(cnt) printf("chunk %2d (col %d,row %d): %4u objs\n", chunk, chunk%8, chunk/8, cnt);
        for(int i=0;i<cnt;i++){
            const uint8_t*r=rec+i*8;
            int status=r[0]&3;
            int x=r[1]|((r[2]&3)<<8);
            int y=((r[2]&0xfc)>>2)|((r[3]&0xf)<<6);
            int z=(r[3]&0xf0)>>4;
            int obj_n=r[4]|((r[5]&3)<<8);
            status_hist[status]++; total++;
            if(status==0){
                int formula=(y>>7)*8+(x>>7);
                fmt_total++;
                if(formula==chunk){ fmt_match++; prev_chunk_of_container=chunk; }
                else if(shown_mm<15){ printf("  MISMATCH: in chunk %d but (x%d,y%d,z%d)->chunk %d obj_n %d\n",
                                              chunk,x,y,z,formula,obj_n); shown_mm++; }
            } else {
                if(shown_cont<10){ printf("  contained(status%d): x%d y%d z%d obj_n %d  (chunk-by-xy=%d)\n",
                                          status,x,y,z,obj_n,(y>>7)*8+(x>>7)); shown_cont++; }
            }
        }
        pos+=(long)cnt*8;
    }
    (void)prev_chunk_of_container;
    printf("\ntotal=%ld  status: map=%d container=%d inv=%d readied=%d\n",
           total,status_hist[0],status_hist[1],status_hist[2],status_hist[3]);
    printf("partition  chunk == (y>>7)*8 + (x>>7):  %ld/%ld map objs match (%ld mismatch)\n",
           fmt_match, fmt_total, fmt_total-fmt_match);
    return 0;
}
