// SPDX-License-Identifier: GPL-2.0-or-later
/* mapdump.c — host-side M2 visual proof: decode the U6 world and render PNGs.
 *
 * Renders (a) the full surface as a 1024x1024 overview (one representative pixel
 * per tile) and (b) a 256x256 tilesheet of tiles 0..255 at full 16x16. Also
 * runs structural assertions. Reads the user's own data in /refdata; writes
 * only to build/. Usage: mapdump <refdata/dos/u6> <out-dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u6lzw.h"
#include "u6pal.h"
#include "u6map.h"
#include "u6tile.h"
#include "png.h"

static int g_fail = 0;
static void check(const char *what, int ok)
{ printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) g_fail++; }

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f); return b;
}
static uint8_t *slurp_in(const char *dir, const char *name, long *n)
{
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, name);
    uint8_t *b = slurp(p, n);
    if (!b) printf("  (cannot read %s)\n", p);
    return b;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "refdata/dos/u6";
    const char *out = (argc > 2) ? argv[2] : "build";
    const char *palname = (argc > 3) ? argv[3] : "U6PAL";  /* SE=SEPAL, MD=<tbd> */
    printf("== mapdump: decode U6-engine world from %s (pal %s) ==\n", dir, palname);

    long pn, mn, cn, tn, in;
    uint8_t *palb = slurp_in(dir, palname, &pn);
    uint8_t *mapb = slurp_in(dir, "MAP", &mn);
    uint8_t *chkb = slurp_in(dir, "CHUNKS", &cn);
    uint8_t *mtc  = slurp_in(dir, "MAPTILES.VGA", &tn);
    uint8_t *tib  = slurp_in(dir, "TILEINDX.VGA", &in);
    if (!palb || !mapb || !chkb || !mtc || !tib) return 2;

    /* palette */
    u6pal_t pal;
    check("U6PAL parses (256 RGB6 triples)", u6pal_parse(&pal, palb, (int32_t)pn));

    /* tiles: decompress MAPTILES */
    int32_t musz = u6lzw_unpacked_size(mtc, (int32_t)tn);
    uint8_t *mt = malloc((size_t)musz);
    int32_t mgot = u6lzw_decompress(mtc, (int32_t)tn, mt, musz);
    check("MAPTILES decompresses to declared size", mgot == musz);
    u6tileset_t ts;
    u6tile_init(&ts, mt, mgot, tib, (int32_t)in);

    /* map */
    u6map_t map;
    int loaded = u6map_load(&map, mapb, (int32_t)mn, chkb, (int32_t)cn);
    check("MAP+CHUNKS load (size layout matches)", loaded);
    /* If the 12-bit unpack were wrong, indices would blow past 1024. */
    check("all surface chunk indices < 1024", loaded && map.max_chunk_index < U6_CHUNK_COUNT);
    if (!loaded) return 2;

    /* --- overview world map: one representative pixel per surface tile --- */
    int W = U6_SURFACE_TILES, H = U6_SURFACE_TILES;
    uint8_t *img = malloc((size_t)W * H * 3);
    long histo[256]; memset(histo, 0, sizeof histo);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t tile = u6map_surface_tile(&map, x, y);
            histo[tile]++;
            uint8_t ci = u6tile_repr(&ts, tile);
            u6_rgb c = pal.color[ci];
            uint8_t *p = img + ((size_t)y * W + x) * 3;
            p[0] = c.r; p[1] = c.g; p[2] = c.b;
        }
    char path[1024];
    snprintf(path, sizeof path, "%s/worldmap.png", out);
    check("write worldmap.png", png_write_rgb(path, W, H, img) == 0);
    printf("  -> %s (%dx%d)\n", path, W, H);

    /* DIAGNOSTIC: color each tile by a hash of its INDEX (no tile bitmaps), to
     * isolate MAP/CHUNKS correctness from tile rendering. */
    uint8_t *idximg = malloc((size_t)W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t t = u6map_surface_tile(&map, x, y);
            uint8_t *p = idximg + ((size_t)y * W + x) * 3;
            p[0] = (uint8_t)(t * 53); p[1] = (uint8_t)(t * 101); p[2] = (uint8_t)(t * 197);
        }
    snprintf(path, sizeof path, "%s/worldmap_idx.png", out);
    png_write_rgb(path, W, H, idximg);
    printf("  -> %s (tile-index hash visualization)\n", path);

    /* report the dominant terrain tile (U6 surface is mostly ocean) */
    int top = 0; for (int i = 1; i < 256; i++) if (histo[i] > histo[top]) top = i;
    printf("  dominant surface tile = %d (%.1f%% of surface)\n",
           top, 100.0 * (double)histo[top] / ((double)W * H));
    check("a single tile dominates (ocean fill, >25%)",
          histo[top] > (long)((double)W * H * 0.25));

    /* --- tilesheet: tiles 0..255 at full 16x16, 16x16 grid (256x256) --- */
    int TS = 16, sw = TS * U6_TILE_W, sh = TS * U6_TILE_H;
    uint8_t *sheet = malloc((size_t)sw * sh * 3);
    for (int t = 0; t < 256; t++) {
        int gx = (t % TS) * U6_TILE_W, gy = (t / TS) * U6_TILE_H;
        for (int yy = 0; yy < U6_TILE_H; yy++)
            for (int xx = 0; xx < U6_TILE_W; xx++) {
                u6_rgb c = pal.color[u6tile_pixel(&ts, t, xx, yy)];
                uint8_t *p = sheet + ((size_t)(gy + yy) * sw + (gx + xx)) * 3;
                p[0] = c.r; p[1] = c.g; p[2] = c.b;
            }
    }
    snprintf(path, sizeof path, "%s/tilesheet.png", out);
    check("write tilesheet.png", png_write_rgb(path, sw, sh, sheet) == 0);
    printf("  -> %s (%dx%d, tiles 0..255)\n", path, sw, sh);

    printf(g_fail ? "\nRESULT: %d FAILURE(S)\n" : "\nRESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
