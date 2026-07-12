/* shapedump.c — HOST: decode a U6 SHAPE file (*.shp: MAINMENU/TITLES/INTRO/GYPSY)
 * to PNG(s), to validate the format decode (docs/formats/shapes.md).
 *
 * Format (derived from u6tech.txt + our own analysis, clean-room):
 *  whole-file LZW; decompressed:
 *    [u32 LE total_size][N x u32 LE offsets]  (N = (offset[0]-4)/4; offset[i] = byte
 *    position of shape i from the start). Each shape:
 *    [u16 rightX][u16 leftX][u16 upperY][u16 lowerY]  (LE; W=lX+rX+1, H=uY+lY+1,
 *    hotspot at (leftX,upperY)), then pixel blocks:
 *    [u16 dl][s16 x][s16 y][data]  len=dl>>1, flag=dl&1 (1=RLE); the run starts at
 *    (leftX+x, upperY+y) and lays `len` pixels rightward. RLE byte b: count=b>>1;
 *    b&1 => repeat the next 1 pixel `count`x, else `count` literal pixels. dl==0 ends.
 *
 * Usage: shapedump <file.shp> <U6PAL> <out-prefix>   (writes <prefix>_NN.png)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzw.h"
#include "u6pal.h"
#include "u6shape.h"
#include "png.h"

static uint8_t *slurp(const char*p,long*n){ FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);exit(2);}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET); uint8_t*b=malloc((size_t)*n);
  if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);} fclose(f); return b; }

int main(int argc,char**argv){
    const char*in =argc>1?argv[1]:"refdata/dos/u6/MAINMENU.SHP";
    const char*pp =argc>2?argv[2]:"refdata/dos/u6/U6PAL";
    const char*pre=argc>3?argv[3]:"build/shape";
    long fn,pn; uint8_t*f=slurp(in,&fn), *palb=slurp(pp,&pn);
    int32_t un=u6lzw_unpacked_size(f,(int32_t)fn);
    uint8_t*d; int32_t dn;
    if(un>0){ d=malloc((size_t)un); dn=u6lzw_decompress(f,(int32_t)fn,d,un); }
    else    { d=f; dn=(int32_t)fn; }                       /* stored (uncompressed) */
    u6pal_t pal; u6pal_parse(&pal,palb,(int32_t)pn);

    int N=u6shape_count(d,dn);
    printf("%s: unpacked=%d, %d shapes\n", in, dn, N);
    if(N<1||N>4096){ printf("bad shape count\n"); return 2; }

    static uint8_t cv[2048*2048];
    for(int si=0;si<N;si++){
        int W,H;
        if(!u6shape_decode(d,dn,si,&W,&H,cv,(int32_t)sizeof cv)) continue;
        uint8_t*img=malloc((size_t)W*H*3);
        for(int i=0;i<W*H;i++){ u6_rgb col=pal.color[cv[i]]; img[i*3]=col.r; img[i*3+1]=col.g; img[i*3+2]=col.b; }
        char out[512]; snprintf(out,sizeof out,"%s_%02d.png",pre,si);
        png_write_rgb(out,W,H,img);
        printf("  shape %2d: %dx%d -> %s\n", si, W, H, out);
        free(img);
    }
    return 0;
}
