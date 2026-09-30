// SPDX-License-Identifier: GPL-2.0-or-later
/* save.c — on-target ScummVM/Nuvie save writer (see save.h + docs/formats/
 * nuvie_save.md). Patches a template save with live engine state and writes it.
 * All multi-byte save fields are little-endian; we read/write them byte-wise so
 * the code is correct on the big-endian 68000 target with no swapping. */
#include "save.h"
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
                        int x, int y, int frame, int quality)
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
            r[1] = (uint8_t)(x & 0xff);
            r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
            r[3] = (uint8_t)((r[3] & 0xf0) | ((y >> 6) & 0x0f));
            r[5] = (uint8_t)((ron >> 8) & 3) | (uint8_t)((frame & 0x3f) << 2);  /* keep obj_n high, set frame */
            if (quality >= 0) r[7] = (uint8_t)quality;    /* <0 = keep (a stackable's byte 7 is its count's high byte) */
            return 1;
        }
    }
    return 0;
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
                  const uint8_t *in_party, int nact)
{
    rec_it it; uint8_t *r; int moved = 0;
    it_start(&it, d, n);
    while ((r = it_next(&it))){
        if (rec_obj_n(r) != obj_n || r[7] != (uint8_t)quality) continue;
        int loc = r[0] & 0x18;
        if (loc != 0x10 && loc != 0x08) continue;      /* readied records stay with their wearer */
        int direct; if (rec_holder(d, it.base, it.cnt, r, in_party, nact, &direct) < 0) continue;
        r[0] = (uint8_t)(r[0] & ~0x18);                /* location -> on the map */
        r[1] = (uint8_t)(x & 0xff);
        r[2] = (uint8_t)(((x >> 8) & 3) | ((y & 0x3f) << 2));
        r[3] = (uint8_t)(((y >> 6) & 0x0f) | ((z & 0x0f) << 4));
        r[5] = (uint8_t)(((obj_n >> 8) & 3) | ((frame & 0x3f) << 2));
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

/* Patch the avatar's identity: gender @0x1c71 (0=male, 1=female), selected portrait
 * @0x1c72, karma @0x1bf9 (objlist.md §Player fields). */
void sav_set_player(uint8_t *d, int32_t off, int gender, int portrait, int karma)
{
    d[off + 0x1c71] = (uint8_t)gender;
    d[off + 0x1c72] = (uint8_t)portrait;
    d[off + 0x1bf9] = (uint8_t)karma;
}

void sav_set_actor_level_mana(uint8_t *d, int32_t off, int actor, int level, int mana)
{
    d[off + 0xff1  + actor] = (uint8_t)level;
    d[off + 0x13f1 + actor] = (uint8_t)mana;
}

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
