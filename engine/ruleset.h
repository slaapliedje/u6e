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
    /* Behavioural hooks are added HERE where SE/MD actually diverge from U6 (engine/CLAUDE.md):
     * the natural next ones are on_use_item (the door/container/mechanism usecode currently baked
     * into gamedemo's use_object_inner), on_combat_hit, on_rest, area_group (music). They are added
     * with their U6 default + real call site when a per-game override lands, NOT speculatively. */
} RuleSet;

/* Well-known object numbers — the obj_n of the objects that SHARED (non-usecode) systems reference
 * by IDENTITY. U6, SE and MD renumber these, so they are per-game DATA, not #defines. The engine
 * reads them from the loaded GameDef instead of hard-coding U6's numbers. 0 = "this game has no
 * such object / not yet identified" — shared code MUST treat 0 as no-match (never test obj_n==0).
 * Deep per-item USE behaviour (door/container/mechanism frames) stays in the usecode path; it moves
 * behind a RuleSet on_use hook later, so those obj_n stay there rather than in this table. */
typedef struct WellKnownObjs {
    uint16_t avatar;        /* the Avatar actor, drawn at view centre.   U6 410  SE 310  MD (TBD) */
    uint16_t key;           /* key (opens a lock whose quality it matches).  U6 64   SE 53        */
    uint16_t lockpick;      /* lockpick (opens any lock).                    U6 63   (SE/MD none)  */
    uint16_t spellbook;     /* U6 spellbook inventory item.  U6 57.  SE/MD have NO U6 magic => 0  */
    uint16_t reagent_base;  /* first of 8 consecutive reagent obj_n.  U6 65.  SE/MD => 0 (none)   */
    uint16_t gold;          /* gold coin -> the Gold pool on GET.  U6 88  SE 69  MD (TBD, dollar) */
} WellKnownObjs;

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
    WellKnownObjs wko;           /* per-game obj_n of shared, identity-referenced objects */
} GameDef;

extern const RuleSet ruleset_u6;   /* the default rule set */
extern const GameDef  gamedef_u6;  /* Ultima VI                                    */
extern const GameDef  gamedef_se;  /* The Savage Empire  (core pack magic "SEWT")  */
extern const GameDef  gamedef_md;  /* Martian Dreams     (core pack magic "MDWT")  */

/* Select the GameDef whose pack_magic matches the first 4 bytes at `magic`, or NULL if none
 * is registered. This is the multi-game gate: the same binary accepts any registered game's pack. */
const GameDef *gamedef_for_magic(const char *magic);

#endif
