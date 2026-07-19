// SPDX-License-Identifier: GPL-2.0-or-later
/* ruleset.h — GameDef + RuleSet scaffolding (M5, C interface-struct pattern).
 *
 * Per engine/CLAUDE.md: shared systems call RuleSet hooks; the default impl is
 * U6, and Savage/Martian later override only the pointers that differ. A NULL
 * hook means "use the U6 default". M5 only needs the movement turn-cost hook;
 * more hooks (on_combat_hit, on_use_item, ...) get added where the games
 * actually diverge.
 */
#ifndef U6E_RULESET_H
#define U6E_RULESET_H

#include <stdint.h>

struct world;   /* fwd */

typedef struct RuleSet {
    /* Turn cost (game ticks) of a step by (dx,dy). NULL => default (10 per
     * orthogonal step, the U6 turn granularity). */
    int (*move_turn_cost)(const struct world *w, int dx, int dy);
} RuleSet;

/* A game identity + its data model. The engine is one binary; the GameDef selected at load time
 * (by the asset-pack magic) supplies the per-game constants + rule overrides. SE/MD share U6's
 * 1024x1024 surface geometry, so most fields match U6; the deltas are id/magic/rules/level policy.
 * See docs/design/multigame.md (M9). */
typedef struct GameDef {
    const char *id;              /* "u6" / "savage" / "martian" (stable slug) */
    const char *name;            /* display name, e.g. "Ultima VI"            */
    char        pack_magic[4];   /* asset-pack magic that selects this game ("U6WT", ...) */
    /* World geometry (data, not #define). SE/MD currently == U6. */
    int32_t     surface_tiles;   /* tiles per side of the toroidal surface (1024)  */
    int32_t     dungeon_levels;  /* number of dungeon levels (U6=5)               */
    int32_t     start_x, start_y;/* fallback start; the save/pack header overrides */
    const RuleSet *rules;        /* NULL => all-default U6 rules                   */
} GameDef;

extern const RuleSet ruleset_u6;   /* the default rule set */
extern const GameDef  gamedef_u6;  /* Ultima VI                                    */

/* Select the GameDef whose pack_magic matches the first 4 bytes at `magic`, or NULL if none
 * is registered. This is the multi-game gate: the same binary accepts any registered game's pack. */
const GameDef *gamedef_for_magic(const char *magic);

#endif
