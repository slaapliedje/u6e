// SPDX-License-Identifier: GPL-2.0-or-later
/* objsurvey.c — SE/MD obj_n -> look-name survey (M9, host-side, clean-room).
 *
 * Purpose: map each object number to its in-game LOOK name for Savage Empire /
 * Martian Dreams, so we can identify the "well-known" objects (avatar, gold,
 * key, container, door, food, ...) and fill GameDef.wko + the per-game USE
 * usecode. Derived entirely from the USER'S OWN data files (the primary source)
 * with our own decoders — nothing here is shipped.
 *
 * Method: BASETILE (obj_n -> base tile, 1024 x u16 LE) composed with LOOK.LZC
 * (an .LZC-wrapped run-fill table of tile -> name, same layout as U6 LOOK.LZD,
 * docs/formats/look.md). Prints "obj_n <tile> <name>" for every object; grep it.
 *
 * Usage: objsurvey <game-dir> <se|md> [keyword]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "u6lzc.h"
#include "u6lzw.h"

#define NTILES 2048
#define NOBJ   1024

static uint8_t *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (b) *len = (long)fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3){ fprintf(stderr, "usage: objsurvey <game-dir> <se|md> [keyword]\n"); return 2; }
    const char *dir = argv[1];
    uint8_t marker = (strcmp(argv[2], "md") == 0) ? U6LZC_COMP_MD : U6LZC_COMP_SE;
    const char *kw = (argc > 3) ? argv[3] : NULL;
    char path[512];

    snprintf(path, sizeof path, "%s/BASETILE", dir);
    long bn = 0; uint8_t *bt = slurp(path, &bn);
    if (!bt || bn < NOBJ*2){ fprintf(stderr, "BASETILE missing/short (%ld)\n", bn); return 1; }

    int is_u6 = (strcmp(argv[2], "u6") == 0);        /* U6 uses LOOK.LZD (bare U6-LZW); SE/MD use LOOK.LZC */
    snprintf(path, sizeof path, "%s/%s", dir, is_u6 ? "LOOK.LZD" : "LOOK.LZC");
    long ln = 0; uint8_t *lb = slurp(path, &ln);
    if (!lb){ fprintf(stderr, "%s missing\n", is_u6 ? "LOOK.LZD" : "LOOK.LZC"); return 1; }

    static uint8_t look[65536]; long look_len = 0;
    if (is_u6){                                       /* U6: bare LZW (4-byte LE size hdr + stream) */
        int32_t cap = u6lzw_unpacked_size(lb, (int32_t)ln);
        if (cap <= 0 || cap > (int32_t)sizeof look) cap = sizeof look;
        int32_t got = u6lzw_decompress(lb, (int32_t)ln, look, cap);
        if (got <= 0){ fprintf(stderr, "LOOK.LZD decode failed\n"); return 1; }
        look_len = got;
    } else {
        u6lzc_t lc;
        if (u6lzc_open(&lc, lb, (int32_t)ln, marker) && u6lzc_count(&lc) >= 1){
            int32_t got = u6lzc_read(&lc, 0, look, sizeof look);
            if (got < 0){ fprintf(stderr, "LOOK.LZC decode failed\n"); return 1; }
            look_len = got;
        } else { fprintf(stderr, "LOOK.LZC: not a valid .LZC for marker 0x%02x\n", marker); return 1; }
    }

    /* run-fill: [u16 tile][name\0] fills tiles (prev+1 .. tile) with `name` (look.md). */
    static const char *name[NTILES];
    { int prev = -1; long pos = 0;
      while (pos + 2 <= look_len){
          int ix = look[pos] | (look[pos+1] << 8); pos += 2; if (ix >= NTILES) break;
          const char *s = (const char *)&look[pos]; int sl = (int)strlen(s); pos += sl + 1;
          for (int t = prev+1; t <= ix && t < NTILES; t++) name[t] = s;
          prev = ix;
      } }

    /* optional TILEFLAG (7168 B): tileflags1 @ 0x0000, one byte/tile — bit1 Impassable, bit2 Wall. */
    snprintf(path, sizeof path, "%s/TILEFLAG", dir);
    long tfn = 0; uint8_t *tf = slurp(path, &tfn);

    /* frames mode: `objsurvey <dir> <se|md> frames <obj_n> [count]` — dump a door/container's frames
     * with passability, to derive open/closed frame semantics from the game's own data. */
    if (kw && strcmp(kw, "frames") == 0){
        int o = (argc > 4) ? atoi(argv[4]) : 0;
        int base = bt[o*2] | (bt[o*2+1] << 8);
        int next = (o+1 < NOBJ) ? (bt[(o+1)*2] | (bt[(o+1)*2+1] << 8)) : base+16;
        int cnt = (argc > 5) ? atoi(argv[5]) : (next > base ? next - base : 8);  /* default = this object's OWN frames */
        const char *self = (base < NTILES && name[base]) ? name[base] : "";
        printf("obj_n %d base %d (obj_n+1 base %d => %d frames) '%s' — P=passable X=blocks, * = name != '%s':\n",
               o, base, next, next-base, self, self);
        for (int fr = 0; fr < cnt; fr++){ int t = base + fr;
            int blk = (tf && t < NTILES) ? (tf[t] & 0x06) : -1;   /* tileflags1 bit1|bit2 */
            const char *nm = (t < NTILES && name[t]) ? name[t] : "";
            printf("  fr %2d  tile %4d  %s  %s%s\n", fr, t, blk<0?"?":(blk?"X":"P"),
                   nm, (self[0] && strcmp(nm, self) != 0) ? "  *" : "");
        }
        return 0;
    }

    for (int o = 0; o < NOBJ; o++){
        int tile = bt[o*2] | (bt[o*2+1] << 8);
        const char *nm = (tile < NTILES && name[tile]) ? name[tile] : "";
        if (kw){ if (nm[0] && strstr(nm, kw)) printf("obj_n %4d  tile %4d  %s\n", o, tile, nm); }
        else printf("obj_n %4d  tile %4d  %s\n", o, tile, nm);
    }
    return 0;
}
