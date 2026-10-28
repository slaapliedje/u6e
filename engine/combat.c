// SPDX-License-Identifier: GPL-2.0-or-later
/* combat.c — U6 combat math (docs/rules/combat.md §2-3), transcribed from the spec
 * (derived from Nuvie data/scripts/u6/actor.lua: actor_combat_hit_check / actor_take_hit). */
#include "combat.h"

/* §2: MISS if floor((foe_dex + 30 - attack_role) / 2) >= rand(1..30), else HIT. */
int combat_hit(int attack_role, int foe_dex, rng_t *r)
{
    int roll = 1 + rng_range(r, 30);                 /* 1..30 */
    return ((foe_dex + 30 - attack_role) / 2 >= roll) ? 0 : 1;
}

/* §3: damage roll then armour soak.
 *   max = weapon_max; if max>1 and !=255: max = rand(1..max)
 *   if max!=255 and ac>0: max -= rand(1..ac)
 *   result = max>0 ? max : 0   (255 instakill passes through). */
int combat_damage(int weapon_max, int ac, rng_t *r)
{
    int max = weapon_max;
    if (max <= 0) return 0;
    if (max > 1 && max != 255) max = 1 + rng_range(r, max);       /* 1..weapon_max */
    if (max != 255 && ac > 0)  max -= 1 + rng_range(r, ac);       /* -(1..ac) armour */
    return max > 0 ? max : 0;
}

/* §4 weapon damage table (obj_n -> max damage). */
int weapon_dmg(int obj_n)
{
    switch (obj_n){
    case 4: return 4;    case 13: return 4;   case 33: return 6;   case 34: return 8;
    case 35: return 8;   case 36: return 10;  case 37: return 10;  case 38: return 6;
    case 39: return 15;  case 40: return 15;  case 41: return 10;  case 42: return 12;
    case 43: return 15;  case 44: return 20;  case 45: return 20;  case 46: return 20;
    case 47: return 30;  case 48: return 255; case 49: return 8;   case 50: return 12;
    case 54: return 20;  case 78: return 4;   case 79: return 30;  case 80: return 20;
    case 83: return 4;   case 221: return 90; case 412: return 30;
    default: return 0;
    }
}

/* §6 weapon range in tiles (1 = melee/reach). Projectile weapons reach farther. */
int weapon_range(int obj_n)
{
    switch (obj_n){
    case 41: return 5;   /* bow            */
    case 42: return 7;   /* crossbow       */
    case 50: return 7;   /* triple crossbow*/
    case 54: return 7;   /* magic bow      */
    case 33: return 4;   /* sling          */
    case 37: return 3;   /* throwing axe   */
    case 36: return 4;   /* spear (thrown) */
    case 49: return 5;   /* boomerang      */
    case 91: return 4;   /* Zu Ylem        */
    case 79: return 7;   /* lightning wand */
    case 80: return 7;   /* fire wand      */
    case 412: return 5;  /* a ship's cannons (cannons.md §1) */
    default: return 1;   /* melee          */
    }
}

/* §6b the tile a weapon throws or shoots (0 = not a missile weapon). */
int weapon_missile(int obj_n)
{
    switch (obj_n){
    case 33: return 398;   /* sling: a stone      */
    case 36: return 547;   /* spear               */
    case 37: return 548;   /* throwing axe        */
    case 38: return 549;   /* dagger (thrown)     */
    case 41: case 54: return 566;   /* bow, magic bow: an arrow */
    case 42: case 50: return 567;   /* crossbow, triple crossbow: a bolt */
    case 49: return 560;   /* boomerang           */
    case 83: return 601;   /* flask of oil        */
    case 91: return 612;   /* Zu Ylem             */
    case 79: return 392;   /* lightning wand      */
    case 80: return 393;   /* fire wand           */
    case 412: return 399;  /* a ship's cannons: the cannonball (cannons.md §1) */
    default: return 0;
    }
}

/* §5 armour class of a readied item (obj_n -> AC). */
int armour_ac(int obj_n)
{
    switch (obj_n){
    case 1: return 1;   case 2: return 2;   case 3: return 3;   case 4: return 3;
    case 5: return 2;   case 6: return 2;   case 7: return 3;   case 8: return 5;
    case 9: return 2;   case 10: return 3;  case 11: return 3;  case 12: return 3;
    case 13: return 2;  case 14: return 2;  case 15: return 4;  case 16: return 5;
    case 17: return 1;  case 18: return 2;  case 19: return 3;  case 20: return 4;
    case 21: return 5;  case 22: return 7;  case 23: return 10; case 24: return 2;
    case 35: return 1;  case 256: return 5;
    default: return 0;
    }
}
