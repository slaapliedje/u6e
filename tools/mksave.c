// SPDX-License-Identifier: GPL-2.0-or-later
/* mksave.c — host: synthesize a PRISTINE ScummVM/Nuvie-layout save (a "new game") from the
 * user's original U6 data files, exactly the inputs Nuvie's own new-game path reads:
 *   LZOBJBLK  = the 64 surface object blocks (LZW)
 *   LZDNGBLK  = the 5 dungeon object blocks + the pristine OBJLIST (LZW, one stream)
 * Output layout (docs/formats/nuvie_save.md): [u16 256][u16 'U6'] [inventory chunk]
 * [eggs chunk] [64 surface chunks, row-major] [5 dungeon chunks] [u32 objlist_size][objlist].
 * Records are re-bucketed the way a save from a freshly started game is: carried/readied
 * records into the inventory chunk, eggs (obj 335) into the eggs chunk, on-map records by
 * (y>>7)*8+(x>>7) for z==0 or into dungeon chunk z-1, each container's contents following it
 * with their chunk-local parent links re-indexed. A ScummVM extended-save trailer follows
 * (SVMCR header + a tiny thumbnail + the header offset as the last u32): ScummVM's load menu
 * lists a save by that header and ignores files without one. The objlist gets Nuvie's new-game
 * defaults (name/stats/level/magic) — character creation overrides them at save time.
 * The Savage Empire and Martian Dreams start the same way (nuvie_save.md "A new game from the
 * original files"): their LZOBJBLK / LZDNGBLK are one-item .LZC libraries around the same LZW
 * streams, and only the game tag, the egg object and the new-game objlist fields differ. The game
 * is MKWORLD_GAME (u6 / savage / martian, as for mkworld), else found by its executable.
 * Usage: mksave <gamedir> <out.000> [avatar name]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "u6lzw.h"
#include "u6lzc.h"


typedef struct {
    uint8_t  raw[8];
    int      loc, x, y, z, obj_n;
    int      parent;         /* index within the SOURCE block, or -1 */
    int      first_child, next_sibling;   /* original order preserved */
    int      out_index;      /* index within the destination chunk (once emitted) */
} rec_t;

static uint8_t *slurp(const char *dir, const char *name, long *n)
{
    char p[1200]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f){ *n = 0; return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n); if (fread(b, 1, (size_t)*n, f) != (size_t)*n){ fclose(f); free(b); *n = 0; return NULL; }
    fclose(f); return b;
}
typedef struct { const char *id; uint8_t tag[2]; int egg; uint8_t lzc; } game_t;
static const game_t GAMES[3] = {
    { "u6",      { '6', 'U' }, 335, 0 },
    { "savage",  { 'E', 'S' }, 230, U6LZC_COMP_SE },
    { "martian", { 'D', 'M' }, 466, U6LZC_COMP_MD },
};
static const game_t *g_game = &GAMES[0];

static uint8_t *unlzw(const char *dir, const char *name, long *outlen)
{
    long n; uint8_t *b = slurp(dir, name, &n);
    if (!b){ printf("mksave: %s/%s not found\n", dir, name); return NULL; }
    if (g_game->lzc){                                             /* SE/MD: a one-item .LZC around the LZW */
        u6lzc_t lc; int32_t isz; uint8_t fl; const uint8_t *p;
        if (!u6lzc_open(&lc, b, (int32_t)n, g_game->lzc) || !(p = u6lzc_entry(&lc, 0, &isz, &fl))){
            printf("mksave: %s: not an .LZC library\n", name); free(b); return NULL; }
        int32_t cap = u6lzc_is_compressed(&lc, 0) ? u6lzw_unpacked_size(p, isz) : isz;
        uint8_t *out = cap > 0 ? malloc((size_t)cap) : NULL;
        int32_t got = out ? u6lzc_read(&lc, 0, out, cap) : -1;
        free(b);
        if (got <= 0){ printf("mksave: %s: LZC item decode failed\n", name); free(out); return NULL; }
        *outlen = got; return out;
    }
    int32_t un = u6lzw_unpacked_size(b, (int32_t)n);
    if (un <= 0){ printf("mksave: %s: not an LZW container\n", name); free(b); return NULL; }
    uint8_t *out = malloc((size_t)un);
    int32_t got = u6lzw_decompress(b, (int32_t)n, out, un);
    free(b);
    if (got != un){ printf("mksave: %s: LZW decode failed (%d/%d)\n", name, got, un); free(out); return NULL; }
    *outlen = un; return out;
}

