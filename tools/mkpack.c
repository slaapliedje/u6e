/* mkpack.c — host: build an indexed-256 asset pack for the M3 render demo.
 *
 * Decodes a region of the U6 surface + the tileset + palette and writes a
 * BIG-ENDIAN pack (target byte order) the ST loads and composites. Also writes
 * a golden PNG of the region for comparison. Reads /refdata; writes build/.
 * Usage: mkpack <refdata/dos/u6> <out.pak> <golden.png> [ox] [oy]
 *
 * Pack layout (big-endian):
 *   0   char[4] "U6P1"
 *   4   u16     region width  in tiles (Wt)
 *   6   u16     region height in tiles (Ht)
 *   8   u16     tile count NT (256)
 *  10   u16     reserved (0)
 *  12   u8[768] palette (256 * RGB, 6-bit)
 * 780   u8[NT*256] tileset (NT tiles, 16x16x8bpp)
 * ...   u8[Wt*Ht]  region grid (tile indices)
 */
#include <stdio.h>
#include <stdlib.h>
#include "u6lzw.h"
#include "u6pal.h"
#include "u6map.h"
#include "u6tile.h"
#include "png.h"

#define WT 20
#define HT 30
#define NT 256

static uint8_t *slurp(const char *d, const char *name, long *n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", d, name);
    FILE *f = fopen(p, "rb"); if (!f) { printf("cannot read %s\n", p); return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n); fread(b, 1, (size_t)*n, f); fclose(f); return b;
}
static void be16(FILE *f, unsigned v){ fputc((v>>8)&0xFF,f); fputc(v&0xFF,f); }

int main(int argc, char **argv)
{
    const char *dir   = argc > 1 ? argv[1] : "refdata/dos/u6";
    const char *out   = argc > 2 ? argv[2] : "build/u6.pak";
    const char *gold  = argc > 3 ? argv[3] : "build/region_golden.png";
    int ox = argc > 4 ? atoi(argv[4]) : 336;
    int oy = argc > 5 ? atoi(argv[5]) : 336;

    long pn, mn, cn, tn, in;
    uint8_t *palb = slurp(dir, "U6PAL", &pn);
    uint8_t *mapb = slurp(dir, "MAP", &mn);
    uint8_t *chkb = slurp(dir, "CHUNKS", &cn);
    uint8_t *mtc  = slurp(dir, "MAPTILES.VGA", &tn);
    uint8_t *tib  = slurp(dir, "TILEINDX.VGA", &in);
    if (!palb || !mapb || !chkb || !mtc || !tib) return 2;

    u6pal_t pal; u6pal_parse(&pal, palb, (int32_t)pn);
    int32_t musz = u6lzw_unpacked_size(mtc, (int32_t)tn);
    uint8_t *mt = malloc(musz); u6lzw_decompress(mtc, (int32_t)tn, mt, musz);
    u6tileset_t ts; u6tile_init(&ts, mt, musz, tib, (int32_t)in);
    u6map_t map; if (!u6map_load(&map, mapb, (int32_t)mn, chkb, (int32_t)cn)) { printf("map load failed\n"); return 2; }

    /* region grid */
    uint8_t grid[WT * HT];
    for (int ty = 0; ty < HT; ty++)
        for (int tx = 0; tx < WT; tx++)
            grid[ty * WT + tx] = u6map_surface_tile(&map, ox + tx, oy + ty);

    /* write pack (big-endian) */
    FILE *f = fopen(out, "wb"); if (!f) { printf("cannot write %s\n", out); return 2; }
    fwrite("U6P1", 1, 4, f);
    be16(f, WT); be16(f, HT); be16(f, NT); be16(f, 0);
    fwrite(palb, 1, 768, f);                                  /* palette 6-bit */
    for (int t = 0; t < NT; t++) {                            /* tileset */
        uint8_t tile[256];
        for (int yy = 0; yy < 16; yy++)
            for (int xx = 0; xx < 16; xx++)
                tile[yy * 16 + xx] = u6tile_pixel(&ts, t, xx, yy);
        fwrite(tile, 1, 256, f);
    }
    fwrite(grid, 1, WT * HT, f);                              /* region grid */
    fclose(f);

    /* golden PNG of the region (full 16x16 tiles) */
    int W = WT * 16, H = HT * 16;
    uint8_t *img = malloc((size_t)W * H * 3);
    for (int ty = 0; ty < HT; ty++)
        for (int tx = 0; tx < WT; tx++)
            for (int yy = 0; yy < 16; yy++)
                for (int xx = 0; xx < 16; xx++) {
                    u6_rgb c = pal.color[u6tile_pixel(&ts, grid[ty*WT+tx], xx, yy)];
                    uint8_t *p = img + ((size_t)(ty*16+yy) * W + (tx*16+xx)) * 3;
                    p[0]=c.r; p[1]=c.g; p[2]=c.b;
                }
    png_write_rgb(gold, W, H, img);
    printf("wrote %s (%dx%d tiles @ %d,%d) and golden %s (%dx%d)\n",
           out, WT, HT, ox, oy, gold, W, H);
    return 0;
}
