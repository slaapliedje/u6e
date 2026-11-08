// SPDX-License-Identifier: GPL-2.0-or-later
/* objfind.c — host tool: list the new-game world objects (LZOBJBLK surface, LZDNGBLK dungeons) by object
 * number or by tile, read from the user's own data with our decoders (docs/formats/objblk.md). A contained
 * object is reported at its outermost container's tile. Used by the bot (tools/bot) to find ladders and
 * quest items beyond what the game's own dump shows. Ships nothing.
 * Usage: objfind <u6 data dir> <obj_n | @x,y,z> ...
 * Output: one line per object: "obj fr q qty x y z container status" (container = obj_n holding it, or -1) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "u6lzw.h"
#include "u6obj.h"

#define MAXOBJ 65536
static u6_object_t objs[MAXOBJ];

static uint8_t *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n > 0 ? *n : 1);
    if (b && fread(b, 1, *n, f) != (size_t)*n){ free(b); b = NULL; }
    fclose(f); return b;
}

static void scan(const char *dir, const char *file, int argc, char **argv)
{
    char path[512]; snprintf(path, sizeof path, "%s/%s", dir, file);
    long n = 0; uint8_t *lb = slurp(path, &n);
    if (!lb){ fprintf(stderr, "no %s\n", path); return; }
    int32_t cap = 8 * 1024 * 1024; uint8_t *ob = malloc((size_t)cap);
    int32_t got = u6lzw_decompress(lb, (int32_t)n, ob, cap);
    int32_t no = got > 0 ? u6obj_decode_world(ob, got, objs, MAXOBJ) : 0;
    for (int32_t i = 0; i < no; i++){
        int r = i, depth = 0;
        while (objs[r].parent != U6OBJ_PARENT_NONE && depth < 8){ r = objs[r].parent; depth++; }
        for (int a = 2; a < argc; a++){
            int hit;
            if (argv[a][0] == '@'){ int x = -1, y = -1, z = -1; sscanf(argv[a] + 1, "%d,%d,%d", &x, &y, &z);
                hit = objs[r].x == x && objs[r].y == y && objs[r].z == z; }
            else hit = objs[i].obj_n == atoi(argv[a]);
            if (!hit) continue;
            printf("%d %d %d %d %d %d %d %d %d\n", objs[i].obj_n, objs[i].frame, objs[i].quality, objs[i].quantity,
                   objs[r].x, objs[r].y, objs[r].z, depth ? objs[objs[i].parent].obj_n : -1, objs[i].status);
            break;
        }
    }
    free(ob); free(lb);
}

int main(int argc, char **argv)
{
    if (argc < 3){ fprintf(stderr, "usage: %s <u6 data dir> <obj_n | @x,y,z> ...\n", argv[0]); return 2; }
    scan(argv[1], "LZOBJBLK", argc, argv);
    scan(argv[1], "LZDNGBLK", argc, argv);
    return 0;
}