/* ---- output chunks -------------------------------------------------------------------- */
typedef struct { uint8_t *buf; int cnt, cap; } chunk_t;
static chunk_t g_inv, g_eggs, g_surf[64], g_dng[5];
static int chunk_push(chunk_t *c, const uint8_t *r)
{
    if (c->cnt >= c->cap){ c->cap = c->cap ? c->cap * 2 : 64; c->buf = realloc(c->buf, (size_t)c->cap * 8); }
    memcpy(c->buf + (size_t)c->cnt * 8, r, 8);
    return c->cnt++;
}
static void chunk_write(FILE *f, const chunk_t *c)
{
    uint8_t h[2] = { (uint8_t)(c->cnt & 0xff), (uint8_t)(c->cnt >> 8) };
    fwrite(h, 1, 2, f);
    if (c->cnt) fwrite(c->buf, 8, (size_t)c->cnt, f);
}

/* parse one [u16 count][count*8] block at *pos; returns count (or -1), fills recs (linked) */
static int parse_block(const uint8_t *d, long n, long *pos, rec_t *recs)
{
    if (*pos + 2 > n) return -1;
    int cnt = d[*pos] | (d[*pos + 1] << 8); *pos += 2;
    if (*pos + (long)cnt * 8 > n) return -1;
    for (int k = 0; k < cnt; k++){
        rec_t *r = &recs[k]; const uint8_t *b = d + *pos + k * 8;
        memcpy(r->raw, b, 8);
        r->loc = b[0] & 0x18;
        r->x = b[1] | ((b[2] & 3) << 8); r->y = (b[2] >> 2) | ((b[3] & 0x0f) << 6); r->z = b[3] >> 4;
        r->obj_n = b[4] | ((b[5] & 3) << 8);
        r->parent = -1; r->first_child = -1; r->next_sibling = -1; r->out_index = -1;
    }
    /* container links are block-local record indices (objblk.md §2) */
    int *last_child = malloc(sizeof(int) * (size_t)(cnt ? cnt : 1));
    for (int k = 0; k < cnt; k++) last_child[k] = -1;
    for (int k = 0; k < cnt; k++){
        rec_t *r = &recs[k];
        if (r->loc != 0x08) continue;
        int link = ((r->y & 0x3f) << 10) | r->x;
        if (link < 0 || link >= cnt || link == k) continue;       /* orphan: emitted as a root */
        r->parent = link;
        if (recs[link].first_child < 0) recs[link].first_child = k; else recs[last_child[link]].next_sibling = k;
        last_child[link] = k;
    }
    free(last_child);
    *pos += (long)cnt * 8;
    return cnt;
}

/* emit record k of `recs` into chunk c (as a root or as a child of an already-emitted parent),
 * then its contents in original order, rewriting each child's link to the new parent index */
static void emit_tree(chunk_t *c, rec_t *recs, int k)
{
    rec_t *r = &recs[k];
    uint8_t out[8]; memcpy(out, r->raw, 8);
    if (r->parent >= 0){
        int p = recs[r->parent].out_index;                        /* link -> chunk-local index */
        out[1] = (uint8_t)(p & 0xff);
        out[2] = (uint8_t)(((p >> 8) & 3) | (((p >> 10) & 0x3f) << 2));
        out[3] = (uint8_t)(out[3] & 0xf0);                        /* y high bits: don't-care -> 0 */
    }
    r->out_index = chunk_push(c, out);
    for (int ch = r->first_child; ch >= 0; ch = recs[ch].next_sibling) emit_tree(c, recs, ch);
}

static chunk_t *dest_of(const rec_t *r, int *stats)
{
    if (r->loc == 0x10 || r->loc == 0x18){ stats[0]++; return &g_inv; }
    if (r->obj_n == g_game->egg){ stats[1]++; return &g_eggs; }
    if (r->z == 0){ stats[2]++; return &g_surf[((r->y >> 7) & 7) * 8 + ((r->x >> 7) & 7)]; }
    if (r->z >= 1 && r->z <= 5){ stats[3]++; return &g_dng[r->z - 1]; }
    stats[4]++; return &g_surf[((r->y >> 7) & 7) * 8 + ((r->x >> 7) & 7)];
}

/* recs grows to the biggest block seen (a few thousand records, not a fixed 40000: the converter also
 * runs on a 4 MB Atari) */
