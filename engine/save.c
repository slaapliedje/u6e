// SPDX-License-Identifier: GPL-2.0-or-later
/* save.c — on-target ScummVM/Nuvie save writer (see save.h + docs/formats/
 * nuvie_save.md). Patches a template save with live engine state and writes it.
 * All multi-byte save fields are little-endian; we read/write them byte-wise so
 * the code is correct on the big-endian 68000 target with no swapping. */
#include "save.h"
#include <string.h>
#include "platfile.h"

static uint16_t le16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }

/* advance past one super-chunk [u16 count][count*8]; returns new offset or -1 */
static int32_t skip_chunk(const uint8_t *d, int32_t n, int32_t pos)
{
    if (pos + 2 > n) return -1;
    int32_t need = 2 + (int32_t)le16(d + pos) * 8;
    if (pos + need > n) return -1;
    return pos + need;
}

int32_t sav_objlist_off(const uint8_t *d, int32_t n)
{
    int32_t pos = 4;                                   /* version + game_tag (LE u16 each) */
    pos = skip_chunk(d, n, pos); if (pos < 0) return -1;   /* actor inventories */
    pos = skip_chunk(d, n, pos); if (pos < 0) return -1;   /* eggs */
    for (int i = 0; i < 64; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return -1; }  /* surface */
    for (int i = 0; i < 5;  i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return -1; }  /* dungeons */
    if (pos + 4 > n) return -1;                         /* u32 objlist_size */
    return pos + 4;
}

/* Offset of the 64 surface super-chunks (after the version + actor-inventory + eggs chunks).
 * Each super-chunk is [u16 count][count*8]; order is row-major (chunk = (y>>7)*8 + (x>>7)). */
int32_t sav_surface_off(const uint8_t *d, int32_t n)
{
    int32_t pos = 4;                                   /* version + game_tag */
    pos = skip_chunk(d, n, pos); if (pos < 0) return -1;   /* actor inventories */
    pos = skip_chunk(d, n, pos); if (pos < 0) return -1;   /* eggs */
    return pos;
}

/* ---- READ side (in-game load; docs/rules/save_load.md) --------------------------------- */
/* The actor-inventory super-chunk starts right after the two u16 header words. */
int32_t sav_inv_off(const uint8_t *d, int32_t n){ (void)d; (void)n; return 4; }
int32_t sav_inv_end(const uint8_t *d, int32_t n){ return skip_chunk(d, n, 4); }
/* End of the 64 surface super-chunks (= start of the 5 dungeon chunks). */
int32_t sav_surface_end(const uint8_t *d, int32_t n)
{
    int32_t pos = sav_surface_off(d, n); if (pos < 0) return -1;
    for (int i = 0; i < 64; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return -1; }
    return pos;
}

int sav_level_span(const uint8_t *d, int32_t n, int level, int32_t *off, int32_t *end)
{
    if (level == 0){ *off = sav_surface_off(d, n); *end = sav_surface_end(d, n); return *off >= 0 && *end > *off; }
    if (level < 1 || level > 5) return 0;
    int32_t pos = sav_surface_end(d, n); if (pos < 0) return 0;
    for (int i = 1; i < level; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return 0; }
    *off = pos; *end = skip_chunk(d, n, pos);
    return *end > 0;
}

int sav_find_onmap(const uint8_t *d, int32_t n, int quality, int (*ok)(int obj_n), int skip_level, int *x, int *y, int *level)
{
    int32_t pos = sav_surface_off(d, n); int found = 0;
    if (pos < 0) return 0;
    for (int c = 0; c < 64 + 5; c++){
        int32_t next = skip_chunk(d, n, pos); if (next < 0) return found;
        int lv = c < 64 ? 0 : c - 63, cnt = le16(d + pos);
        for (int i = 0; i < cnt && lv != skip_level; i++){
            const uint8_t *r = d + pos + 2 + i * 8; int full = 0;
            if ((r[0] & 0x18) || r[7] != (uint8_t)quality || !ok(r[4] | ((r[5] & 3) << 8))) continue;
            for (int k = i + 1; k < cnt && !full; k++){ const uint8_t *q = d + pos + 2 + k * 8;   /* something inside it (objblk.md: parent link) */
                full = (q[0] & 0x18) == 0x08 && (q[1] | ((q[2] & 3) << 8) | ((q[2] >> 2 & 0x3f) << 10)) == i; }
            if (found && !full) continue;
            *x = r[1] | ((r[2] & 3) << 8); *y = (r[2] >> 2) | ((r[3] & 0xf) << 6); *level = lv; found = 1;
            if (full) return 1; }
        pos = next; }
    return found;
}

/* start (count word) of the chunk holding an on-map record of `level` at (x,y): a surface
 * record's row-major super-chunk, or dungeon chunk level-1; -1 if the layout doesn't parse */
static int32_t chunk_of(const uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y)
{
    int32_t pos = surf_off;
    int skip = (level == 0) ? (((y >> 7) & 7) * 8 + ((x >> 7) & 7)) : (64 + level - 1);
    if (level < 0 || level > 5) return -1;
    for (int i = 0; i < skip; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return -1; }
    return pos;
}

/* Patch the STATE of the on-map object whose record sits at its ORIGINAL position (ox,oy) with
 * the given obj_n, in that position's row-major super-chunk. The object database is a flat fixed
 * record set — a door toggle, a sailed boat or a bought boat changes only the record's frame /
 * position / quality in place (no add/remove), so counts and all following offsets are preserved
 * (nuvie_save.md). Loaders place a record by its own x/y, not by the chunk it sits in, so a boat
 * that sailed into another super-chunk is still found by everyone. The frame is the top 6 bits of
 * byte 5; z (byte 3 high nibble) is kept. Returns 1 if a matching record was found + patched. */
