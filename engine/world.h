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
#include <stdbool.h>
#include "u6map.h"
#include "ruleset.h"
#include "rng.h"

typedef struct world {
    u6map_t        map;            /* decoded surface + chunks            */
    const uint8_t *passable;       /* [256]: 1 = walkable tile, 0 = blocked */
    const uint8_t *blocked_obj;    /* optional 1024x1024 bitmap: bit (y*1024+x) set =
                                    * impassable OBJECT on that tile. NULL = none. */
    const RuleSet *rules;          /* never NULL after init (defaults to U6) */
    int32_t        px, py;         /* player tile position (wrapped)      */
    uint32_t       turns;          /* completed turns                     */
    uint32_t       clock;          /* game-time ticks (sum of turn costs) */
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
void    world_set_blocked_obj(world_t *w, const uint8_t *bitmap);

/* Try to step the player by (dx,dy) (each -1/0/1). Moves + advances the turn
 * clock only if the destination is passable. Returns true if the player moved. */
bool    world_try_move(world_t *w, int dx, int dy);

#endif
