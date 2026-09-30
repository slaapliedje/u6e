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

/* Patch the STATE of the on-map object whose record sits at its ORIGINAL position (ox,oy) with
 * the given obj_n, in that position's row-major super-chunk. The object database is a flat fixed
 * record set — a door toggle, a sailed boat or a bought boat changes only the record's frame /
 * position / quality in place (no add/remove), so counts and all following offsets are preserved
 * (nuvie_save.md). Loaders place a record by its own x/y, not by the chunk it sits in, so a boat
 * that sailed into another super-chunk is still found by everyone. The frame is the top 6 bits of
 * byte 5; z (byte 3 high nibble) is kept. Returns 1 if a matching record was found + patched. */
int sav_patch_obj_state(uint8_t *d, int32_t n, int32_t surf_off, int ox, int oy, int obj_n,
                        int x, int y, int frame, int quality)
{
    int chunk = ((oy >> 7) & 7) * 8 + ((ox >> 7) & 7);
    int32_t pos = surf_off;
    for (int i = 0; i < chunk; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return 0; }
    if (pos + 2 > n) return 0;
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

int sav_drop_item(uint8_t *d, int32_t n, int obj_n, int quality, int x, int y, int frame,
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
        r[3] = (uint8_t)((y >> 6) & 0x0f);             /* z = 0: surface */
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
int sav_patch_obj_taken(uint8_t *d, int32_t n, int32_t surf_off, int x, int y, int obj_n, int owner)
{
    int chunk = ((y >> 7) & 7) * 8 + ((x >> 7) & 7);
    int32_t pos = surf_off;
    for (int i = 0; i < chunk; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return 0; }
    if (pos + 2 > n) return 0;
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
int sav_patch_contained_taken(uint8_t *d, int32_t n, int32_t surf_off,
        int cont_x, int cont_y, int cont_obj_n, int item_obj_n, int item_quality, int owner)
{
    int chunk = ((cont_y >> 7) & 7) * 8 + ((cont_x >> 7) & 7);
    int32_t pos = surf_off;
    for (int i = 0; i < chunk; i++){ pos = skip_chunk(d, n, pos); if (pos < 0) return 0; }
    if (pos + 2 > n) return 0;
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