int sav_patch_obj_state(uint8_t *d, int32_t n, int32_t surf_off, int level, int ox, int oy, int obj_n,
                        int x, int y, int frame, int quality, int new_obj_n)
{
    int32_t pos = chunk_of(d, n, surf_off, level, ox, oy);
    if (pos < 0 || pos + 2 > n) return 0;
    int cnt = le16(d + pos); pos += 2;
    for (int k = 0; k < cnt; k++){
        if (pos + 8 > n) return 0;
        uint8_t *r = d + pos; pos += 8;
        if ((r[0] & 0x18) != 0) continue;              /* only on-map records (location bits clear) */
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int ron = r[4] | ((r[5] & 3) << 8);
        if (rx == ox && ry == oy && ron == obj_n){
            int won = (new_obj_n >= 0) ? new_obj_n : ron;
            r[1] = (uint8_t)(x & 0xff);
            r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
            r[3] = (uint8_t)((r[3] & 0xf0) | ((y >> 6) & 0x0f));
            r[4] = (uint8_t)(won & 0xff);
            r[5] = (uint8_t)((won >> 8) & 3) | (uint8_t)((frame & 0x3f) << 2);  /* obj_n (maybe retyped) + frame */
            if (quality >= 0) r[7] = (uint8_t)quality;    /* <0 = keep (a stackable's byte 7 is its count's high byte) */
            return 1;
        }
    }
    return 0;
}

/* Hull strength write-back (docs/rules/cannons.md §4): the qty byte of the on-map record at
 * (x,y,obj_n) of `level`. Returns 1 if patched. */
int sav_patch_obj_qty(uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y, int obj_n, int qty)
{
    int32_t pos = chunk_of(d, n, surf_off, level, x, y);
    if (pos < 0 || pos + 2 > n) return 0;
    int cnt = le16(d + pos); pos += 2;
    for (int k = 0; k < cnt; k++){
        if (pos + 8 > n) return 0;
        uint8_t *r = d + pos; pos += 8;
        if ((r[0] & 0x18) != 0) continue;
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int ron = r[4] | ((r[5] & 3) << 8);
        if (rx == x && ry == y && ron == obj_n){ r[6] = (uint8_t)qty; return 1; }
    }
    return 0;
}

/* DESTROY (docs/rules/cannons.md §4): delete the on-map record at (x,y,obj_n) of `level` from the
 * template IN PLACE. The chunk's count drops by one, every in-container link of that chunk that
 * pointed past the deleted index is decremented (links are chunk-local record indices,
 * objblk.md §2), the buffer tail moves down 8 bytes and a ScummVM trailer's header offset (the
 * last u32) is corrected. Only a childless record may be deleted (a record linked as a parent is
 * refused). Returns the new buffer length, or -1 if nothing was deleted. */
/* Delete record k of the chunk whose count word is at cpos: refuses a container (children would be
 * orphaned), re-indexes the chunk's later container links, shifts the file down 8 bytes, fixes the
 * count word and a ScummVM trailer's header offset. Returns the new length, or -1. */
static int32_t delete_record_k(uint8_t *d, int32_t n, int32_t cpos, int k)
{
    int cnt = le16(d + cpos); int32_t base = cpos + 2;
    if (k < 0 || k >= cnt || base + (int32_t)cnt * 8 > n) return -1;
    for (int j = 0; j < cnt; j++){                         /* refuse a container; re-index later links */
        uint8_t *r = d + base + j * 8;
        if ((r[0] & 0x18) != 0x08) continue;
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int link = ((ry & 0x3f) << 10) | rx;
        if (link == k) return -1;
    }
    int32_t ol = sav_objlist_off(d, n); if (ol < 4) return -1;
    int32_t osz = (int32_t)(d[ol-4] | (d[ol-3] << 8) | ((uint32_t)d[ol-2] << 16) | ((uint32_t)d[ol-1] << 24));
    int32_t tail = ol + osz; if (tail > n) return -1;
    int32_t hp = -1;
    if (tail + 4 <= n){                                    /* a trailer: its header offset is the file's last u32 */
        const uint8_t *e = d + n - 4;
        hp = (int32_t)(e[0] | (e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24));
        if (!(hp >= tail && hp + 6 <= n - 4 && d[hp] == 'S' && d[hp+1] == 'V' && d[hp+2] == 'M' && d[hp+3] == 'C' && d[hp+4] == 'R')) hp = -1;
    }
    for (int j = 0; j < cnt; j++){
        uint8_t *r = d + base + j * 8;
        if ((r[0] & 0x18) != 0x08) continue;
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int link = ((ry & 0x3f) << 10) | rx;
        if (link > k){ link--; rx = link & 0x3ff; ry = (ry & ~0x3f) | ((link >> 10) & 0x3f);
            r[1] = (uint8_t)(rx & 0xff);
            r[2] = (uint8_t)(((rx >> 8) & 3) | ((ry & 0x3f) << 2));
            r[3] = (uint8_t)((r[3] & 0xf0) | ((ry >> 6) & 0x0f)); }
    }
    int32_t at = base + k * 8;
    memmove(d + at, d + at + 8, (size_t)(n - at - 8));
    n -= 8;
    d[cpos] = (uint8_t)((cnt - 1) & 0xff); d[cpos+1] = (uint8_t)(((cnt - 1) >> 8) & 0xff);
    if (hp >= 0){ int32_t np = hp - 8; uint8_t *e = d + n - 4;
        e[0] = (uint8_t)np; e[1] = (uint8_t)(np >> 8); e[2] = (uint8_t)(np >> 16); e[3] = (uint8_t)(np >> 24); }
    return n;
}
/* index of the on-map record at (x,y) with obj_n in the chunk at cpos, or -1 */
static int find_onmap_k(const uint8_t *d, int32_t n, int32_t cpos, int x, int y, int obj_n)
{
    if (cpos < 0 || cpos + 2 > n) return -1;
    int cnt = le16(d + cpos); int32_t base = cpos + 2;
    if (base + (int32_t)cnt * 8 > n) return -1;
    for (int k = 0; k < cnt; k++){
        const uint8_t *r = d + base + k * 8;
        if ((r[0] & 0x18) != 0) continue;
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int ron = r[4] | ((r[5] & 3) << 8);
        if (rx == x && ry == y && ron == obj_n) return k;
    }
    return -1;
}
int32_t sav_delete_record(uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y, int obj_n)
{
    int32_t cpos = chunk_of(d, n, surf_off, level, x, y);
    int k = find_onmap_k(d, n, cpos, x, y, obj_n);
    return (k < 0) ? -1 : delete_record_k(d, n, cpos, k);
}
int32_t sav_delete_contained(uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y, int cont_obj_n, int child_obj_n)
{
    int32_t cpos = chunk_of(d, n, surf_off, level, x, y);
    int P = find_onmap_k(d, n, cpos, x, y, cont_obj_n); if (P < 0) return -1;
    int cnt = le16(d + cpos); int32_t base = cpos + 2;
    for (int k = 0; k < cnt; k++){
        const uint8_t *r = d + base + k * 8;
        if ((r[0] & 0x18) != 0x08) continue;
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int link = ((ry & 0x3f) << 10) | rx;
        int ron = r[4] | ((r[5] & 3) << 8);
        if (link == P && ron == child_obj_n) return delete_record_k(d, n, cpos, k);
    }
    return -1;
}
/* record k of chunk cpos goes ON THE MAP at (x,y,z): its location bits clear and the link bytes
 * become coordinates (a broken container's contents spill; rules/combat.md §3b). */
