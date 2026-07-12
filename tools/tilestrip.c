// SPDX-License-Identifier: GPL-2.0-or-later
/* tilestrip.c — host diagnostic: render a list of tile indices from a built world
 * pack (build/u6.world) as a horizontal PNG strip, to eyeball animation frames.
 * Reuses the pack's already-decoded 2048-tile set + palette (no VGA re-decode).
 * Usage: tilestrip <u6.world> <out.png> <tile> [tile ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "png.h"

static uint32_t be32(const uint8_t*p){ return ((uint32_t)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }

int main(int argc,char**argv){
    if(argc<4){ printf("usage: %s <u6.world> <out.png> <tile>...\n",argv[0]); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){ printf("open %s failed\n",argv[1]); return 2; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n) return 2; fclose(f);

    uint32_t map_len=be32(d+4), chunks_len=be32(d+8), ntiles=be32(d+12);
    const uint8_t *tileset=d+24+map_len+chunks_len;
    const uint8_t *pal=tileset+(long)ntiles*256;            /* 768 bytes RGB6 */

    int ntile=argc-3, scale=4, tw=16*scale;
    int W=ntile*tw, H=tw;
    uint8_t *rgb=calloc((size_t)W*H,3);
    for(int k=0;k<ntile;k++){
        long t=atol(argv[3+k]); if(t<0||t>=(long)ntiles) continue;
        const uint8_t*src=tileset+t*256;
        for(int yy=0;yy<tw;yy++)for(int xx=0;xx<tw;xx++){
            uint8_t ci=src[(yy/scale)*16+(xx/scale)];
            const uint8_t*c=pal+ci*3;
            uint8_t*o=rgb+(((long)yy*W)+(k*tw+xx))*3;
            o[0]=(uint8_t)((c[0]*255+31)/63); o[1]=(uint8_t)((c[1]*255+31)/63); o[2]=(uint8_t)((c[2]*255+31)/63);
        }
    }
    if(png_write_rgb(argv[2],W,H,rgb)!=0){ printf("png write failed\n"); return 2; }
    printf("wrote %s (%d tiles, %dx%d)\n", argv[2], ntile, W, H);
    return 0;
}
