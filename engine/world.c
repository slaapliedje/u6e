// SPDX-License-Identifier: GPL-2.0-or-later
/* world.c — world model + movement + turn clock. See world.h. */
#include "world.h"
#include <stddef.h>   /* NULL */

/* U6 default rules ------------------------------------------------------- */
static int u6_move_turn_cost(const world_t *w, int dx, int dy)
{
    (void)w; (void)dx; (void)dy;
    return 5;    /* movement points of a walking step (movement_turns.md §3) */
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
    { 410, 64, 63, 57, 65, 88 },      /* wko: avatar, key, lockpick, spellbook, reagent_base, gold */
};

/* Savage Empire + Martian Dreams: same U6 engine + surface geometry (verified —
 * 1024x1024, 2048 tiles). They boot from full packs (magics "SEWT"/"MDWT"); their own rules
 * live in the game layer's tables (docs/design/game_layer.md). start_x/y come from the pack header. */
const GameDef gamedef_se = {
    "savage", "The Savage Empire",
    { 'S','E','W','T' },
    U6_SURFACE_TILES, U6_DUNGEON_LEVELS, 0, 0, &ruleset_u6,
    /* wko surveyed from SE's own BASETILE+LOOK.LZC (tools/objsurvey; docs/formats/se_md_objn.md):
     * avatar 310 "modern man" (confirmed vs the captured objlist), key 53 "key". SE has NO lockpick
     * (jungle setting), NO U6 magic => spellbook/reagent_base 0. gold is obj_n 69 but wko has no gold
     * field (currency identity is not an obj_n check). */
    { 310, 53, 0, 0, 0, 69 },         /* +gold 69 (survey); no lockpick/spellbook/reagents */
};
const GameDef gamedef_md = {
    "martian", "Martian Dreams",
    { 'M','D','W','T' },
    U6_SURFACE_TILES, U6_DUNGEON_LEVELS, 0, 0, &ruleset_u6,
    /* wko: MD avatar/key TBD (unconfirmed — see se_md_objn.md); MD "dollar" is obj_n 24 but left 0
     * until MD is a booted slice. All 0 => no-match. */
    { 0, 0, 0, 0, 0, 0 },
};

static const GameDef *const g_gamedefs[] = { &gamedef_u6, &gamedef_se, &gamedef_md };

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
    w->force_pass = NULL;
    w->rules    = (def->rules) ? def->rules : &ruleset_u6;
    w->px       = def->start_x;
    w->py       = def->start_y;
    w->level    = 0;                 /* start on the surface */
    w->turns    = 0;
    w->clock    = 0;
    w->mp_acc   = 0;
    w->min_pts  = 0;
    rng_seed(&w->rng, seed);
    return true;
}

uint8_t world_tile(const world_t *w, int32_t x, int32_t y)
{
    if (w->level == 0) return u6map_surface_tile(&w->map, x, y);
    return u6map_dungeon_tile(&w->map, w->level - 1, x, y);        /* dungeon level (256x256, wrapped) */
}

bool world_transition(world_t *w, int kind, uint8_t quality)
{
    int32_t z = w->level, nx = w->px, ny = w->py, nz;
    if (kind == WL_UP) {
        if (z <= 0) return false;                                 /* already on the surface */
        nz = z - 1;
        if (nz == 0) {                                            /* L1 -> surface: recover the /4-lost sub-chunk */
            nx = (w->px / 8) * 32 + ((quality & 3) * 8) + (w->px % 8);
            ny = (w->py / 8) * 32 + (((quality >> 2) & 3) * 8) + (w->py % 8);
        }                                                         /* interior up: identity x,y */
    } else {                                                      /* WL_DOWN or WL_HOLE */
        if (z == 0) {                                             /* surface -> L1: chunk-granular /4 (map_levels.md) */
            nx = (w->px & 7) | ((w->px >> 2) & 0xF8);
            ny = (w->py & 7) | ((w->py >> 2) & 0xF8);
            nz = 1;
        } else if (z >= U6_DUNGEON_LEVELS) {                      /* hole at the deepest level goes UP (U6 quirk) */
            nz = z - 1;
        } else {
            nz = z + 1;                                           /* interior down: identity x,y */
        }
    }
    w->level = nz; w->px = nx; w->py = ny;
    return true;
}

bool world_is_passable(const world_t *w, int32_t x, int32_t y)
{
    if (w->blocked_obj) {                                         /* impassable object? (the current level's bitmap) */
        if (sb_get(w->blocked_obj, (int)x, (int)y)) return false;
    }
    if (w->passable[world_tile(w, x, y)]) return true;            /* the terrain lets us through */
    return w->force_pass && w->force_pass(x & (U6_SURFACE_TILES - 1), y & (U6_SURFACE_TILES - 1));   /* a bridge over it */
}

void world_spend(world_t *w, int pts)
{
    int32_t per = w->min_pts > 0 ? w->min_pts : 5;
    if (w->min_pts < 0) return;                     /* the game runs its own clock */
    w->mp_acc += pts;
    while (w->mp_acc >= per) { w->mp_acc -= per; w->clock++; }
}

void world_set_force_pass(world_t *w, bool (*fn)(int32_t x, int32_t y))
{
    if (w) w->force_pass = fn;
}

void world_set_blocked_obj(world_t *w, const sparsebits_t *bitmap)
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
    world_spend(w, cost);
    return true;
}