void sav_spill_k(uint8_t *d, int32_t cpos, int k, int x, int y, int z)
{
    uint8_t *r = d + cpos + 2 + k * 8;
    r[0] = (uint8_t)(r[0] & ~0x18);
    r[1] = (uint8_t)(x & 0xff);
    r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
    r[3] = (uint8_t)(((y >> 6) & 0x0f) | ((z & 0x0f) << 4));
}
int32_t sav_delete_inv_record(uint8_t *d, int32_t n, int owner, int obj_n)
{
    int32_t cpos = sav_inv_off(d, n);
    if (cpos < 0 || cpos + 2 > n) return -1;
    int cnt = le16(d + cpos); int32_t base = cpos + 2;
    if (base + (int32_t)cnt * 8 > n) return -1;
    for (int k = 0; k < cnt; k++){
        const uint8_t *r = d + base + k * 8;
        if ((r[0] & 0x18) != 0x10) continue;                /* carried, not readied */
        int ro = r[1] | ((r[2] & 3) << 8);
        int ron = r[4] | ((r[5] & 3) << 8);
        if (ro == owner && ron == obj_n){ int32_t nn = delete_record_k(d, n, cpos, k); if (nn > 0) return nn; }   /* a container is refused: try the next */
    }
    return -1;
}

/* ---- carried-record reconciliation (save.h; docs/rules/save_load.md §5) ------------------- */

/* Iterate every record of the object region: the actor-inventory chunk (offset 4), the eggs
 * chunk and the 64 surface chunks. A container link is CHUNK-LOCAL (objblk.md §2), so the
 * iterator exposes the current chunk's base offset + record count for link resolution. */
typedef struct { uint8_t *d; int32_t n, pos, end, base; int cnt, k; } rec_it;

static void it_start(rec_it *it, uint8_t *d, int32_t n)
{
    it->d = d; it->n = n; it->pos = 4; it->end = sav_surface_end(d, n);
    it->base = 0; it->cnt = 0; it->k = 0;
}
/* next record, or NULL at the end of the region (it->k-1 is its index within it->base's chunk) */
static uint8_t *it_next(rec_it *it)
{
    while (it->k >= it->cnt){
        if (it->end < 0 || it->pos + 2 > it->end) return 0;
        it->cnt = le16(it->d + it->pos); it->base = it->pos + 2; it->k = 0;
        it->pos = it->base + (int32_t)it->cnt * 8;
        if (it->pos > it->end) return 0;
    }
    return it->d + it->base + (it->k++) * 8;
}

/* The party actor holding record r: its owner when carried/readied, else the owner at the root
 * of its container chain (<= 8 hops, chunk-local links). -1 when the root is on the map or the
 * holder isn't in the party. Sets *direct when r itself is the carried/readied record. */
static int rec_holder(const uint8_t *d, int32_t base, int cnt, const uint8_t *r,
                      const uint8_t *in_party, int nact, int *direct)
{
    *direct = 1;
    for (int hops = 0; hops < 8; hops++){
        int loc = r[0] & 0x18;
        if (loc == 0x10 || loc == 0x18){
            int owner = r[1] | ((r[2] & 3) << 8);
            return (owner >= 0 && owner < nact && in_party[owner]) ? owner : -1;
        }
        if (loc != 0x08) return -1;                    /* on-map root: not carried by anyone */
        int rx = r[1] | ((r[2] & 3) << 8), ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int link = ((ry & 0x3f) << 10) | rx;
        if (link >= cnt) return -1;
        r = d + base + link * 8; *direct = 0;
    }
    return -1;
}

static int rec_obj_n(const uint8_t *r){ return r[4] | ((r[5] & 3) << 8); }

int sav_unready_party(uint8_t *d, int32_t n, const uint8_t *in_party, int nact)
{
    rec_it it; uint8_t *r; int changed = 0;
    it_start(&it, d, n);
    while ((r = it_next(&it))){
        if ((r[0] & 0x18) != 0x18) continue;
        int owner = r[1] | ((r[2] & 3) << 8);
        if (owner >= nact || !in_party[owner]) continue;
        r[0] = (uint8_t)((r[0] & ~0x18) | 0x10); changed++;
    }
    return changed;
}

int sav_ready_item(uint8_t *d, int32_t n, int actor, int obj_n, const uint8_t *in_party, int nact)
{
    rec_it it; uint8_t *r, *best = 0; int best_score = -1, best_direct = 1;
    it_start(&it, d, n);
    while ((r = it_next(&it))){
        if (rec_obj_n(r) != obj_n) continue;
        int loc = r[0] & 0x18;
        if (loc != 0x10 && loc != 0x08) continue;      /* already readied (claimed) or on the map */
        int direct; int holder = rec_holder(d, it.base, it.cnt, r, in_party, nact, &direct);
        if (holder < 0) continue;
        int score = (holder == actor ? 4 : 0) + (r[6] <= 1 ? 2 : 0) + (direct ? 1 : 0);
        if (score > best_score){ best_score = score; best = r; best_direct = direct; }
    }
    if (!best) return 0;
    best[0] = (uint8_t)((best[0] & ~0x18) | 0x18);
    best[1] = (uint8_t)(actor & 0xff);
    if (best_direct) best[2] = (uint8_t)((best[2] & ~0x03) | ((actor >> 8) & 3));
    else { best[2] = (uint8_t)((actor >> 8) & 3); best[3] = 0; }   /* was a container link: clear it */
    return 1;
}

