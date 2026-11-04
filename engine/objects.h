// SPDX-License-Identifier: GPL-2.0-or-later
/* objects.h — world object store + player inventory (M6, portable engine).
 *
 * Shared, headless game logic for the look/get/use verbs: a mutable array of
 * placed surface objects (the "alive" flag is cleared when one is picked up),
 * a gettable query (from the precomputed TILEFLAG-derived table), and a small
 * inventory that stacks by object number. No TOS, no render — the UI (render/
 * gamedemo) owns names + messages and calls into here.
 *
 * Implemented from docs/formats/tileflag.md (gettable rule) + objblk.md.
 */
#ifndef U6E_OBJECTS_H
#define U6E_OBJECTS_H

#include <stdint.h>

#define OBJ_INV_MAX 64        /* distinct inventory stacks the avatar can hold */

#define WOBJ_PARENT_NONE 0xFFFF   /* parent: not inside a container */

typedef struct {
    uint16_t x, y, tile, obj_n;
    uint16_t qty;                 /* stack count (qty16 for stackables; >=1)      */
    uint16_t parent;              /* world index of the containing object, or WOBJ_PARENT_NONE */
    uint16_t origin;              /* container this was SPILLED from (world index), or WOBJ_PARENT_NONE.
                                   * parent is cleared on spill so the item behaves as on-map; origin
                                   * survives get() so the save can locate its in-container record. */
    uint8_t  quality;             /* per-object datum: KEY ID on locks/keys (objblk.md §2) */
    uint8_t  alive;               /* 1 = present ON the map (contained objects load 0) */
    uint8_t  dirty;               /* WOBJ_DIRTY_*: changed since load -> re-serialize on save (nuvie_save.md) */
    uint8_t  owned;               /* 1 = status bit 0 clear: taking it from the surface is theft (usecode_use.md) */
} wobj_t;
#define WOBJ_DIRTY_STATE   1      /* frame/quality/position changed (its save record exists: patch it) */
#define WOBJ_DIRTY_DROPPED 2      /* placed on the map from the pack: its record is a CARRIED one (relocate it) */
#define WOBJ_DIRTY_DESTROYED 3   /* gone for good: its record is deleted at reconcile (cannons.md §4) */
#define WOBJ_DIRTY_NEW     4      /* created during play and KEPT: no record yet, one is appended at reconcile (save_load.md §5 step 0b) */
typedef struct { uint16_t obj_n, tile, qty; uint8_t quality; } invitem_t;

typedef struct {
    wobj_t        *objs;      /* borrowed, mutable (alive toggled on get)      */
    int            nobjs;
    const uint8_t *gettable;  /* [1024] by obj_n, 1 = pick-up-able             */
    invitem_t      inv[OBJ_INV_MAX];
    int            ninv;
} objworld_t;

void objworld_init(objworld_t *o, wobj_t *objs, int nobjs, const uint8_t *gettable);

/* Index of the topmost ALIVE object exactly at (x,y) (later in the array wins,
 * matching the back-to-front draw order), or -1 if none. */
int  objworld_at(const objworld_t *o, int x, int y);

/* 1 if obj_n is pick-up-able per the precomputed gettable table. */
int  objworld_can_get(const objworld_t *o, uint16_t obj_n);

/* Pick up object index idx: clear it from the world and add it to the inventory
 * (stacks merge by obj_n + quality). Returns 1 on success, 0 if not gettable / inv full. */
int  objworld_get(objworld_t *o, int idx);

/* 1 if the inventory holds an obj_n with the given quality (e.g. the KEY, obj 64,
 * whose quality must match a lock's key id — usecode_use.md). */
int  objworld_has(const objworld_t *o, uint16_t obj_n, uint8_t quality);

/* Total quantity of obj_n held (summed across quality/stacks). */
int  objworld_count(const objworld_t *o, uint16_t obj_n);

/* Remove n of obj_n from the inventory (across stacks). Returns the number
 * actually removed (< n if the caller held fewer). Empty stacks are compacted
 * out. Used by spellcasting to consume reagents (magic_skills.md §6). */
int  objworld_consume(objworld_t *o, uint16_t obj_n, int n);

#endif
