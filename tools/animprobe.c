/* animprobe.c — host diagnostic: decode U6 ANIMDATA into its parallel arrays so we
 * can work out the tile-animation mechanism (clean-room, from our own bytes).
 * Layout hypothesis (194 bytes): u16 count, then 32-entry arrays
 *   tile[32] u16 (animated tile), B[32] u16, C[32] u8, D[32] u8.
 * Usage: animprobe <ANIMDATA>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static uint16_t rd2(const uint8_t*p){ return (uint16_t)(p[0]|(p[1]<<8)); }

int main(int argc,char**argv){
    if(argc<2){ printf("usage: %s <ANIMDATA>\n",argv[0]); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("cannot open %s\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n) return 2; fclose(f);
    printf("ANIMDATA %ld bytes\n", n);

    int count=rd2(d);
    const uint8_t *tile=d+2, *B=d+2+32*2, *C=d+2+32*2+32*2, *D=d+2+32*2+32*2+32;
    printf("count=%d (active animated tiles)\n\n", count);
    printf(" i  tile   B(u16)  C(u8) D(u8)   notes\n");
    for(int i=0;i<32;i++){
        unsigned t=rd2(tile+i*2), b=rd2(B+i*2);
        int c=C[i], dd=D[i];
        printf("%2d  %4u   %5u    %3d   %3d   %s%s\n", i, t, b, c, dd,
               i<count?"":"(inactive) ",
               (i>0 && t==rd2(tile+(i-1)*2)+1)?"<consecutive tile>":"");
    }
    /* group consecutive tile runs (likely one multi-frame animation each) */
    printf("\nconsecutive tile runs in tile[]:\n");
    for(int i=0;i<count;){ int j=i;
        while(j+1<count && rd2(tile+(j+1)*2)==rd2(tile+j*2)+1) j++;
        if(j>i) printf("  tiles %u..%u (%d frames)\n", rd2(tile+i*2), rd2(tile+j*2), j-i+1);
        i=j+1;
    }
    return 0;
}