int sav_drop_item(uint8_t *d, int32_t n, int obj_n, int quality, int x, int y, int z, int frame,
                  int as_obj_n, const uint8_t *in_party, int nact)
{
    int won = (as_obj_n >= 0) ? as_obj_n : obj_n;
    rec_it it; uint8_t *r; int moved = 0;
    it_start(&it, d, n);
    while ((r = it_next(&it))){
        if (rec_obj_n(r) != obj_n || r[7] != (uint8_t)quality) continue;
        int loc = r[0] & 0x18;
        if (loc != 0x10 && loc != 0x08) continue;      /* readied records stay with their wearer */
        int direct; if (rec_holder(d, it.base, it.cnt, r, in_party, nact, &direct) < 0) continue;
        r[0] = (uint8_t)((r[0] & ~0x18) | 0x01);       /* location -> on the map, free to take again */
        r[1] = (uint8_t)(x & 0xff);
        r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
        r[3] = (uint8_t)(((y >> 6) & 0x0f) | ((z & 0x0f) << 4));
        r[4] = (uint8_t)(won & 0xff);
        r[5] = (uint8_t)(((won >> 8) & 3) | ((frame & 0x3f) << 2));
        moved++;
    }
    return moved;
}

/* Persist a TAKEN item: flip the on-map record at (x,y,obj_n) to IN-INVENTORY (status location
 * bits 0x18 -> 0x10) owned by actor `owner` (record x). U6/Nuvie load objects by STATUS, not by
 * physical chunk (nuvie_save.md), so an in-inventory record left in its surface super-chunk still
 * loads into the owner's inventory — no cross-chunk move / rebuild. Only matches ON-MAP records,
 * so it safely no-ops for contained/spilled items (their record isn't on-map). Returns 1 if patched. */
int sav_patch_obj_taken(uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y, int obj_n, int owner)
{
    int32_t pos = chunk_of(d, n, surf_off, level, x, y);
    if (pos < 0 || pos + 2 > n) return 0;
    int cnt = le16(d + pos); pos += 2;
    for (int k = 0; k < cnt; k++){
        if (pos + 8 > n) return 0;
        uint8_t *r = d + pos; pos += 8;
        if ((r[0] & 0x18) != 0) continue;              /* only on-map records */
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int ron = r[4] | ((r[5] & 3) << 8);
        if (rx == x && ry == y && ron == obj_n){
            r[0] = (uint8_t)((r[0] & ~0x18) | 0x10);   /* location -> in-inventory */
            r[1] = (uint8_t)(owner & 0xff);            /* x field -> owner actor */
            r[2] = (uint8_t)(r[2] & ~0x03) | (uint8_t)((owner >> 8) & 3);
            return 1;
        }
    }
    return 0;
}

/* Persist a TAKEN CONTAINED item (opened a container, took a spilled child). The child's
 * record is IN-CONTAINER (status loc 0x08) inside the container's super-chunk, and its coord
 * bits are a PARENT LINK (= the container's record index in that chunk), not a map position —
 * so position-matching (sav_patch_obj_taken) can't find it. Instead: (1) locate the container's
 * own on-map record to learn its index P in the chunk, then (2) find the in-container child whose
 * parent link == P and obj_n == item_obj_n (quality too, if >=0) and flip it to in-inventory
 * (0x08 -> 0x10) owned by `owner`. Loads by STATUS like every taken item (nuvie_save.md), so the
 * record stays put and no rebuild is needed. (Limitation: if a container holds two children with
 * the same obj_n AND quality, both taken, only the first record is flipped — a rare case; the
 * second reverts to the container on reload, the pre-fix "acceptable" behaviour.) Returns 1 if patched. */
int sav_patch_contained_taken(uint8_t *d, int32_t n, int32_t surf_off, int level,
        int cont_x, int cont_y, int cont_obj_n, int item_obj_n, int item_quality, int owner)
{
    int32_t pos = chunk_of(d, n, surf_off, level, cont_x, cont_y);
    if (pos < 0 || pos + 2 > n) return 0;
    int cnt = le16(d + pos); int32_t base = pos + 2;
    if (base + (int32_t)cnt * 8 > n) return 0;
    /* pass 1: the container's record index P (the on-map record at cont_x,cont_y with its obj_n) */
    int P = -1;
    for (int k = 0; k < cnt; k++){
        const uint8_t *r = d + base + k * 8;
        if ((r[0] & 0x18) != 0) continue;                  /* on-map only */
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int ron = r[4] | ((r[5] & 3) << 8);
        if (rx == cont_x && ry == cont_y && ron == cont_obj_n){ P = k; break; }
    }
    if (P < 0) return 0;
    /* pass 2: the in-container child linked to P (parent = ((y&0x3f)<<10)|x; objblk.md §Containment) */
    for (int k = 0; k < cnt; k++){
        uint8_t *r = d + base + k * 8;
        if ((r[0] & 0x18) != 0x08) continue;               /* in-container only */
        int rx = r[1] | ((r[2] & 3) << 8);
        int ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
        int link = ((ry & 0x3f) << 10) | rx;
        int ron = r[4] | ((r[5] & 3) << 8);
        if (link == P && ron == item_obj_n && (item_quality < 0 || r[7] == (uint8_t)item_quality)){
            r[0] = (uint8_t)((r[0] & ~0x18) | 0x10);        /* location -> in-inventory */
            r[1] = (uint8_t)(owner & 0xff);                 /* x field -> owner actor */
            r[2] = (uint8_t)(r[2] & ~0x03) | (uint8_t)((owner >> 8) & 3);
            return 1;
        }
    }
    return 0;
}

void sav_set_actor_pos(uint8_t *d, int32_t objlist_off, int actor, int x, int y, int z)
{
    uint8_t *p = d + objlist_off + 0x100 + actor * 3;
    p[0] = (uint8_t)(x & 0xff);
    p[1] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
    p[2] = (uint8_t)(((y >> 6) & 0xf) | ((z & 0xf) << 4));
}

/* Patch actor `actor`'s stat block: HP @0xe00, strength @0x900, dexterity @0xa00,
 * intelligence @0xb00 (each a 1-byte per-actor array; objlist.md §Stats). */
void sav_set_actor_stats(uint8_t *d, int32_t off, int actor, int hp, int str, int dex, int intel)
{
    d[off + 0xe00 + actor] = (uint8_t)hp;
    d[off + 0x900 + actor] = (uint8_t)str;
    d[off + 0xa00 + actor] = (uint8_t)dex;
    d[off + 0xb00 + actor] = (uint8_t)intel;
}

/* Patch actor `actor`'s experience (objlist 0xc00 + actor*2, u16 little-endian in the
 * ScummVM save — write byte-wise so it is correct on the big-endian 68000). */
