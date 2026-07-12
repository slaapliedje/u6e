/* bmpdump.c — HOST: decode a U6 "U6Bmp" image (e.g. PAPER.BMP, the game-screen
 * papyrus border + scroll background) to a PNG, to validate the format decode.
 *
 * Format (understood from Nuvie files/U6Bmp.cpp, implemented from our spec):
 * the file is WHOLE-FILE LZW-compressed; decompressed it is
 *   [u16 LE width][u16 LE height][width*height bytes of 8-bit palette indices]
 * row-major, top-left origin, U6PAL palette.
 *
 * Usage: bmpdump <file.bmp> <U6PAL> <out.png>
 */
#include <stdio.h>
#include <stdlib.h>
#include "u6lzw.h"
#include "u6pal.h"
#include "png.h"

static uint8_t *slurp(const char*p,long*n)
{ FILE*f=fopen(p,"rb"); if(!f){printf("cannot read %s\n",p);exit(2);}
  fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc((size_t)*n); if(fread(b,1,(size_t)*n,f)!=(size_t)*n){printf("short read\n");exit(2);}
  fclose(f); return b; }

int main(int argc,char**argv)
{
    const char*in =argc>1?argv[1]:"refdata/dos/u6/PAPER.BMP";
    const char*pp =argc>2?argv[2]:"refdata/dos/u6/U6PAL";
    const char*out=argc>3?argv[3]:"build/paper.png";

    long fn,pn; uint8_t*f=slurp(in,&fn), *palb=slurp(pp,&pn);
    int32_t un=u6lzw_unpacked_size(f,(int32_t)fn);
    if(un<4){printf("not LZW or too small (unpacked=%d)\n",un);return 2;}
    uint8_t*d=malloc((size_t)un);
    if(u6lzw_decompress(f,(int32_t)fn,d,un)!=un){printf("decompress failed\n");return 2;}
    int w=d[0]|(d[1]<<8), h=d[2]|(d[3]<<8);
    printf("%s: unpacked=%d  width=%d height=%d  (pixels=%d)\n",in,un,w,h,w*h);
    if(w<=0||h<=0||4+(long)w*h>un){printf("bad dimensions\n");return 2;}

    u6pal_t pal; u6pal_parse(&pal,palb,(int32_t)pn);
    const uint8_t*raw=d+4;
    uint8_t*img=malloc((size_t)w*h*3);
    for(int i=0;i<w*h;i++){u6_rgb c=pal.color[raw[i]];img[i*3]=c.r;img[i*3+1]=c.g;img[i*3+2]=c.b;}
    int rc=png_write_rgb(out,w,h,img);
    printf("wrote %s (%dx%d)\n",out,w,h);
    return rc;
}
