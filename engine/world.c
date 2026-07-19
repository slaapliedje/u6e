// SPDX-License-Identifier: GPL-2.0-or-later
/* world.c — world model + movement + turn clock. See world.h. */
#include "world.h"
#include <stddef.h>   /* NULL */

/* U6 default rules ------------------------------------------------------- */
static int u6_move_turn_cost(const world_t *w, int dx, int dy)
{
    (void)w; (void)dx; (void)dy;
    return 10;   /* U6 advances time ~10 ticks per step */
}
const RuleSet ruleset_u6 = { u6_move_turn_cost };

/* Game registry ---------------------------------------------------------- *
 * The engine is one binary; the GameDef is selected at load time by the asset-pack magic.
 * SE/MD append their GameDefs here as they land (M9, docs/design/multigame.md). */
const GameDef gamedef_u6 = {
    "u6", "Ultima VI",
    { 'U','6','W','T' },              /* pack_magic                                   */
    U6_SURFACE_TILES,                 /* surface_tiles = 1024                         */
    U6_DUNGEON_LEVELS,                /* dungeon_levels = 5                           */
    0, 0,                             /* start_x/y: the pack header overrides these   */
    &ruleset_u6,
};

static const GameDef *const g_gamedefs[] = { &gamedef_u6 };

const GameDef *gamedef_for_magic(const char *magic)
{
    if (!magic) return NULL;
    for (unsigned i = 0; i < sizeof g_gamedefs / sizeof g_gamedefs[0]; i++) {
        const GameDef *g = g_gamedefs[i];
        if (g->pack_magic[0] == magic[0] && g->pack_magic[1] == magic[1] &&
            g->pack_magic[2] == magic[2] && g->pack_magic[3] == magic[3])
            return g;
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
bool world_init(world_t *w, const uint8_t *map, int32_t map_len,
                const uint8_t *chunks, int32_t chunks_len,
                const uint8_t *passable, const GameDef *def, uint32_t seed)
{
    if (!w || !passable || !def) return false;
    if (!u6map_load(&w->map, map, map_len, chunks, chunks_len)) return false;
    w->passable    = passable;
    w->blocked_obj = NULL;
    w->rules    = (def->rules) ? def->rules : &ruleset_u6;
    w->px       = def->start_x;
    w->py       = def->start_y;
    w->turns    = 0;
    w->clock    = 0;
    rng_seed(&w->rng, seed);
    return true;
}

uint8_t world_tile(const world_t *w, int32_t x, int32_t y)
{
    return u6map_surface_tile(&w->map, x, y);
}

bool world_is_passable(const world_t *w, int32_t x, int32_t y)
{
    if (!w->passable[world_tile(w, x, y)]) return false;          /* terrain blocks */
    if (w->blocked_obj) {                                          /* impassable object? */
        int32_t bx = x & (U6_SURFACE_TILES - 1), by = y & (U6_SURFACE_TILES - 1);
        int32_t idx = by * U6_SURFACE_TILES + bx;
        if (w->blocked_obj[idx >> 3] & (1u << (idx & 7))) return false;
    }
    return true;
}

void world_set_blocked_obj(world_t *w, const uint8_t *bitmap)
{
    if (w) w->blocked_obj = bitmap;
}

bool world_try_move(world_t *w, int dx, int dy)
{
    if (dx == 0 && dy == 0) return false;
    int32_t nx = (w->px + dx) & (U6_SURFACE_TILES - 1);   /* toroidal wrap */
    int32_t ny = (w->py + dy) & (U6_SURFACE_TILES - 1);
    if (!world_is_passable(w, nx, ny)) return false;       /* blocked: no turn */

    int cost = w->rules->move_turn_cost
             ? w->rules->move_turn_cost(w, dx, dy)
             : ruleset_u6.move_turn_cost(w, dx, dy);
    w->px = nx;
    w->py = ny;
    w->turns++;
    w->clock += (uint32_t)cost;
    return true;
}