void sav_set_actor_exp(uint8_t *d, int32_t off, int actor, int exp)
{
    uint8_t *p = d + off + 0xc00 + actor * 2;
    p[0] = (uint8_t)(exp & 0xff);
    p[1] = (uint8_t)((exp >> 8) & 0xff);
}

/* Patch the game clock (objlist 0x1bf3 = minute-of-hour, 0x1bf4 = hour; single bytes). */
void sav_set_clock(uint8_t *d, int32_t off, int minute, int hour)
{
    d[off + 0x1bf3] = (uint8_t)minute;
    d[off + 0x1bf4] = (uint8_t)hour;
}
/* The game date after the clock (objlist 0x1bf5 day, 0x1bf6 month, 0x1bf7-8 year LE; moongates.md §1). */
void sav_set_date(uint8_t *d, int32_t off, int day, int month, int year)
{
    d[off + 0x1bf5] = (uint8_t)day; d[off + 0x1bf6] = (uint8_t)month;
    d[off + 0x1bf7] = (uint8_t)year; d[off + 0x1bf8] = (uint8_t)(year >> 8);
}
/* the powder-keg fuse (objlist 0x1c4b, u16 LE; usecode_use.md) */
void sav_set_keg_timer(uint8_t *d, int32_t off, int t){ d[off + 0x1c4b] = (uint8_t)t; d[off + 0x1c4c] = (uint8_t)(t >> 8); }
int  sav_get_keg_timer(const uint8_t *d, int32_t off){ return d[off + 0x1c4b] | (d[off + 0x1c4c] << 8); }
void sav_set_timer(uint8_t *d, int32_t off, int idx, int val){ if (idx >= 0 && idx < 16) d[off + 0x1c03 + idx] = (uint8_t)val; }
/* the rest counter: hours until resting may heal the party again (objlist 0x1bf2; rules/rest.md §4) */
void sav_set_rest_counter(uint8_t *d, int32_t off, int val){ d[off + 0x1bf2] = (uint8_t)val; }
int  sav_get_rest_counter(const uint8_t *d, int32_t off){ return d[off + 0x1bf2]; }
void sav_set_vanish(uint8_t *d, int32_t off, int v){ d[off + 0x1c13] = (uint8_t)v; d[off + 0x1c14] = (uint8_t)(v >> 8); }
int  sav_get_vanish(const uint8_t *d, int32_t off){ return d[off + 0x1c13] | (d[off + 0x1c14] << 8); }
int  sav_get_timer(const uint8_t *d, int32_t off, int idx){ return (idx >= 0 && idx < 16) ? d[off + 0x1c03 + idx] : 0; }
void sav_get_date(const uint8_t *d, int32_t off, int *day, int *month, int *year)
{
    *day = d[off + 0x1bf5]; *month = d[off + 0x1bf6]; *year = d[off + 0x1bf7] | (d[off + 0x1bf8] << 8);
}

/* Patch the avatar's identity: gender @0x1c71 (0=male, 1=female), selected portrait
 * @0x1c72, karma @0x1bf9 (objlist.md §Player fields). */
void sav_set_player(uint8_t *d, int32_t off, int gender, int portrait, int karma)
{
    d[off + 0x1c71] = (uint8_t)gender;
    d[off + 0x1c72] = (uint8_t)portrait;
    d[off + 0x1bf9] = (uint8_t)karma;
}

void sav_set_talk_state(uint8_t *d, int32_t off, const uint8_t *talk_flags, int on_quest, int knows_gargish)
{
    for (int i = 0; i < 256; i++) d[off + 0x17f1 + i] = talk_flags[i];
    d[off + 0x1bf1] = (uint8_t)on_quest;
    d[off + 0x1c5f] = (uint8_t)knows_gargish;
}
void sav_get_talk_state(const uint8_t *d, int32_t off, uint8_t *talk_flags, int *on_quest, int *knows_gargish)
{
    for (int i = 0; i < 256; i++) talk_flags[i] = d[off + 0x17f1 + i];
    if (on_quest) *on_quest = d[off + 0x1bf1];
    if (knows_gargish) *knows_gargish = d[off + 0x1c5f];
}

void sav_set_wind(uint8_t *d, int32_t off, int wind_byte){ d[off + 0x1bfa] = (uint8_t)wind_byte; }
int  sav_get_wind(const uint8_t *d, int32_t off){ return d[off + 0x1bfa]; }

void sav_set_actor_level_mana(uint8_t *d, int32_t off, int actor, int level, int mana)
{
    d[off + 0xff1  + actor] = (uint8_t)level;
    d[off + 0x13f1 + actor] = (uint8_t)mana;
}

/* the per-actor WORKTYPE byte (objlist 0x11f1; docs/rules/usecode_vm.md §7 / schedule.md) */
void sav_set_actor_worktype(uint8_t *d, int32_t off, int actor, int wt){ d[off + 0x11f1 + actor] = (uint8_t)wt; }
int  sav_get_actor_worktype(const uint8_t *d, int32_t off, int actor){ return d[off + 0x11f1 + actor]; }

void sav_set_avatar_name(uint8_t *d, int32_t off, const char *name)
{
    int i = 0;
    for (; i < 13 && name[i]; i++) d[off + 0xf00 + i] = (uint8_t)name[i];
    for (; i < 14; i++) d[off + 0xf00 + i] = 0;
}

void sav_set_trailer_date(uint8_t *d, int32_t n, int year, int month, int day, int hour, int minute)
{
    if (n < 24) return;
    const uint8_t *e = d + n - 4;
    int32_t hp = (int32_t)(e[0] | (e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24));
    if (hp <= 0 || hp + 13 > n - 4) return;
    uint8_t *h = d + hp;
    if (h[0] != 'S' || h[1] != 'V' || h[2] != 'M' || h[3] != 'C' || h[4] != 'R' || h[5] != 0) return;
    uint32_t date = ((uint32_t)(day & 0xff) << 24) | ((uint32_t)(month & 0xff) << 16) | (uint32_t)(year & 0xffff);
    h[7] = (uint8_t)date; h[8] = (uint8_t)(date >> 8); h[9] = (uint8_t)(date >> 16); h[10] = (uint8_t)(date >> 24);
    h[11] = (uint8_t)(minute & 0xff); h[12] = (uint8_t)(hour & 0xff);
}

int sav_write_file(const char *path, const uint8_t *buf, int32_t n)
{
    plat_file f = plat_fcreate(path);
    if (f == PLAT_BADFILE) return -1;
    int32_t w = plat_fwrite(f, buf, n);
    plat_fclose(f);
    return (w == n) ? 0 : -1;
}

