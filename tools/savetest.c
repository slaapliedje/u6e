// SPDX-License-Identifier: GPL-2.0-or-later
/* savetest.c — host unit test for the save template patchers that change the object REGION's
 * shape (engine/save.c, from docs/rules/cannons.md §4 + save_load.md §5): in-place record
 * deletion (chunk count, chunk-local container links, buffer tail, ScummVM trailer offset) and
 * the hull-strength qty patch. A synthetic ScummVM-layout buffer, no game data. Run by
 * `make savetest`. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "save.h"
#include "platfile.h"

/* save.c's file writer is linked but not exercised here */
plat_file plat_fcreate(const char *path){ (void)path; return PLAT_BADFILE; }
int       plat_fclose(plat_file f){ (void)f; return 0; }
int32_t   plat_fwrite(plat_file f, const void *src, int32_t len){ (void)f; (void)src; return len; }

static int fails;
#define OK(cond, msg) do { if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint8_t buf[4096];
static int32_t n;

static void put16(int32_t at, int v){ buf[at] = (uint8_t)v; buf[at+1] = (uint8_t)(v >> 8); }
static void put32(int32_t at, uint32_t v){ for (int i = 0; i < 4; i++) buf[at+i] = (uint8_t)(v >> (8*i)); }
static int  get16(int32_t at){ return buf[at] | (buf[at+1] << 8); }
static uint32_t get32(int32_t at){ return buf[at] | (buf[at+1] << 8) | ((uint32_t)buf[at+2] << 16) | ((uint32_t)buf[at+3] << 24); }

