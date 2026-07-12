// SPDX-License-Identifier: GPL-2.0-or-later
/* combat.h — U6 combat math (docs/rules/combat.md). Pure, deterministic via rng_t,
 * so it unit-tests and is reused by both the player attack and enemy AI. */
#ifndef U6E_COMBAT_H
#define U6E_COMBAT_H

#include "rng.h"

/* §2 hit check: attack_role = attacker's str_adj or dex_adj; foe_dex = defender's
 * dex_adj. Returns 1 = hit, 0 = miss. (A single d30 roll.) */
int combat_hit(int attack_role, int foe_dex, rng_t *r);

/* §3 damage: weapon_max = weapon/monster base damage; ac = defender armour class.
 * Returns the damage to apply: 0 = grazed (no effect), 255 = instant kill (passes
 * through, e.g. glass sword), else 1..weapon_max reduced by 1..ac of armour. */
int combat_damage(int weapon_max, int ac, rng_t *r);

/* Static U6 tables (docs/rules/combat.md §4-6), by object number. */
int weapon_dmg(int obj_n);    /* §4 max damage; 0 if obj_n is not a weapon            */
int weapon_range(int obj_n);  /* §6 attack range in tiles; 1 = melee, >1 = ranged     */
int armour_ac(int obj_n);     /* §5 armour-class contribution of a readied item; 0 if not armour */

#endif
