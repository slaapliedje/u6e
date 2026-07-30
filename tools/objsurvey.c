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

    snprintf(path, sizeof path, "%s/LOOK.LZC", dir);
    long ln = 0; uint8_t *lb = slurp(path, &ln);
    if (!lb){ fprintf(stderr, "LOOK.LZC missing\n"); return 1; }

    static uint8_t look[65536]; long look_len = 0;
    u6lzc_t lc;
    if (u6lzc_open(&lc, lb, (int32_t)ln, marker) && u6lzc_count(&lc) >= 1){
        int32_t got = u6lzc_read(&lc, 0, look, sizeof look);
        if (got < 0){ fprintf(stderr, "LOOK.LZC decode failed\n"); return 1; }
        look_len = got;
    } else { fprintf(stderr, "LOOK.LZC: not a valid .LZC for marker 0x%02x\n", marker); return 1; }

    /* run-fill: [u16 tile][name\0] fills tiles (prev+1 .. tile) with `name` (look.md). */
    static const char *name[NTILES];
    { int prev = -1; long pos = 0;
      while (pos + 2 <= look_len){
          int ix = look[pos] | (look[pos+1] << 8); pos += 2; if (ix >= NTILES) break;
          const char *s = (const char *)&look[pos]; int sl = (int)strlen(s); pos += sl + 1;
          for (int t = prev+1; t <= ix && t < NTILES; t++) name[t] = s;
          prev = ix;
      } }

    for (int o = 0; o < NOBJ; o++){
        int tile = bt[o*2] | (bt[o*2+1] << 8);
        const char *nm = (tile < NTILES && name[tile]) ? name[tile] : "";
        if (kw){ if (nm[0] && strstr(nm, kw)) printf("obj_n %4d  tile %4d  %s\n", o, tile, nm); }
        else printf("obj_n %4d  tile %4d  %s\n", o, tile, nm);
    }
    return 0;
}
