// SPDX-License-Identifier: GPL-2.0-or-later
/* raw2png.c — host: convert a dumped chunky-8bpp backbuffer (OUT.RAW) + U6PAL
 * into a PNG, to verify the ST's composite against the golden image.
 * Usage: raw2png <out.raw> <U6PAL> <width> <height> <out.png> */
#include <stdio.h>
#include <stdlib.h>
#include "u6pal.h"
#include "png.h"

static uint8_t *slurp(const char *p, long *n)
{ FILE *f=fopen(p,"rb"); if(!f)return NULL; fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t *b=malloc((size_t)*n); fread(b,1,(size_t)*n,f); fclose(f); return b; }

int main(int argc, char **argv)
{
    if (argc < 6) { printf("usage: raw2png raw u6pal W H out.png\n"); return 2; }
    long rn, pn;
    uint8_t *raw = slurp(argv[1], &rn);
    uint8_t *palb = slurp(argv[2], &pn);
    int W = atoi(argv[3]), H = atoi(argv[4]);
    if (!raw || !palb) { printf("read error\n"); return 2; }
    if (rn < (long)W * H) { printf("raw too small: %ld < %d\n", rn, W*H); return 2; }
    u6pal_t pal; u6pal_parse(&pal, palb, (int32_t)pn);
    uint8_t *img = malloc((size_t)W * H * 3);
    for (int i = 0; i < W * H; i++) {
        u6_rgb c = pal.color[raw[i]];
        img[i*3+0]=c.r; img[i*3+1]=c.g; img[i*3+2]=c.b;
    }
    return png_write_rgb(argv[5], W, H, img);
}
