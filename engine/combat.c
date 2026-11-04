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
    case 100: return 6;  case 101: return 4;  case 103: return 4;  case 104: return 4;
    case 105: return 4;  case 109: return 2;  case 113: return 4;  case 114: return 4;
    case 141: return 4;   /* tools: combat.md §6c */
    default: return 0;
    }
}

/* §6c weapon range (1 = melee). The morning star and halberd reach 2 without shooting. */
int weapon_range(int obj_n)
{
    switch (obj_n){
    case 40: case 47: return 2;   /* morning star, halberd */
    case 37: case 38: return 3;   /* throwing axe, dagger  */
    case 33: case 36: case 91: return 4;   /* sling, spear, Zu Ylem */
    case 41: case 49: case 83: case 412: return 5;   /* bow, boomerang, oil, a ship's cannons (cannons.md §1) */
    case 42: case 50: case 54: case 79: case 80: return 7;   /* crossbows, magic bow, wands */
    default: return 1;
    }
}

/* §6c: an item that fights when readied (a row in the damage table, Zu Ylem's being 0) */
int weapon_is(int obj_n){ return obj_n==91 || weapon_dmg(obj_n)>0; }

/* §6c: never mind the distance, this weapon shoots (the morning star and halberd never do) */
int weapon_shoots(int obj_n)
{
    switch (obj_n){
    case 33: case 37: case 41: case 42: case 49: case 50: case 79: case 80: case 83: case 91: return 1;
    default: return 0;
    }
}

/* §6c distance: a table, not the larger of dx and dy */
int combat_dist(int dx,int dy)
{
    static const uint8_t T[8][8]={ {0,1,2,3,4,5,6,7},{1,1,2,3,4,5,6,7},{2,2,2,3,4,5,6,7},{3,3,3,4,5,6,7,7},
                                   {4,4,4,5,6,7,7,8},{5,5,5,6,7,7,8,8},{6,6,6,7,7,8,8,8},{7,7,7,7,8,8,8,8} };
    if(dx<0) dx=-dx;
    if(dy<0) dy=-dy;
    return (dx>=8||dy>=8) ? 9 : T[dx][dy];
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
