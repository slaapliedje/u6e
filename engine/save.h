// SPDX-License-Identifier: GPL-2.0-or-later
/* save.h — write a ScummVM/Nuvie-compatible save from live engine state.
 *
 * We adopt ScummVM's Nuvie save format as our own (see docs/formats/nuvie_save.md).
 * The engine can't yet model every region (inventories, eggs, dungeon levels,
 * most objlist arrays), so the on-target writer uses the SAME strategy as the host
 * tool /tools/nuviewrite: start from a TEMPLATE save (the user's own .000, loaded
 * at runtime), patch in place the fields we DO model, and write it back.
 *
 * Big-endian-SAFE: every multi-byte save field is little-endian and is read/written
 * byte-wise here, so this is correct on the 68000 target and on the host alike.
 */
#ifndef U6E_SAVE_H
#define U6E_SAVE_H

#include <stdint.h>

/* Locate the objlist within a template save buffer by walking the chunk list.
 * Returns the objlist byte offset, or -1 if the layout doesn't parse. */
int32_t sav_objlist_off(const uint8_t *d, int32_t n);

/* Patch actor `actor`'s position (objlist 0x100 + actor*3), little-endian. */
void sav_set_actor_pos(uint8_t *d, int32_t objlist_off, int actor, int x, int y, int z);

/* Offset of the 64 surface super-chunks within a template save (or -1). */
int32_t sav_surface_off(const uint8_t *d, int32_t n);

/* READ side (in-game load): the actor-inventory super-chunk offset, and the end of the
 * 64 surface super-chunks (= where the 5 dungeon chunks begin). */
int32_t sav_inv_off(const uint8_t *d, int32_t n);
int32_t sav_inv_end(const uint8_t *d, int32_t n);      /* end of the inventory chunk (= the eggs chunk) */
int32_t sav_surface_end(const uint8_t *d, int32_t n);
/* The object region of one map LEVEL: 0 = the 64 surface chunks, 1..5 = that dungeon chunk.
 * Returns 1 and fills [*off, *end), or 0 if the layout doesn't parse. */
int sav_level_span(const uint8_t *d, int32_t n, int level, int32_t *off, int32_t *end);

/* Every on-map patcher below addresses a record by its LEVEL: a surface record (level 0) lives in
 * super-chunk (y>>7)*8+(x>>7), a dungeon record (level 1..5) in dungeon chunk level-1. */

/* Patch the on-map object whose record sits at its ORIGINAL position (ox,oy) with `obj_n`
 * (in-place; nuvie_save.md): new position x,y (a sailed boat), frame (doors, boat facing) and
 * quality (a bought boat's cleared price tag; <0 keeps the record's). Returns 1 if patched. */
int sav_patch_obj_state(uint8_t *d, int32_t n, int32_t surf_off, int level, int ox, int oy, int obj_n,
                        int x, int y, int frame, int quality);

/* ---- carried-record RECONCILIATION (docs/rules/save_load.md §5) ----------------------------
 * These walk EVERY record of the object region [4, surface_end): the actor-inventory chunk, the
 * eggs chunk and the 64 surface chunks (a GET-flipped record stays in its surface chunk).
 * Records are patched in place; the chunk framing never changes (nothing is added or removed).
 * `in_party[a]` (nact entries) is 1 for the actors whose inventories the engine pools. */

/* Readied reconcile, pass 0: every READIED record (loc 0x18) owned by a party actor becomes
 * plain carried (0x10). Returns the number changed. */
int sav_unready_party(uint8_t *d, int32_t n, const uint8_t *in_party, int nact);

/* Readied reconcile, pass 1: mark ONE record of `obj_n` as readied by `actor`. Candidates: a
 * carried record owned by any party actor (this actor's own preferred, single items preferred)
 * or a contained record whose container chain roots at a party actor (a bag in the pack).
 * Returns 1 if a record was found. */
int sav_ready_item(uint8_t *d, int32_t n, int actor, int obj_n, const uint8_t *in_party, int nact);

