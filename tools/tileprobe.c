/* tileprobe.c — host diagnostic: load the world pack and, for the 11x11 map view
 * around a coordinate, print each cell's NUMBER (1..121, left->right top->bottom),
 * world coords, terrain tile index, and any object there. Helps map a user's
 * "position N looks wrong" report to the actual tile/obj being drawn.
 * Usage: tileprobe <u6.world> [px py]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6map.h"

static uint32_t be32(const uint8_t*p){return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}
static uint16_t be16(const uint8_t*p){return (p[0]<<8)|p[1];}

int main(int argc,char**argv)
{
    if(argc<2){printf("usage: %s <u6.world> [px py]\n",argv[0]);return 2;}
    FILE*f=fopen(argv[1],"rb"); if(!f){printf("cannot open\n");return 2;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n){return 2;} fclose(f);
    if(memcmp(d,"U6W",3)){printf("not a U6W pack (got %.4s)\n",d);return 2;}
    uint32_t map_len=be32(d+4), chunks_len=be32(d+8), ntiles=be32(d+12), nobjs=be32(d+16);
    int px=argc>3?atoi(argv[2]):315, py=argc>3?atoi(argv[3]):367;
    const uint8_t*map=d+24, *chunks=map+map_len, *tileset=chunks+chunks_len;
    const uint8_t*objs=tileset+ntiles*256+768+256;     /* after palette+passable */
    u6map_t m; if(!u6map_load(&m,map,map_len,chunks,chunks_len)){printf("u6map_load failed\n");return 2;}

    printf("pack: %u map, %u chunks, %u tiles, %u objs; view @ (%d,%d)\n",
           map_len,chunks_len,ntiles,nobjs,px,py);
    int left=px-5, top=py-5;
    for(int row=0;row<11;row++){
        for(int col=0;col<11;col++){
            int wx=(left+col)&1023, wy=(top+row)&1023;
            int pos=row*11+col+1;
            int tile=u6map_surface_tile(&m,wx,wy);
            /* object(s) at this cell */
            int otile=-1,on=-1;
            for(uint32_t i=0;i<nobjs;i++){const uint8_t*o=objs+i*8;
                if(be16(o)==wx&&be16(o+2)==wy){ otile=be16(o+4); on=be16(o+6); break; }}
            printf("  #%-3d (%d,%d) ground=%-3d", pos, wx, wy, tile);
            if(on>=0) printf("  OBJ obj_n=%d tile=%d", on, otile);
            printf("\n");
        }
    }
    return 0;
}