/* an 8-byte record: status, x, y (10 bits each), obj_n + frame, qty, quality */
static void rec(int32_t at, int status, int x, int y, int obj_n, int frame, int qty, int quality)
{
    buf[at+0] = (uint8_t)status;
    buf[at+1] = (uint8_t)(x & 0xff);
    buf[at+2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
    buf[at+3] = (uint8_t)(((y >> 6) & 0x0f));
    buf[at+4] = (uint8_t)(obj_n & 0xff);
    buf[at+5] = (uint8_t)(((obj_n >> 8) & 3) | ((frame & 0x3f) << 2));
    buf[at+6] = (uint8_t)qty;
    buf[at+7] = (uint8_t)quality;
}
/* an in-container record linked to chunk-local index `parent` (objblk.md §2: parent = (y6<<10)|x) */
static void child(int32_t at, int parent, int obj_n)
{
    rec(at, 0x08, parent & 0x3ff, (parent >> 10) & 0x3f, obj_n, 0, 1, 0);
}
static int child_link(int32_t at)
{
    int x = buf[at+1] | ((buf[at+2] & 3) << 8), y = (buf[at+2] >> 2) | ((buf[at+3] & 0x0f) << 6);
    return ((y & 0x3f) << 10) | x;
}

/* Layout: [ver][tag] [inv: 0] [eggs: 0] [surface 0: 4 records] [surface 1..63: 0] [dungeon 1..5: 0]
 * [u32 objlist size = 16][16 bytes] ['SVMCR' + 3 bytes] [u32 header offset]. Surface chunk 0 holds
 * (x,y) < 128: index 0 = a ship's back part at (2,2) (to be deleted), 1 = a chest at (5,5),
 * 2 = a key inside the chest (link 1), 3 = the ship's centre at (2,3). */
static int32_t build(void)
{
    memset(buf, 0, sizeof buf);
    int32_t p = 0;
    put16(p, 256); put16(p+2, 0x3655); p += 4;              /* version + 'U6' tag */
    put16(p, 0); p += 2;                                     /* actor inventories: none */
    put16(p, 0); p += 2;                                     /* eggs: none */
    put16(p, 4); p += 2;                                     /* surface chunk 0 */
    rec(p, 0x00, 2, 2, 412, 16, 0, 0);   p += 8;             /* [0] ship back part */
    rec(p, 0x00, 5, 5,  98,  1, 0, 7);   p += 8;             /* [1] chest */
    child(p, 1, 64);                     p += 8;             /* [2] key inside the chest (link -> 1) */
    rec(p, 0x00, 2, 3, 412,  8, 100, 0); p += 8;             /* [3] ship centre, hull 100 */
    for (int i = 1; i < 64; i++){ put16(p, 0); p += 2; }     /* the other surface chunks */
    for (int i = 0; i < 5;  i++){ put16(p, 0); p += 2; }     /* dungeon chunks */
    put32(p, 16); p += 4; memset(buf + p, 0xaa, 16); p += 16;/* objlist */
    int32_t hdr = p;
    static const uint8_t magic[8] = { 'S','V','M','C','R', 1, 2, 3 };
    memcpy(buf + p, magic, 8); p += 8;                       /* a trailer header */
    put32(p, (uint32_t)hdr); p += 4;                         /* the file's last u32 -> the header */
    return p;
}


/* ---- M)ove into a container: sav_recontain (usecode_use.md "M)ove") ---------------------------
 * Layout 2: surface chunk 0 (x,y < 128): [0] open chest (5,5), [1] key in it, [2] dagger (6,5),
 * [3] bag (7,7), [4] gold in the bag, [5] gem in the bag; surface chunk 1 (x in 128..255):
 * [0] candle (130,5), [1] basket (130,6). Same trailer. */
static int32_t build2(void)
{
    memset(buf, 0, sizeof buf);
    int32_t p = 0;
    put16(p, 256); put16(p+2, 0x3655); p += 4;
    put16(p, 0); p += 2; put16(p, 0); p += 2;
    put16(p, 6); p += 2;
    rec(p, 0x00, 5, 5,  98, 0, 0, 7); p += 8;                 /* [0] open chest */
    child(p, 0, 64);                  p += 8;                 /* [1] key in the chest */
    rec(p, 0x00, 6, 5,  38, 0, 1, 0); p += 8;                 /* [2] dagger */
    rec(p, 0x00, 7, 7, 188, 0, 0, 0); p += 8;                 /* [3] bag */
    child(p, 3, 88);                  p += 8;                 /* [4] gold in the bag */
    child(p, 3, 76);                  p += 8;                 /* [5] gem in the bag */
    put16(p, 2); p += 2;
    rec(p, 0x00, 130, 5, 122, 0, 1, 0); p += 8;               /* chunk 1 [0] candle */
    rec(p, 0x00, 130, 6, 191, 0, 0, 0); p += 8;               /* chunk 1 [1] basket */
    for (int i = 2; i < 64; i++){ put16(p, 0); p += 2; }
    for (int i = 0; i < 5;  i++){ put16(p, 0); p += 2; }
    put32(p, 16); p += 4; memset(buf + p, 0xaa, 16); p += 16;
    int32_t hdr = p;
    static const uint8_t magic[8] = { 'S','V','M','C','R', 1, 2, 3 };
    memcpy(buf + p, magic, 8); p += 8;
    put32(p, (uint32_t)hdr); p += 4;
    return p;
}
static int rec_on(int32_t cpos, int k){ int32_t at = cpos + 2 + k*8; return buf[at+4] | ((buf[at+5] & 3) << 8); }
static int trailer_ok(void){ return memcmp(buf + get32(n - 4), "SVMCR", 5) == 0; }

static void recontain_tests(void)
{
    n = build2();
    int32_t surf = sav_surface_off(buf, n), c0 = surf, c1 = surf + 2 + 6*8, cap = (int32_t)sizeof buf;
    int k = -1;
    OK(sav_locate_onmap(buf, n, surf, 0, 6, 5, 38, &k) == c0 && k == 2, "locate: the dagger is chunk 0 record 2");
    OK(sav_locate_onmap(buf, n, surf, 0, 130, 5, 122, &k) == c1 && k == 0, "locate: the candle is chunk 1 record 0");
    OK(sav_find_child(buf, n, c0, 0, 64, -1) == 1, "find_child: the key under the chest");
    OK(sav_find_child(buf, n, c0, 3, 76, -1) == 5 && sav_find_child(buf, n, c0, 0, 76, -1) == -1, "find_child: the gem is under the bag, not the chest");

    /* 1) the dagger into the chest: same chunk, after the holder */
    int32_t hdr0 = (int32_t)get32(n - 4), n0 = n;
    int32_t nn = sav_recontain(buf, n, cap, c0, 2, c0, 0);
    OK(nn == n0, "dagger -> chest: the buffer keeps its length");
    n = nn;
    OK(get16(c0) == 6, "chunk 0 still holds 6 records");
    OK(rec_on(c0, 2) == 188 && child_link(c0 + 2 + 3*8) == 2 && child_link(c0 + 2 + 4*8) == 2, "the bag moved up to 2 and its contents link to it");
    OK(rec_on(c0, 5) == 38 && (buf[c0 + 2 + 5*8] & 0x18) == 0x08 && child_link(c0 + 2 + 5*8) == 0, "the dagger is the last record, in-container, linked to the chest");
    OK(sav_find_child(buf, n, c0, 0, 38, -1) == 5, "find_child sees the dagger under the chest");
    OK(sav_locate_onmap(buf, n, surf, 0, 6, 5, 38, &k) == -1, "no on-map dagger record remains");
    OK((int32_t)get32(n - 4) == hdr0 && trailer_ok(), "trailer untouched by an in-chunk move");

    /* 2) the bag with its contents into the chest */
    nn = sav_recontain(buf, n, cap, c0, 2, c0, 0);
    OK(nn == n, "bag -> chest: length unchanged");
    n = nn;
    OK(rec_on(c0, 2) == 38 && child_link(c0 + 2 + 2*8) == 0, "the dagger moved up to 2, still under the chest");
    OK(rec_on(c0, 3) == 188 && child_link(c0 + 2 + 3*8) == 0, "the bag is record 3 inside the chest");
    OK(rec_on(c0, 4) == 88 && child_link(c0 + 2 + 4*8) == 3 && rec_on(c0, 5) == 76 && child_link(c0 + 2 + 5*8) == 3, "gold + gem followed the bag and link to its new index");
    OK(sav_recontain(buf, n, cap, c0, 3, c0, 4) == -1 && n == nn, "a holder cannot go into its own contents (refused, nothing changed)");

    /* 3) cross-chunk, backwards: the candle (chunk 1) into the chest (chunk 0) */
    nn = sav_recontain(buf, n, cap, c1, 0, c0, 0);
    OK(nn == n, "candle -> chest: length unchanged");
    n = nn;
    OK(get16(c0) == 7 && rec_on(c0, 6) == 122 && child_link(c0 + 2 + 6*8) == 0, "chunk 0 grew to 7: the candle is last, under the chest");
    c1 = c0 + 2 + 7*8;
    OK(get16(c1) == 1 && rec_on(c1, 0) == 191, "chunk 1 shrank to the basket alone");
    OK(sav_locate_onmap(buf, n, surf, 0, 130, 6, 191, &k) == c1 && k == 0, "the basket is located at its new chunk offset");
    OK(trailer_ok() && (int32_t)get32(n - 4) == hdr0, "trailer consistent after the cross-chunk move");

    /* 4) cross-chunk, forwards: the bag subtree (chunk 0) into the basket (chunk 1) */
    nn = sav_recontain(buf, n, cap, c0, 3, c1, 0);
    OK(nn == n, "bag -> basket: length unchanged");
    n = nn;
    OK(get16(c0) == 4 && rec_on(c0, 3) == 122 && child_link(c0 + 2 + 3*8) == 0, "chunk 0: chest, key, dagger, candle");
    c1 = c0 + 2 + 4*8;
    OK(get16(c1) == 4 && rec_on(c1, 0) == 191, "chunk 1 now holds the basket + 3");
    OK(rec_on(c1, 1) == 188 && child_link(c1 + 2 + 1*8) == 0, "the bag is in the basket");
    OK(rec_on(c1, 2) == 88 && child_link(c1 + 2 + 2*8) == 1 && rec_on(c1, 3) == 76 && child_link(c1 + 2 + 3*8) == 1, "gold + gem link to the bag's index in chunk 1");
    int32_t ol = sav_objlist_off(buf, n);
    OK(ol > 0 && get32(ol - 4) == 16 && buf[ol] == 0xaa && trailer_ok(), "objlist + trailer intact after four moves");

    /* 5) the count patch on a contained stackable */
    sav_set_rec_qty(buf, n, c1, 2, 0x1234);
    OK(buf[c1 + 2 + 2*8 + 6] == 0x34 && buf[c1 + 2 + 2*8 + 7] == 0x12, "qty16 written little-endian");
    /* 6) delete_at on a childless contained record */
    nn = sav_delete_at(buf, n, c1, 3);
    OK(nn == n - 8 && get16(c1) == 3, "delete_at removed the gem");
    n = nn;
    OK(sav_delete_at(buf, n, c1, 1) == -1, "delete_at refuses the bag (it still holds the gold)");
}

int main(void)
{
    n = build();
    int32_t surf = sav_surface_off(buf, n), ol0 = sav_objlist_off(buf, n);
    OK(surf == 8, "surface chunks start after the two empty chunks");
    OK(ol0 > 0 && get32(ol0 - 4) == 16, "objlist located");

    /* hull strength: the centre's qty byte */
    OK(sav_patch_obj_qty(buf, n, surf, 0, 2, 3, 412, 77) == 1, "hull qty patched on the centre record");
    OK(buf[surf + 2 + 3*8 + 6] == 77, "qty byte = 77");
    OK(sav_patch_obj_qty(buf, n, surf, 0, 9, 9, 412, 1) == 0, "no record there -> 0");

    /* a container is refused */
    OK(sav_delete_record(buf, n, surf, 0, 5, 5, 98) == -1, "deleting a record with children is refused");

    /* delete the back part at index 0: count 4 -> 3, the key's link 1 -> 0, tail moved, trailer fixed */
    int32_t hdr0 = (int32_t)get32(n - 4);
    int32_t n1 = sav_delete_record(buf, n, surf, 0, 2, 2, 412);
    OK(n1 == n - 8, "buffer shrank by one record");
    n = n1;
    OK(get16(surf) == 3, "chunk count 4 -> 3");
    OK(buf[surf + 2 + 0*8 + 4] == 98, "the chest is now record 0");
    OK(child_link(surf + 2 + 1*8) == 0, "the key's container link was re-indexed 1 -> 0");
    OK((buf[surf + 2 + 2*8 + 4] | ((buf[surf + 2 + 2*8 + 5] & 3) << 8)) == 412 && buf[surf + 2 + 2*8 + 6] == 77,
       "the centre (hull 77) is now record 2");
    int32_t ol1 = sav_objlist_off(buf, n);
    OK(ol1 == ol0 - 8 && get32(ol1 - 4) == 16 && buf[ol1] == 0xaa, "objlist moved down 8 and is intact");
    int32_t hdr1 = (int32_t)get32(n - 4);
    OK(hdr1 == hdr0 - 8 && memcmp(buf + hdr1, "SVMCR", 5) == 0, "trailer header offset corrected and points at SVMCR");

    /* deleting the last record of the chunk */
    int32_t n2 = sav_delete_record(buf, n, surf, 0, 2, 3, 412);
    OK(n2 == n - 8 && get16(surf) == 2, "centre deleted: count 3 -> 2");
    n = n2;
    OK(child_link(surf + 2 + 1*8) == 0, "a link below the deleted index is untouched");
    OK(memcmp(buf + get32(n - 4), "SVMCR", 5) == 0, "trailer still consistent after a second deletion");
    OK(sav_delete_record(buf, n, surf, 0, 2, 3, 412) == -1, "deleting it again finds nothing");

    recontain_tests();

    if (fails){ printf("\n%d SAVE TEST(S) FAILED\n", fails); return 1; }
    printf("\nALL SAVE TESTS PASS\n");
    return 0;
}