static void distribute_stream(const uint8_t *d, long n, long *pos, int nblocks, rec_t **precs, int *ncap, int *stats)
{
    for (int b = 0; b < nblocks; b++){
        int want = *pos + 2 <= n ? (d[*pos] | (d[*pos + 1] << 8)) : 0;
        if (want > *ncap){
            rec_t *g = realloc(*precs, sizeof(rec_t) * (size_t)want);
            if (!g){ printf("mksave: out of memory (block %d, %d records)\n", b, want); exit(2); }
            *precs = g; *ncap = want; }
        rec_t *recs = *precs;
        int cnt = parse_block(d, n, pos, recs);
        if (cnt < 0){ printf("mksave: block %d truncated\n", b); exit(2); }
        for (int k = 0; k < cnt; k++){
            if (recs[k].parent >= 0) continue;                    /* emitted with its container */
            emit_tree(dest_of(&recs[k], stats), recs, k);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3){ printf("usage: %s <u6dir> <out.000> [avatar name]\n", argv[0]); return 2; }
    const char *dir = argv[1], *out = argv[2], *name = argc > 3 ? argv[3] : "Avatar";
    {   const char *id = getenv("MKWORLD_GAME"); long n; uint8_t *probe;
        if (!id || !*id){ id = "u6";
            if ((probe = slurp(dir, "SAVAGE.EXE", &n))){ free(probe); id = "savage"; }
            else if ((probe = slurp(dir, "MARTIAN.EXE", &n))){ free(probe); id = "martian"; } }
        for (int i = 0; i < 3; i++) if (!strcmp(id, GAMES[i].id)) g_game = &GAMES[i];
        printf("mksave: game %s\n", g_game->id); }
    long sn, dn;
    uint8_t *surf = unlzw(dir, "LZOBJBLK", &sn); if (!surf) return 2;
    uint8_t *dng  = unlzw(dir, "LZDNGBLK", &dn); if (!dng) return 2;
    rec_t *recs = NULL; int ncap = 0;
    int stats[5] = {0};
    long pos = 0;
    distribute_stream(surf, sn, &pos, 64, &recs, &ncap, stats);
    if (pos != sn) printf("mksave: LZOBJBLK: %ld trailing bytes after 64 blocks\n", sn - pos);
    free(surf);
    pos = 0;
    distribute_stream(dng, dn, &pos, 5, &recs, &ncap, stats);
    free(recs);
    long objlist_size = dn - pos;
    if (objlist_size < 0x1c73){ printf("mksave: objlist too short (%ld)\n", objlist_size); return 2; }   /* every game's reaches past 0x1c72 */
    uint8_t *objlist = dng + pos;
    printf("mksave: carried/readied %d, eggs %d, surface %d, dungeon %d, other %d; objlist %ld bytes\n",
           stats[0], stats[1], stats[2], stats[3], stats[4], objlist_size);

    /* new-game objlist fields (what Nuvie's update_objlist_for_new_game sets from its chargen;
     * ours re-writes them from character creation at save time): name @0xf00 (13 chars + NUL),
     * gender @0x1c71 (0 male), portrait @0x1c72, str/dex/int @0x901/0xa01/0xb01, exp @0xc02 (u16),
     * level @0xff2, magic @0x13f2 (objlist.md). */
    {   size_t len = strlen(name); if (len > 13) len = 13;
        const int str = 15, dex = 15, in = 15;
        memset(objlist + 0xf00, 0, 14); memcpy(objlist + 0xf00, name, len);
        objlist[0x901] = str; objlist[0xa01] = dex; objlist[0xb01] = in;
        if (g_game == &GAMES[0]){                                  /* U6 */
            objlist[0x1c71] = 0; objlist[0x1c72] = 1;
            objlist[0xc02] = 0x72; objlist[0xc03] = 0x01;          /* 370 */
            objlist[0xff2] = 3; objlist[0x13f2] = (uint8_t)(2 * in); }
        else if (g_game == &GAMES[1]){                             /* SE */
            objlist[0xe01] = (uint8_t)(2 * str + in); objlist[0x14f2] = dex; }
        else {                                                     /* MD */
            objlist[0xc02] = 600 & 0xff; objlist[0xc03] = 600 >> 8;
            objlist[0xe01] = (uint8_t)(2 * str + 4 * 24); objlist[0xff2] = 4;
            objlist[0x14f2] = dex; objlist[0x15f2] = 20; } }
    {   const uint8_t *pb = objlist + 0x100 + 3;
        int x = pb[0] | ((pb[1] & 3) << 8), y = (pb[1] >> 2) | ((pb[2] & 0xf) << 6);
        int party = 0; for (int i = 1; i < 256; i++) if (objlist[0x800 + i] & 0x80) party++;
        printf("mksave: avatar at (%d,%d) %02d:%02d, party of %d\n", x, y, objlist[0x1bf4], objlist[0x1bf3], party); }

    FILE *f = fopen(out, "wb"); if (!f){ printf("mksave: cannot write %s\n", out); return 2; }
    uint8_t hdr[4] = { 0x00, 0x01, g_game->tag[0], g_game->tag[1] };   /* version 256, the game tag, LE */
    fwrite(hdr, 1, 4, f);
    chunk_write(f, &g_inv); chunk_write(f, &g_eggs);
    for (int i = 0; i < 64; i++) chunk_write(f, &g_surf[i]);
    for (int i = 0; i < 5; i++) chunk_write(f, &g_dng[i]);
    uint8_t sz[4] = { (uint8_t)objlist_size, (uint8_t)(objlist_size >> 8), (uint8_t)(objlist_size >> 16), (uint8_t)(objlist_size >> 24) };
    fwrite(sz, 1, 4, f);
    fwrite(objlist, 1, (size_t)objlist_size, f);
    /* ScummVM extended save header (engines/metaengine.cpp appendExtendedSaveToStream, v4):
     * "SVMCR\0" u8 version=4, u32 LE date (day<<24|month<<16|year), u16 LE time (hour<<8|min),
     * u32 LE playtime, pascal description, u8 isAutosave, a THMB block (graphics/thumbnail.cpp:
     * "THMB" u32 BE size, u8 version=2, u16 BE w,h, PixelFormat 9 bytes, w*h u16 BE RGB565),
     * then u32 LE = the header's file offset (read from the end of the file). A 16x10 black
     * thumbnail keeps the trailer ~400 bytes (ScummVM's own is 160x100 = 32 KB of RAM on target). */
    {   long hpos = ftell(f);
        time_t now = time(NULL); struct tm *tmv = localtime(&now);
        int year = tmv ? tmv->tm_year + 1900 : 2026, mon = tmv ? tmv->tm_mon + 1 : 1, day = tmv ? tmv->tm_mday : 1;
        int hour = tmv ? tmv->tm_hour : 0, min = tmv ? tmv->tm_min : 0;
        const char *desc = "New game";
        uint8_t h[64]; int p = 0;
        memcpy(h + p, "SVMCR", 6); p += 6;                          /* id incl. NUL */
        h[p++] = 4;                                                  /* EXTENDED_SAVE_VERSION */
        uint32_t date = ((uint32_t)(day & 0xff) << 24) | ((uint32_t)(mon & 0xff) << 16) | (uint32_t)(year & 0xffff);
        h[p++] = (uint8_t)date; h[p++] = (uint8_t)(date >> 8); h[p++] = (uint8_t)(date >> 16); h[p++] = (uint8_t)(date >> 24);
        h[p++] = (uint8_t)(min & 0xff); h[p++] = (uint8_t)(hour & 0xff);   /* u16 LE: hour<<8 | min */
        h[p++] = 0; h[p++] = 0; h[p++] = 0; h[p++] = 0;              /* playtime ms */
        h[p++] = (uint8_t)strlen(desc); memcpy(h + p, desc, strlen(desc)); p += (int)strlen(desc);
        h[p++] = 0;                                                  /* not an autosave */
        fwrite(h, 1, (size_t)p, f);
        const int tw = 16, th = 10;
        uint32_t tsize = (uint32_t)(4 + 4 + 1 + 2 + 2 + 9 + tw * th * 2);
        uint8_t t[32]; p = 0;
        memcpy(t + p, "THMB", 4); p += 4;
        t[p++] = (uint8_t)(tsize >> 24); t[p++] = (uint8_t)(tsize >> 16); t[p++] = (uint8_t)(tsize >> 8); t[p++] = (uint8_t)tsize;
        t[p++] = 2;                                                  /* THMB_VERSION */
        t[p++] = 0; t[p++] = (uint8_t)tw; t[p++] = 0; t[p++] = (uint8_t)th;
        t[p++] = 2; t[p++] = 3; t[p++] = 2; t[p++] = 3; t[p++] = 8;  /* bpp 2, RGB565 losses */
        t[p++] = 11; t[p++] = 5; t[p++] = 0; t[p++] = 0;             /* shifts */
        fwrite(t, 1, (size_t)p, f);
        for (int i = 0; i < tw * th; i++){ uint8_t px[2] = {0, 0}; fwrite(px, 1, 2, f); }
        uint8_t hp[4] = { (uint8_t)hpos, (uint8_t)(hpos >> 8), (uint8_t)(hpos >> 16), (uint8_t)(hpos >> 24) };
        fwrite(hp, 1, 4, f); }
    long total = ftell(f); fclose(f);
    printf("mksave: wrote %s (%ld bytes)\n", out, total);
    free(dng);                                    /* the converter runs the tools in one process */
    free(g_inv.buf); free(g_eggs.buf);
    for (int i = 0; i < 64; i++) free(g_surf[i].buf);
    for (int i = 0; i < 5; i++) free(g_dng[i].buf);
    memset(&g_inv, 0, sizeof g_inv); memset(&g_eggs, 0, sizeof g_eggs);
    memset(g_surf, 0, sizeof g_surf); memset(g_dng, 0, sizeof g_dng);
    return 0;
}