/* ---- streaming REBUILD writer (save.h; docs/rules/save_load.md §6) ------------------------- */

void sav_encode_record(uint8_t *r, int status, int x, int y, int z, int obj_n, int frame, int qty, int quality)
{
    r[0] = (uint8_t)status;
    r[1] = (uint8_t)(x & 0xff);
    r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
    r[3] = (uint8_t)(((y >> 6) & 0x0f) | ((z & 0x0f) << 4));
    r[4] = (uint8_t)(obj_n & 0xff);
    r[5] = (uint8_t)(((obj_n >> 8) & 3) | ((frame & 0x3f) << 2));
    r[6] = (uint8_t)qty;
    r[7] = (uint8_t)quality;
}

typedef struct { plat_file f; uint8_t buf[2048]; int n; int err; int32_t total; } wr_t;
static void wr_flush(wr_t *w)
{
    if (w->n && !w->err){ if (plat_fwrite(w->f, w->buf, w->n) != w->n) w->err = 1; }
    w->n = 0;
}
static void wr_put(wr_t *w, const uint8_t *p, int32_t len)
{
    while (len > 0 && !w->err){
        int room = (int)sizeof w->buf - w->n;
        if (room == 0){ wr_flush(w); continue; }
        int take = (len < room) ? (int)len : room;
        for (int i = 0; i < take; i++) w->buf[w->n + i] = p[i];
        w->n += take; p += take; len -= take; w->total += take;
    }
}
static void wr_u16(wr_t *w, int v){ uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; wr_put(w, b, 2); }

#define REMAP_MAX 4096
static uint16_t g_remap[REMAP_MAX];   /* chunk-local old index -> new index (0xffff = dropped) */

/* Copy one chunk at d[pos] minus the party-held records (links re-indexed), then `nextra`
 * appended records whose links are relative to the first extra. Returns the next chunk's
 * offset in d, or -1 on a bad layout / oversized chunk. */
static int32_t write_chunk_filtered(wr_t *w, const uint8_t *d, int32_t n, int32_t pos,
                                    const uint8_t *held, int nact, const uint8_t *extra, int nextra)
{
    if (pos < 0 || pos + 2 > n) return -1;
    int cnt = le16(d + pos); int32_t base = pos + 2;
    if (cnt > REMAP_MAX || base + (int32_t)cnt * 8 > n) return -1;
    int nkept = 0;
    for (int k = 0; k < cnt; k++){
        const uint8_t *r = d + base + k * 8; int direct;
        int holder = rec_holder(d, base, cnt, r, held, nact, &direct);
        g_remap[k] = (holder >= 0) ? 0xffff : (uint16_t)nkept++;
    }
    wr_u16(w, nkept + nextra);
    for (int k = 0; k < cnt; k++){
        if (g_remap[k] == 0xffff) continue;
        const uint8_t *r = d + base + k * 8; uint8_t out[8];
        for (int i = 0; i < 8; i++) out[i] = r[i];
        if ((r[0] & 0x18) == 0x08){                          /* re-index the container link */
            int rx = r[1] | ((r[2] & 3) << 8), ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
            int link = ((ry & 0x3f) << 10) | rx;
            int p = (link < cnt && g_remap[link] != 0xffff) ? g_remap[link] : 0;
            out[1] = (uint8_t)(p & 0xff);
            out[2] = (uint8_t)(((p >> 8) & 3) | (((p >> 10) & 0x3f) << 2));
            out[3] = (uint8_t)(out[3] & 0xf0);
        }
        wr_put(w, out, 8);
    }
    for (int k = 0; k < nextra; k++){
        const uint8_t *r = extra + k * 8; uint8_t out[8];
        for (int i = 0; i < 8; i++) out[i] = r[i];
        if ((r[0] & 0x18) == 0x08){
            int rx = r[1] | ((r[2] & 3) << 8), ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
            int p = (((ry & 0x3f) << 10) | rx) + nkept;
            out[1] = (uint8_t)(p & 0xff);
            out[2] = (uint8_t)(((p >> 8) & 3) | (((p >> 10) & 0x3f) << 2));
            out[3] = (uint8_t)(out[3] & 0xf0);
        }
        wr_put(w, out, 8);
    }
    return base + (int32_t)cnt * 8;
}

int sav_write_rebuilt(const char *path, const uint8_t *d, int32_t n, const uint8_t *party, int nparty,
                      const uint8_t *held, int nact, int32_t *out_len)
{
    int32_t ol = sav_objlist_off(d, n); if (ol < 0 || n < 8) return -1;
    wr_t w; w.f = plat_fcreate(path); w.n = 0; w.err = 0; w.total = 0;
    if (w.f == PLAT_BADFILE) return -1;
    wr_put(&w, d, 4);                                                   /* version + game tag */
    int32_t pos = 4;
    pos = write_chunk_filtered(&w, d, n, pos, held, nact, party, nparty);   /* actor inventories + party */
    for (int c = 0; c < 1 + 64 + 5 && pos > 0; c++)                     /* eggs, surface, dungeons */
        pos = write_chunk_filtered(&w, d, n, pos, held, nact, 0, 0);
    if (pos < 0 || pos + 4 > n){ wr_flush(&w); plat_fclose(w.f); return -2; }
    int32_t osz = (int32_t)(d[pos] | (d[pos+1] << 8) | ((uint32_t)d[pos+2] << 16) | ((uint32_t)d[pos+3] << 24));
    int32_t tail = pos + 4 + osz;                                       /* start of any trailer */
    if (tail > n){ wr_flush(&w); plat_fclose(w.f); return -2; }
    int32_t delta = w.total - pos;                                      /* how much the object region grew */
    wr_put(&w, d + pos, 4 + osz);                                       /* objlist size + objlist */
    if (tail + 4 <= n){                                                 /* ScummVM trailer: fix its header offset */
        const uint8_t *e = d + n - 4;
        int32_t hp = (int32_t)(e[0] | (e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24));
        int valid = (hp >= tail && hp + 6 <= n - 4 && d[hp] == 'S' && d[hp+1] == 'V' && d[hp+2] == 'M' && d[hp+3] == 'C' && d[hp+4] == 'R');
        wr_put(&w, d + tail, n - tail - 4);
        int32_t np = valid ? hp + delta : hp;
        uint8_t b[4] = { (uint8_t)np, (uint8_t)(np >> 8), (uint8_t)(np >> 16), (uint8_t)(np >> 24) };
        wr_put(&w, b, 4);
    } else wr_put(&w, d + tail, n - tail);
    wr_flush(&w);
    plat_fclose(w.f);
    if (out_len) *out_len = w.total;
    return w.err ? -3 : 0;
}

