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

typedef struct GameDef {
    const char *id;            /* "u6" / "savage" / "martian" */
    int32_t     start_x, start_y;
    const RuleSet *rules;      /* NULL => all-default U6 rules */
} GameDef;

extern const RuleSet ruleset_u6;   /* the default rule set */

#endif
