// SPDX-License-Identifier: GPL-2.0-or-later
/* coredump.c — HOST validator for the M9 CORE world pack (SE/MD/U6) emitted by
 * mkworld's game-parameterized core path. Reads the pack (magic + geometry +
 * decoded tileset + palette + passable + objects), then renders the densest
 * object cluster (a settlement) as terrain + objects to a PNG — proving SE/MD
 * tiles + objects actually DECODE (not just parse) via the shared U6 loaders.
 *
 * Core pack layout (big-endian header; see mkworld.c emit_core_pack):
 *   0  magic[4]  4 u32 map_len  8 u32 chunks_len  12 u32 ntiles  16 u32 nobj
 *   20 u16 startX  22 u16 startY  24 u16 start_min
 *   26 MAP | CHUNKS | tileset(ntiles*256) | palette(768) | passable(256)
 *      | objects(nobj * 14B BE {x,y,tile,obj_n,qty16,quality,parent})
 *
 * Usage: coredump <pack> <out.png>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u6pal.h"
#include "u6map.h"
#include "png.h"

static uint8_t *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) { printf("cannot read %s\n", p); return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f); return b;
}
static uint32_t be32(const uint8_t *p){ return (uint32_t)p[0]<<24|p[1]<<16|p[2]<<8|p[3]; }
static uint16_t be16(const uint8_t *p){ return (uint16_t)(p[0]<<8|p[1]); }

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: coredump <pack> <out.png>\n"); return 2; }
    long n; uint8_t *pak = slurp(argv[1], &n);
    if (!pak || n < 26) { printf("pack too small\n"); return 2; }

    uint32_t map_len = be32(pak+4), chk_len = be32(pak+8), ntiles = be32(pak+12), nobj = be32(pak+16);
    int startx = be16(pak+20), starty = be16(pak+22);
    const uint8_t *map = pak+26, *chunks = map+map_len, *tileset = chunks+chk_len;
    const uint8_t *palette = tileset + (long)ntiles*256, *passable = palette+768;
    const uint8_t *objects = passable+256;
    long need = 26 + (long)map_len + chk_len + (long)ntiles*256 + 768 + 256 + (long)nobj*14;
    printf("== coredump %.4s: map=%u chunks=%u tiles=%u objects=%u start=(%d,%d) ==\n",
           (const char*)pak, map_len, chk_len, ntiles, nobj, startx, starty);
    if (need != n) { printf("pack size %ld != expected %ld (layout mismatch)\n", n, need); return 2; }

    u6map_t w;
    if (!u6map_load(&w, map, (int32_t)map_len, chunks, (int32_t)chk_len)) { printf("u6map_load failed\n"); return 2; }
    u6pal_t pal; if (!u6pal_parse(&pal, palette, 768)) { printf("palette parse failed\n"); return 2; }

    /* surface objects (parent == 0xFFFF) -> position list; find densest 24x24 cluster */
    enum { VT = 24, PXW = VT*16 };
    int nsurf = 0;
    static int ox[65536], oy[65536], otile[65536];
    for (uint32_t i = 0; i < nobj && nsurf < 65536; i++) {
        const uint8_t *r = objects + (long)i*14;
        if (be16(r+12) != 0xFFFF) continue;                 /* contained: skip */
        ox[nsurf] = be16(r); oy[nsurf] = be16(r+2); otile[nsurf] = be16(r+4); nsurf++;
    }
    int bestcx = startx, bestcy = starty, bestn = -1;
    for (int i = 0; i < nsurf; i += (nsurf > 4000 ? 3 : 1)) {   /* sample for speed */
        int c = 0;
        for (int j = 0; j < nsurf; j++) {
            int dx = ox[j]-ox[i], dy = oy[j]-oy[i];
            if (dx > -12 && dx < 12 && dy > -12 && dy < 12) c++;
        }
        if (c > bestn) { bestn = c; bestcx = ox[i]; bestcy = oy[i]; }
    }
    printf("surface objects=%d; densest cluster @ (%d,%d) with %d nearby\n", nsurf, bestcx, bestcy, bestn);

    int left = bestcx - VT/2, top = bestcy - VT/2;
    static uint8_t bb[PXW*PXW];
    for (int vy = 0; vy < VT; vy++) for (int vx = 0; vx < VT; vx++) {
        int t = u6map_surface_tile(&w, left+vx, top+vy);
        const uint8_t *src = tileset + (long)t*256;
        for (int r = 0; r < 16; r++) for (int c = 0; c < 16; c++)
            bb[(vy*16+r)*PXW + vx*16+c] = src[r*16+c];
    }
    /* overlay objects in view (transparent index 255) */
    int drawn = 0;
    for (int i = 0; i < nsurf; i++) {
        int vx = ox[i]-left, vy = oy[i]-top;
        if (vx < 0 || vx >= VT || vy < 0 || vy >= VT) continue;
        int t = otile[i]; if (t < 0 || t >= (int)ntiles) continue;
        const uint8_t *src = tileset + (long)t*256;
        for (int r = 0; r < 16; r++) for (int c = 0; c < 16; c++) {
            uint8_t v = src[r*16+c];
            if (v != 255) bb[(vy*16+r)*PXW + vx*16+c] = v;
        }
        drawn++;
    }

    uint8_t *img = malloc((size_t)PXW*PXW*3);
    for (int i = 0; i < PXW*PXW; i++) { u6_rgb c = pal.color[bb[i]]; img[i*3]=c.r; img[i*3+1]=c.g; img[i*3+2]=c.b; }
    int rc = png_write_rgb(argv[2], PXW, PXW, img);
    printf("wrote %s (%dx%d) centred (%d,%d); %d objects drawn\n", argv[2], PXW, PXW, left, top, drawn);
    return rc;
}