/* ---- M)ove into a container: record relocation (save.h; usecode_use.md "M)ove") ------------- */

static int rec_link(const uint8_t *r)
{   int rx = r[1] | ((r[2] & 3) << 8), ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6);
    return ((ry & 0x3f) << 10) | rx; }
static void rec_set_link(uint8_t *r, int link)
{   int x = link & 0x3ff, y6 = (link >> 10) & 0x3f;
    r[0] = (uint8_t)((r[0] & ~0x18) | 0x08);
    r[1] = (uint8_t)(x & 0xff);
    r[2] = (uint8_t)(((x >> 8) & 3) | (y6 << 2));
    r[3] = (uint8_t)(r[3] & 0xf0); }

int32_t sav_locate_onmap(const uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y, int obj_n, int *k)
{
    int32_t cpos = chunk_of(d, n, surf_off, level, x, y);
    int kk = find_onmap_k(d, n, cpos, x, y, obj_n);
    if (kk >= 0){ *k = kk; return cpos; }
    int32_t pos = 4;                                        /* every chunk: inventories, eggs, 64 surface, 5 dungeon */
    for (int c = 0; c < 71; c++){
        if (pos + 2 > n) return -1;
        int cnt = le16(d + pos); int32_t base = pos + 2;
        if (base + (int32_t)cnt * 8 > n) return -1;
        for (int j = 0; j < cnt; j++){
            const uint8_t *r = d + base + j * 8;
            if ((r[0] & 0x18) != 0) continue;
            int rx = r[1] | ((r[2] & 3) << 8), ry = (r[2] >> 2) | ((r[3] & 0x0f) << 6), rz = r[3] >> 4;
            int ron = r[4] | ((r[5] & 3) << 8);
            if (rx == x && ry == y && rz == level && ron == obj_n){ *k = j; return pos; }
        }
        pos = base + (int32_t)cnt * 8;
    }
    return -1;
}

int sav_find_child(const uint8_t *d, int32_t n, int32_t cpos, int parent_k, int obj_n, int quality)
{
    if (cpos < 0 || cpos + 2 > n) return -1;
    int cnt = le16(d + cpos); int32_t base = cpos + 2;
    if (base + (int32_t)cnt * 8 > n) return -1;
    for (int k = 0; k < cnt; k++){
        const uint8_t *r = d + base + k * 8;
        if ((r[0] & 0x18) != 0x08) continue;
        if (rec_link(r) != parent_k) continue;
        int ron = r[4] | ((r[5] & 3) << 8);
        if (ron == obj_n && (quality < 0 || r[7] == (uint8_t)quality)) return k;
    }
    return -1;
}

int32_t sav_delete_at(uint8_t *d, int32_t n, int32_t cpos, int k){ return delete_record_k(d, n, cpos, k); }

void sav_set_rec_qty(uint8_t *d, int32_t n, int32_t cpos, int k, int qty)
{
    if (cpos < 0 || cpos + 2 > n) return;
    int cnt = le16(d + cpos); int32_t at = cpos + 2 + (int32_t)k * 8;
    if (k < 0 || k >= cnt || at + 8 > n) return;
    d[at + 6] = (uint8_t)(qty & 0xff); d[at + 7] = (uint8_t)((qty >> 8) & 0xff);
}

/* grow the buffer by `delta` bytes at `at` (the tail shifts up); a ScummVM trailer's header offset
 * (the file's last u32) follows the tail. Returns the new length, or -1 if it does not fit. */
static int32_t grow_at(uint8_t *d, int32_t n, int32_t cap, int32_t at, int32_t delta)
{
    if (n + delta > cap || at < 0 || at > n) return -1;
    int32_t ol = sav_objlist_off(d, n); if (ol < 4) return -1;
    int32_t osz = (int32_t)(d[ol-4] | (d[ol-3] << 8) | ((uint32_t)d[ol-2] << 16) | ((uint32_t)d[ol-1] << 24));
    int32_t tail = ol + osz; if (tail > n) return -1;
    int32_t hp = -1;
    if (tail + 4 <= n){
        const uint8_t *e = d + n - 4;
        hp = (int32_t)(e[0] | (e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24));
        if (!(hp >= tail && hp + 6 <= n - 4 && d[hp] == 'S' && d[hp+1] == 'V' && d[hp+2] == 'M' && d[hp+3] == 'C' && d[hp+4] == 'R')) hp = -1;
    }
    memmove(d + at + delta, d + at, (size_t)(n - at));
    n += delta;
    if (hp >= 0){ int32_t np = hp + delta; uint8_t *e = d + n - 4;
        e[0] = (uint8_t)np; e[1] = (uint8_t)(np >> 8); e[2] = (uint8_t)(np >> 16); e[3] = (uint8_t)(np >> 24); }
    return n;
}

