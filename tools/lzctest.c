// SPDX-License-Identifier: GPL-2.0-or-later
/* lzctest.c — HOST validator for the SE/MD .LZC flagged-library container.
 *
 * Implemented FROM /docs/formats/lzc.md. Walks a game data directory, opens
 * every *.LZC with our u6lzc reader, and asserts: header == file size, the
 * index tiles to EOF (no bad spans), and every LZW-flagged item decompresses
 * with our u6lzw to EXACTLY its declared size. Prints a readable TALK snippet
 * as a content sanity check. This proves decode-correctness host-side (M9)
 * before any SE/MD asset conversion depends on it.
 *
 * Usage: lzctest <dir> <se|md>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "u6lzc.h"
#include "u6lzw.h"

static int g_fail = 0;

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f); return b;
}

/* Print up to `cap` printable characters of a buffer (control -> '.'), for a
 * quick "is this real text?" eyeball. */
static void snippet(const uint8_t *b, int32_t n, int cap)
{
    for (int i = 0; i < n && i < cap; i++) {
        uint8_t c = b[i];
        putchar((c == '\n' || c == '\r') ? ' ' : (c >= 32 && c < 127) ? (int)c : '.');
    }
    putchar('\n');
}

/* Validate one .LZC file. Returns 1 if fully OK. */
static int check_file(const char *dir, const char *name, uint8_t marker)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    long n; uint8_t *b = slurp(path, &n);
    if (!b) { printf("  [FAIL] %-16s cannot read\n", name); g_fail++; return 0; }

    u6lzc_t lib;
    if (!u6lzc_open(&lib, b, (int32_t)n, marker)) {
        printf("  [FAIL] %-16s u6lzc_open (hdr!=size or bad index)\n", name);
        g_fail++; free(b); return 0;
    }

    int present = 0, comp = 0, stored = 0, badspan = 0, baddecode = 0;
    int32_t last_end = -1;
    static uint8_t out[1 << 20];   /* 1 MiB scratch — largest item seen is < 100 KiB */

    for (int32_t i = 0; i < lib.count; i++) {
        int32_t size; uint8_t flag;
        const uint8_t *p = u6lzc_entry(&lib, i, &size, &flag);
        if (!p) continue;                     /* absent slot */
        present++;
        if (p < b || p + size > b + n) { badspan++; continue; }

        if (u6lzc_is_compressed(&lib, i)) {
            comp++;
            int32_t decl = u6lzw_unpacked_size(p, size);
            int32_t got  = u6lzw_decompress(p, size, out, sizeof out);
            if (got < 0 || got != decl) baddecode++;
        } else {
            stored++;
        }
        last_end = (int32_t)(p - b) + size;
    }
    int tiled = (last_end == (int32_t)n);     /* last present item runs to EOF */

    int ok = (badspan == 0 && baddecode == 0 && tiled);
    printf("  [%s] %-16s count=%-4d present=%-4d lzw=%-4d stored=%-4d%s%s%s\n",
           ok ? "PASS" : "FAIL", name, lib.count, present, comp, stored,
           badspan ? "  BAD-SPAN" : "", baddecode ? "  BAD-DECODE" : "",
           tiled ? "" : "  NO-TILE-TO-EOF");
    if (!ok) g_fail++;
    free(b);
    return ok;
}

/* Decompress/copy a few TALK items and print text snippets (content sanity). */
static void talk_snippet(const char *dir, uint8_t marker)
{
    char path[1024]; snprintf(path, sizeof path, "%s/TALK.LZC", dir);
    long n; uint8_t *b = slurp(path, &n); if (!b) return;
    u6lzc_t lib; if (!u6lzc_open(&lib, b, (int32_t)n, marker)) { free(b); return; }
    static uint8_t out[1 << 20];
    int shown = 0;
    for (int32_t i = 0; i < lib.count && shown < 3; i++) {
        int32_t got = u6lzc_read(&lib, i, out, sizeof out);
        if (got <= 0) continue;
        printf("  TALK item%-3d (%6d bytes) -> ", i, got);
        snippet(out, got, 58);
        shown++;
    }
    free(b);
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: lzctest <dir> <se|md>\n"); return 2; }
    const char *dir = argv[1];
    uint8_t marker = (strcasecmp(argv[2], "se") == 0) ? U6LZC_COMP_SE
                   : (strcasecmp(argv[2], "md") == 0) ? U6LZC_COMP_MD : 0;
    if (!marker) { printf("second arg must be se or md\n"); return 2; }

    printf("== .LZC container validation (%s) ==\n", argv[2]);

    DIR *d = opendir(dir);
    if (!d) { printf("cannot open dir %s\n", dir); return 2; }
    /* Collect + sort names for stable output. */
    char names[256][64]; int nn = 0;
    struct dirent *de;
    while ((de = readdir(d)) && nn < 256) {
        size_t L = strlen(de->d_name);
        if (L > 4 && strcasecmp(de->d_name + L - 4, ".LZC") == 0)
            snprintf(names[nn++], 64, "%s", de->d_name);
    }
    closedir(d);
    for (int i = 0; i < nn; i++) for (int j = i + 1; j < nn; j++)
        if (strcmp(names[i], names[j]) > 0) {
            char t[64]; memcpy(t, names[i], 64); memcpy(names[i], names[j], 64); memcpy(names[j], t, 64);
        }

    int total = 0, ok = 0;
    for (int i = 0; i < nn; i++) { total++; ok += check_file(dir, names[i], marker); }

    printf("\n");
    talk_snippet(dir, marker);
    printf("\n%d/%d files OK; %s\n", ok, total, g_fail ? "FAILURES PRESENT" : "ALL PASS");
    return g_fail ? 1 : 0;
}
