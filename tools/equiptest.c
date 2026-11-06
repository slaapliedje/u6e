// SPDX-License-Identifier: GPL-2.0-or-later
/* equiptest.c — host unit test for the readied-equipment slot model
 * (engine/equip.c, from docs/rules/equipment.md). Run by `make equiptest`. */
#include <stdio.h>
#include <string.h>
#include "equip.h"
#include "combat.h"

static int fails;
#define OK(cond, msg) do { if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    uint16_t s[EQ_NSLOTS]; uint16_t w; uint8_t ac;

    /* placement basics */
    memset(s, 0, sizeof s);
    OK(equip_ready(s, 43) == EQ_ARM,  "sword -> main hand");
    OK(equip_ready(s, 9)  == EQ_ARM2, "shield overflows to off hand");
    OK(equip_ready(s, 38) == EQUIP_EFULL, "third arm item refused");
    OK(equip_ready(s, 3)  == EQ_HEAD, "iron helm -> head");
    OK(equip_ready(s, 1)  == EQUIP_EFULL, "second helm refused");
    OK(equip_ready(s, 22) == EQ_BODY, "plate mail -> body");
    OK(equip_ready(s, 27) == EQ_FOOT, "boots -> feet");
    OK(equip_ready(s, 74) == EQ_NECK, "ankh amulet -> neck");
    OK(equip_ready(s, 82)  == EQ_HAND,  "ring -> hand");
    OK(equip_ready(s, 256) == EQ_HAND2, "second ring -> other hand");
    OK(equip_ready(s, 257) == EQUIP_EFULL, "third ring refused");
    OK(equip_ready(s, 88) == EQUIP_ENOTREADIABLE, "gold coin not readiable");

    /* fold: sword 15 dmg is the weapon; AC sums shield + helm + plate + protection ring */
    equip_fold(s, &w, &ac);
    OK(w == 43, "fold picks the sword");
    OK(ac == (uint8_t)(armour_ac(9) + armour_ac(3) + armour_ac(22) + armour_ac(82) + armour_ac(256)),
       "fold sums armour AC");

    /* two-handed rules */
    memset(s, 0, sizeof s);
    OK(equip_ready(s, 47) == EQ_ARM, "halberd (two-handed) takes main hand");
    OK(equip_ready(s, 9)  == EQUIP_EFULL, "shield refused beside a two-handed weapon");
    OK(equip_ready(s, 38) == EQUIP_EFULL, "dagger refused beside a two-handed weapon");
    OK(equip_unready(s, EQ_ARM) == 47, "unready returns the halberd");
    OK(equip_ready(s, 38) == EQ_ARM, "dagger fits after unready");
    OK(equip_ready(s, 41) == EQUIP_EFULL, "bow refused with one hand occupied");

    /* fold on an empty set = unarmed, AC 0 */
    memset(s, 0, sizeof s);
    equip_fold(s, &w, &ac);
    OK(w == 0 && ac == 0, "empty set folds to unarmed / AC 0");

    /* the strongest readied weapon wins the fold */
    (void)equip_ready(s, 38);                       /* dagger 6  */
    (void)equip_ready(s, 39);                       /* mace 15   */
    equip_fold(s, &w, &ac);
    OK(w == 39, "fold picks the strongest weapon (mace over dagger)");

    /* DOS's slots (equipment.md §9) */
    { uint16_t d[EQ_NSLOTS] = {0};
      OK(equip_ready(d, 9)  == EQ_ARM2, "DOS: a shield goes to the off hand first");
      OK(equip_ready(d, 43) == EQ_ARM,  "DOS: then a sword takes the main hand");
      uint16_t e[EQ_NSLOTS] = {0}; (void)equip_ready(e, 35);
      OK(equip_ready(e, 9)  == EQ_ARM,  "DOS: a shield beside a main gauche takes the main hand");
      OK(equip_class(203) < 0 && equip_class(204) < 0, "DOS: pliers and hammer can't be readied");
      OK(equip_class(141) == EQ_ARM && equip_class(115) == EQ_ARM && equip_class(116) == EQ_ARM, "DOS: decorative sword, wine, mead in a hand");
      OK(equip_class(26) == EQ_NECK, "DOS: the gargoyle belt on the neck"); }

    /* every obj_n with weapon damage or armour AC must be readiable (spec §3 note) */
    { int miss = 0;
      for (int o = 1; o < 300; o++)
          if (o == 221) continue;   /* cannon: a fixed emplacement, in the damage table but never carried */
          else if ((weapon_dmg(o) > 0 || armour_ac(o) > 0) && equip_class(o) < 0) { miss++;
              printf("  unreadiable combat item obj_n %d\n", o); }
      OK(miss == 0, "all combat-table items are readiable"); }

    if (fails) { printf("\n%d EQUIP TEST(S) FAILED\n", fails); return 1; }
    printf("\nALL EQUIP TESTS PASS\n");
    return 0;
}