#define RC_MAX 64
int32_t sav_recontain(uint8_t *d, int32_t n, int32_t cap, int32_t src_cpos, int src_k, int32_t dst_cpos, int dst_k)
{
    static uint8_t  blk[RC_MAX * 8];      /* the travelling records, links made block-relative */
    static uint16_t idx[RC_MAX];          /* their indices in the source chunk, root first, ascending */
    static uint8_t  inset[1024];          /* bitmap over source-chunk indices */
    if (src_cpos < 0 || dst_cpos < 0 || src_cpos + 2 > n || dst_cpos + 2 > n) return -1;
    int scnt = le16(d + src_cpos); int32_t sbase = src_cpos + 2;
    if (src_k < 0 || src_k >= scnt || scnt > 8192 || sbase + (int32_t)scnt * 8 > n) return -1;
    int dcnt = le16(d + dst_cpos);
    if (dst_k < 0 || dst_k >= dcnt) return -1;
    memset(inset, 0, sizeof inset);
    int m = 0; idx[m++] = (uint16_t)src_k; inset[src_k >> 3] |= (uint8_t)(1u << (src_k & 7));
    for (int j = src_k + 1; j < scnt; j++){                 /* contents follow their holder (objblk.md §2) */
        const uint8_t *r = d + sbase + j * 8;
        if ((r[0] & 0x18) != 0x08) continue;
        int link = rec_link(r);
        if (link < j && ((inset[link >> 3] >> (link & 7)) & 1)){
            if (m >= RC_MAX) return -1;
            idx[m++] = (uint16_t)j; inset[j >> 3] |= (uint8_t)(1u << (j & 7));
        }
    }
    if (dst_cpos == src_cpos && ((inset[dst_k >> 3] >> (dst_k & 7)) & 1)) return -1;   /* a holder cannot go into itself */
    if (n + (int32_t)m * 8 > cap) return -1;
    for (int a = 0; a < m; a++){
        uint8_t *b = blk + a * 8; memcpy(b, d + sbase + idx[a] * 8, 8);
        if (a == 0){ rec_set_link(b, 0); continue; }      /* the root: its holder is patched in below */
        int link = rec_link(b), rel = 0;
        for (int q = 0; q < a; q++) if (idx[q] == link){ rel = q; break; }
        rec_set_link(b, rel);
    }
    /* out of the source chunk, deepest (highest index) first: each deletion re-indexes later links */
    for (int a = m - 1; a >= 0; a--){
        int32_t nn = delete_record_k(d, n, src_cpos, idx[a]);
        if (nn < 0) return -1;                             /* (cannot happen once the block is complete) */
        n = nn;
        if (dst_cpos == src_cpos && idx[a] < dst_k) dst_k--;
        else if (dst_cpos > src_cpos) dst_cpos -= 8;
    }
    /* onto the end of the destination chunk */
    dcnt = le16(d + dst_cpos);
    int32_t dend = dst_cpos + 2 + (int32_t)dcnt * 8;
    int32_t nn = grow_at(d, n, cap, dend, (int32_t)m * 8);
    if (nn < 0) return -1;                                 /* (room was checked above) */
    n = nn;
    for (int a = 0; a < m; a++){
        uint8_t *r = d + dend + a * 8; memcpy(r, blk + a * 8, 8);
        rec_set_link(r, a == 0 ? dst_k : dcnt + rec_link(blk + a * 8));
    }
    d[dst_cpos] = (uint8_t)((dcnt + m) & 0xff); d[dst_cpos + 1] = (uint8_t)(((dcnt + m) >> 8) & 0xff);
    return n;
}

/* ---- New records + the dead flag + alcohol (save_load.md §5 step 0b; usecode_use.md "Created and
 * destroyed objects persist", 2026-10-28) ---------------------------------------------------------- */
int32_t sav_insert_onmap(uint8_t *d, int32_t n, int32_t cap, int32_t surf_off, int level, int x, int y,
                         int obj_n, int frame, int qty, int quality, int status)
{
    int32_t cpos = chunk_of(d, n, surf_off, level, x, y);
    if (cpos < 0 || cpos + 2 > n) return -1;
    int cnt = le16(d + cpos); if (cnt >= 8191) return -1;
    int32_t at = cpos + 2 + (int32_t)cnt * 8; if (at > n) return -1;
    int32_t nn = grow_at(d, n, cap, at, 8); if (nn < 0) return -1;
    sav_encode_record(d + at, status & ~0x18, x, y, level, obj_n, frame, qty, quality);
    d[cpos] = (uint8_t)((cnt + 1) & 0xff); d[cpos+1] = (uint8_t)(((cnt + 1) >> 8) & 0xff);
    return nn;
}
int32_t sav_insert_child(uint8_t *d, int32_t n, int32_t cap, int32_t cpos, int parent_k, int obj_n, int frame, int qty, int quality)
{
    if (cpos < 0 || cpos + 2 > n) return -1;
    int cnt = le16(d + cpos); if (parent_k < 0 || parent_k >= cnt || cnt >= 8191) return -1;
    int32_t at = cpos + 2 + (int32_t)cnt * 8; if (at > n) return -1;
    int32_t nn = grow_at(d, n, cap, at, 8); if (nn < 0) return -1;
    sav_encode_record(d + at, 0x09, 0, 0, 0, obj_n, frame, qty, quality);
    rec_set_link(d + at, parent_k);
    d[cpos] = (uint8_t)((cnt + 1) & 0xff); d[cpos+1] = (uint8_t)(((cnt + 1) >> 8) & 0xff);
    return nn;
}
int sav_inv_owner_first(const uint8_t *d, int32_t n, int actor)
{
    int32_t iv = 4; if (iv + 2 > n) return -1;
    int cnt = le16(d + iv); const uint8_t *rec = d + iv + 2; if (iv + 2 + (int32_t)cnt * 8 > n) return -1;
    for (int k = 0; k < cnt; k++){ const uint8_t *b = rec + k * 8; int loc = b[0] & 0x18;
        if (loc != 0x10 && loc != 0x18) continue;
        if ((b[1] | ((b[2] & 3) << 8)) == actor) return k; }
    return -1;
}
int sav_drop_actor_inv(uint8_t *d, int32_t n, int actor, int x, int y, int z)
{
    int32_t iv = 4; if (iv + 2 > n) return 0;
    int cnt = le16(d + iv); uint8_t *rec = d + iv + 2; if (iv + 2 + (int32_t)cnt * 8 > n) return 0;
    int moved = 0;
    for (int k = 0; k < cnt; k++){ uint8_t *r = rec + k * 8; int loc = r[0] & 0x18;
        if (loc != 0x10 && loc != 0x18) continue;
        if ((r[1] | ((r[2] & 3) << 8)) != actor) continue;
        r[0] = (uint8_t)((r[0] & ~0x18) | 0x01);
        r[1] = (uint8_t)(x & 0xff);
        r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
        r[3] = (uint8_t)(((y >> 6) & 0x0f) | ((z & 0x0f) << 4));
        moved++; }
    return moved;
}
void sav_set_actor_dead(uint8_t *d, int32_t off, int actor, int dead)
{   if (dead) d[off + 0x800 + actor] |= 0x10; else d[off + 0x800 + actor] &= (uint8_t)~0x10; }
int  sav_get_actor_dead(const uint8_t *d, int32_t off, int actor){ return (d[off + 0x800 + actor] & 0x10) != 0; }
void sav_set_alcohol(uint8_t *d, int32_t off, int v){ d[off + 0x1c17] = (uint8_t)v; }
int  sav_get_alcohol(const uint8_t *d, int32_t off){ return d[off + 0x1c17]; }