/* D)rop persistence: relocate EVERY party-carried record of (obj_n, quality) onto the map at
 * (x,y,z) with `frame`. Each record keeps its own stack count — the pooled stack the player
 * dropped IS their union. Returns the number relocated. */
int sav_drop_item(uint8_t *d, int32_t n, int obj_n, int quality, int x, int y, int z, int frame,
                  const uint8_t *in_party, int nact);

/* Persist a TAKEN item: flip the on-map record at (x,y,obj_n) to in-inventory, owner `owner`.
 * Returns 1 if patched (no-ops for non-on-map records). */
int sav_patch_obj_taken(uint8_t *d, int32_t n, int32_t surf_off, int level, int x, int y, int obj_n, int owner);

/* Persist a TAKEN CONTAINED item: locate the container's record (cont_x,cont_y,cont_obj_n) to get
 * its index in the super-chunk, then flip the in-container child (parent link -> that index) with
 * obj_n=item_obj_n (and quality, if >=0) to in-inventory owned by `owner`. Returns 1 if patched. */
int sav_patch_contained_taken(uint8_t *d, int32_t n, int32_t surf_off, int level,
        int cont_x, int cont_y, int cont_obj_n, int item_obj_n, int item_quality, int owner);

/* Patch actor `actor`'s stats (HP@0xe00, str@0x900, dex@0xa00, int@0xb00). */
void sav_set_actor_stats(uint8_t *d, int32_t off, int actor, int hp, int str, int dex, int intel);

/* Patch actor `actor`'s experience (objlist 0xc00 + actor*2, little-endian). */
void sav_set_actor_exp(uint8_t *d, int32_t off, int actor, int exp);

/* Patch the game clock (objlist 0x1bf3 = minute-of-hour, 0x1bf4 = hour). */
void sav_set_clock(uint8_t *d, int32_t off, int minute, int hour);

/* Patch the avatar identity (0x1c71 gender, 0x1c72 portrait, 0x1bf9 karma). */
void sav_set_player(uint8_t *d, int32_t off, int gender, int portrait, int karma);

/* Patch actor `actor`'s level (objlist 0xff1 + actor) and current magic points (0x13f1 + actor). */
void sav_set_actor_level_mana(uint8_t *d, int32_t off, int actor, int level, int mana);

/* Patch the player's name (objlist 0xf00: up to 13 characters + NUL, Nuvie's new-game field). */
void sav_set_avatar_name(uint8_t *d, int32_t off, const char *name);

/* Date-stamp the ScummVM extended-save trailer (if the buffer carries one: "SVMCR" at the
 * offset stored in the last u32; nuvie_save.md) so ScummVM's load menu shows when we saved. */
void sav_set_trailer_date(uint8_t *d, int32_t n, int year, int month, int day, int hour, int minute);

/* Write a full save buffer to `path` via the platform file layer.
 * Returns 0 on success, negative on error. */
int sav_write_file(const char *path, const uint8_t *buf, int32_t n);

/* ---- streaming REBUILD writer (docs/rules/save_load.md §6) ----------------------------------
 * Writes the save to `path` from the template buffer `d`, REPLACING the party's inventory: every
 * record held by an actor with held[a]=1 (carried/readied by it, or contained in something it
 * carries) is left out of every chunk, and `nparty` fresh records (`party`, 8 bytes each — the
 * live readied gear, pooled pack, gold, spellbook) are appended to the actor-inventory chunk.
 * Chunk counts and chunk-local container links are re-indexed; the eggs, dungeon and objlist
 * regions are copied; a ScummVM trailer's header offset is fixed up. In-container links inside
 * `party` are indices relative to its first record. Nothing is allocated: records stream through
 * a small buffer. Returns 0 on success (and the written length), negative on error. */
int sav_write_rebuilt(const char *path, const uint8_t *d, int32_t n, const uint8_t *party, int nparty,
                      const uint8_t *held, int nact, int32_t *out_len);

/* Encode one 8-byte object record (nuvie_save.md): a carried/readied record has owner in x. */
void sav_encode_record(uint8_t *r, int status, int x, int y, int z, int obj_n, int frame, int qty, int quality);

#endif /* U6E_SAVE_H */
