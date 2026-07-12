// SPDX-License-Identifier: GPL-2.0-or-later
/* objrender.c — host validation: decode objects + render a region with objects
 * drawn over terrain (M6). Proves OBJBLK decode + BASETILE + combined tileset.
 * Usage: objrender <refdata/dos/u6> <out.png> [ox oy]  (auto-densest if omitted)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u6lzw.h"
#include "u6pal.h"
#include "u6map.h"
#include "u6tile.h"
#include "u6obj.h"
#include "png.h"

#define TRANSPARENT 255
#define MAXOBJ 16384

static uint8_t *slurp(const char *d, const char *name, long *n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", d, name);
    FILE *f = fopen(p, "rb"); if (!f) { printf("cannot read %s\n", p); exit(2); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n); fread(b, 1, (size_t)*n, f); fclose(f); return b;
}

int g_fail = 0;
static void check(const char *w, int ok){ printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok) g_fail++; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "refdata/dos/u6";
    const char *out = argc > 2 ? argv[2] : "build/objrender.png";

    long mn, cn, pn, tn, on, bn, in, zn;
    uint8_t *mapb = slurp(dir,"MAP",&mn), *chk = slurp(dir,"CHUNKS",&cn);
    uint8_t *palb = slurp(dir,"U6PAL",&pn), *mtc = slurp(dir,"MAPTILES.VGA",&tn);
    uint8_t *ot = slurp(dir,"OBJTILES.VGA",&on), *ti = slurp(dir,"TILEINDX.VGA",&in);
    uint8_t *bt = slurp(dir,"BASETILE",&bn), *lz = slurp(dir,"LZOBJBLK",&zn);

    u6pal_t pal; u6pal_parse(&pal, palb, (int32_t)pn);
    u6map_t map; u6map_load(&map, mapb, (int32_t)mn, chk, (int32_t)cn);

    /* combined tileset blob: decompressed MAPTILES ++ raw OBJTILES, indexed by TILEINDX */
    int32_t musz = u6lzw_unpacked_size(mtc, (int32_t)tn);
    int32_t comb_len = musz + (int32_t)on;
    uint8_t *comb = malloc(comb_len);
    u6lzw_decompress(mtc, (int32_t)tn, comb, musz);
    memcpy(comb + musz, ot, (size_t)on);
    u6tileset_t ts; u6tile_init(&ts, comb, comb_len, ti, (int32_t)in);

    /* decode objects */
    int32_t uz = u6lzw_unpacked_size(lz, (int32_t)zn);
    uint8_t *od = malloc(uz); u6lzw_decompress(lz, (int32_t)zn, od, uz);
    u6_object_t *objs = malloc(sizeof(u6_object_t) * MAXOBJ);
    int32_t nobj = u6obj_decode(od, uz, objs, MAXOBJ);

    printf("== M6 object render validation ==\n");
    check("decoded a plausible number of on-map objects (>3000)", nobj > 3000);
    /* count objects whose mapped tile is a real object tile (>=512) */
    int objtiles = 0, surf = 0;
    for (int32_t i = 0; i < nobj; i++) {
        if (objs[i].z == 0) surf++;
        if (u6_basetile(bt, objs[i].obj_n) >= 512) objtiles++;
    }
    check("most objects map to object tiles (>=512)", objtiles > nobj/2);
    printf("  %d on-map objects (%d surface); %d use object-tiles\n", nobj, surf, objtiles);

    /* pick region: arg, else auto-densest surface 20x15 window */
    int ox, oy;
    if (argc > 4) { ox = atoi(argv[3]); oy = atoi(argv[4]); }
    else {
        static int grid[64][64]; memset(grid,0,sizeof grid);
        for (int32_t i=0;i<nobj;i++) if(objs[i].z==0 && objs[i].x<1024 && objs[i].y<1024) grid[objs[i].y/16][objs[i].x/16]++;
        int best=-1; ox=oy=0;
        for (int gy=0;gy<63;gy++) for(int gx=0;gx<63;gx++){int s=grid[gy][gx]+grid[gy][gx+1]+grid[gy+1][gx]+grid[gy+1][gx+1]; if(s>best){best=s;ox=gx*16;oy=gy*16;}}
    }

    int W=20,H=15,IW=W*16,IH=H*16;
    uint8_t *img = malloc((size_t)IW*IH*3);
    for (int ty=0;ty<H;ty++) for(int tx=0;tx<W;tx++){            /* terrain */
        int t = u6map_surface_tile(&map, ox+tx, oy+ty);
        for(int yy=0;yy<16;yy++) for(int xx=0;xx<16;xx++){
            u6_rgb c = pal.color[u6tile_pixel(&ts,t,xx,yy)];
            uint8_t *p=img+(((size_t)(ty*16+yy))*IW+(tx*16+xx))*3; p[0]=c.r;p[1]=c.g;p[2]=c.b; }
    }
    int drawn=0;
    for (int32_t i=0;i<nobj;i++){                                /* objects */
        if (objs[i].z!=0) continue;
        int x=objs[i].x, y=objs[i].y;
        if (x<ox||x>=ox+W||y<oy||y>=oy+H) continue;
        int t = u6_basetile(bt, objs[i].obj_n) + objs[i].frame;
        int sx=(x-ox)*16, sy=(y-oy)*16; drawn++;
        for(int yy=0;yy<16;yy++) for(int xx=0;xx<16;xx++){
            int pi=u6tile_pixel(&ts,t,xx,yy); if(pi==TRANSPARENT) continue;
            u6_rgb c=pal.color[pi]; uint8_t *p=img+(((size_t)(sy+yy))*IW+(sx+xx))*3; p[0]=c.r;p[1]=c.g;p[2]=c.b; }
    }
    check("region has objects to draw", drawn > 0);
    png_write_rgb(out, IW, IH, img);
    printf("  region (%d,%d): drew %d objects -> %s\n", ox, oy, drawn, out);

    printf(g_fail ? "\nRESULT: %d FAILURE(S)\n" : "\nRESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
