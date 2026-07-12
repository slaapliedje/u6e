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

/* Patch the frame of the on-map object at (x,y) with `obj_n` (in-place; nuvie_save.md).
 * Returns 1 if a matching record was found + patched. */
int sav_patch_obj_frame(uint8_t *d, int32_t n, int32_t surf_off, int x, int y, int obj_n, int frame);

/* Persist a TAKEN item: flip the on-map record at (x,y,obj_n) to in-inventory, owner `owner`.
 * Returns 1 if patched (no-ops for non-on-map records). */
int sav_patch_obj_taken(uint8_t *d, int32_t n, int32_t surf_off, int x, int y, int obj_n, int owner);

/* Persist a TAKEN CONTAINED item: locate the container's record (cont_x,cont_y,cont_obj_n) to get
 * its index in the super-chunk, then flip the in-container child (parent link -> that index) with
 * obj_n=item_obj_n (and quality, if >=0) to in-inventory owned by `owner`. Returns 1 if patched. */
int sav_patch_contained_taken(uint8_t *d, int32_t n, int32_t surf_off,
        int cont_x, int cont_y, int cont_obj_n, int item_obj_n, int item_quality, int owner);

/* Patch actor `actor`'s stats (HP@0xe00, str@0x900, dex@0xa00, int@0xb00). */
void sav_set_actor_stats(uint8_t *d, int32_t off, int actor, int hp, int str, int dex, int intel);

/* Patch actor `actor`'s experience (objlist 0xc00 + actor*2, little-endian). */
void sav_set_actor_exp(uint8_t *d, int32_t off, int actor, int exp);

/* Patch the game clock (objlist 0x1bf3 = minute-of-hour, 0x1bf4 = hour). */
void sav_set_clock(uint8_t *d, int32_t off, int minute, int hour);

/* Patch the avatar identity (0x1c71 gender, 0x1c72 portrait, 0x1bf9 karma). */
void sav_set_player(uint8_t *d, int32_t off, int gender, int portrait, int karma);

/* Write a full save buffer to `path` via the platform file layer.
 * Returns 0 on success, negative on error. */
int sav_write_file(const char *path, const uint8_t *buf, int32_t n);

#endif /* U6E_SAVE_H */
