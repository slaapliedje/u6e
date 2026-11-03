// SPDX-License-Identifier: GPL-2.0-or-later
/* world.h — shared world model + player movement + turn clock (M5).
 *
 * Portable engine logic (no TOS, no render). Operates on the decoded surface
 * (u6map) plus a passability table, so it is fully headless-testable. The
 * surface wraps toroidally (all three games rely on this).
 */
#ifndef U6E_WORLD_H
#define U6E_WORLD_H

#include <stdint.h>
#include "sparsebits.h"
#include <stdbool.h>
#include "u6map.h"
#include "ruleset.h"
#include "rng.h"

typedef struct world {
    u6map_t        map;            /* decoded surface + chunks            */
    const uint8_t *passable;       /* [256]: 1 = walkable tile, 0 = blocked */
    const sparsebits_t *blocked_obj;    /* optional 1024x1024 bitmap: bit (y*1024+x) set =
                                    * impassable OBJECT on that tile. NULL = none. */
    bool         (*force_pass)(int32_t x, int32_t y);   /* optional: an object makes impassable terrain
                                    * walkable (a drawbridge span over the moat; tileflag.md tileflags3 bit2) */
    const RuleSet *rules;          /* never NULL after init (defaults to U6) */
    int32_t        px, py;         /* player tile position (wrapped)      */
    int32_t        level;          /* 0 = surface (1024x1024); 1..5 = dungeon level (256x256). map_levels.md */
    uint32_t       turns;          /* completed turns                     */
    uint32_t       clock;          /* game minutes since the start (movement_turns.md §3)            */
    int32_t        mp_acc;         /* movement points spent toward the next game minute              */
    int32_t        min_pts;        /* points per minute = the Avatar's dexterity (0 -> 5: a minute a step) */
    rng_t          rng;            /* deterministic RNG seam              */
} world_t;

/* Initialise from decoded MAP+CHUNKS buffers + a 256-entry passability table
 * (caller owns the buffers; world borrows them). Player starts at (sx,sy). */
bool world_init(world_t *w, const uint8_t *map, int32_t map_len,
                const uint8_t *chunks, int32_t chunks_len,
                const uint8_t *passable, const GameDef *def, uint32_t seed);

uint8_t world_tile(const world_t *w, int32_t x, int32_t y);       /* surface tile, wrapped */
bool    world_is_passable(const world_t *w, int32_t x, int32_t y);/* tile at (x,y) walkable */

/* Attach an optional object-blocking bitmap (1024x1024 bits, bit (y*1024+x) set =
 * an impassable object occupies that tile). world borrows it; pass NULL to clear. */
void    world_set_blocked_obj(world_t *w, const sparsebits_t *bitmap);
/* Attach the forced-passability test (NULL = none). Checked only where the terrain blocks. */
void    world_set_force_pass(world_t *w, bool (*fn)(int32_t x, int32_t y));

/* Spend movement points: a game minute passes per min_pts points (the reference refills every
 * actor's points by its dexterity each minute; a walking step costs 5). */
void    world_spend(world_t *w, int pts);

/* Try to step the player by (dx,dy) (each -1/0/1). Moves + advances the turn
 * clock only if the destination is passable. Returns true if the player moved. */
bool    world_try_move(world_t *w, int dx, int dy);

/* Level transitions (docs/rules/map_levels.md). kind: */
enum { WL_DOWN = 0, WL_UP = 1, WL_HOLE = 2 };
/* Apply a level transition from the player's current (px,py,level), moving (px,py,level) to the
 * destination per the U6 rules: surface->L1 uses a chunk-granular /4 transform (down); L1->surface
 * inverts it via `quality` (the up-ladder's surface sub-chunk selector, bits0-1=x, bits2-3=y);
 * interior levels are vertically aligned (identity x,y, z+/-1); a hole at the deepest level goes up.
 * `quality` is used only for the L1->surface case. Returns true if the level changed. */
bool    world_transition(world_t *w, int kind, uint8_t quality);

#endif
