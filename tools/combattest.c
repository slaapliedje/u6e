// SPDX-License-Identifier: GPL-2.0-or-later
/* combattest.c — host unit test for engine/combat.c (docs/rules/combat.md §2-3).
 * Verifies the hit-chance distribution + damage roll/armour behave per spec. */
#include <stdio.h>
#include "combat.h"
#include "rng.h"

static int fails = 0;
#define CHECK(cond, msg) do{ if(!(cond)){ printf("FAIL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); }while(0)

int main(void){
    rng_t r; rng_seed(&r, 12345);
    const int N = 20000;

    /* §2 hit chance. threshold = (foe_dex + 30 - attack_role)/2 vs roll 1..30.
     * equal 15/15 -> threshold 15 -> ~ (30-15)/30 = 50% hit. */
    int hits=0; for(int i=0;i<N;i++) hits += combat_hit(15,15,&r);
    double p_eq = 100.0*hits/N;
    printf("hit%% equal(15v15) = %.1f\n", p_eq);
    CHECK(p_eq>45 && p_eq<55, "equal stats ~50% hit");

    hits=0; for(int i=0;i<N;i++) hits += combat_hit(30,1,&r);    /* strong attacker vs weak foe */
    double p_hi = 100.0*hits/N; printf("hit%% strong(30v1) = %.1f\n", p_hi);
    CHECK(p_hi>90, "strong attacker vs weak foe > 90% hit");
    hits=0; for(int i=0;i<N;i++) hits += combat_hit(30,15,&r);   /* strong vs avg ~ 77% */
    printf("hit%% strong(30v15) = %.1f (expect ~77)\n", 100.0*hits/N);

    hits=0; for(int i=0;i<N;i++) hits += combat_hit(1,30,&r);    /* weak vs agile */
    double p_lo = 100.0*hits/N; printf("hit%% weak(1v30) = %.1f\n", p_lo);
    CHECK(p_lo<10, "weak attacker < 10% hit");

    /* §3 damage roll: sword max 15, AC 0 -> 1..15 */
    int mn=999,mx=0; for(int i=0;i<N;i++){ int d=combat_damage(15,0,&r); if(d<mn)mn=d; if(d>mx)mx=d; }
    printf("dmg(15,ac0) range = %d..%d\n", mn, mx);
    CHECK(mn>=1 && mx<=15 && mx>=14, "sword dmg in 1..15");

    /* armour reduces average damage and never goes negative */
    long sum0=0,sum8=0; int neg=0;
    for(int i=0;i<N;i++){ sum0+=combat_damage(15,0,&r); int d=combat_damage(15,8,&r); sum8+=d; if(d<0)neg++; }
    printf("avg dmg ac0 = %.2f  ac8 = %.2f\n", (double)sum0/N, (double)sum8/N);
    CHECK(sum8 < sum0, "armour AC8 reduces average damage");
    CHECK(neg==0, "damage never negative");

    /* specials */
    CHECK(combat_damage(255,10,&r)==255, "255 = instakill passes through armour");
    CHECK(combat_damage(0,0,&r)==0, "no weapon = 0 damage");
    CHECK(combat_damage(1,0,&r)==1, "min weapon = 1 damage");

    /* §4-6 tables */
    CHECK(weapon_dmg(43)==15, "sword dmg 15");
    CHECK(weapon_dmg(47)==30, "halberd dmg 30");
    CHECK(weapon_dmg(48)==255, "glass sword instakill");
    CHECK(weapon_dmg(999)==0, "non-weapon dmg 0");
    CHECK(weapon_range(43)==1, "sword melee range 1");
    CHECK(weapon_range(41)==5, "bow range 5");
    CHECK(weapon_range(42)==7, "crossbow range 7");
    CHECK(weapon_range(412)==5 && weapon_dmg(412)==30, "ship cannons: range 5, damage 30 (cannons.md)");
    CHECK(armour_ac(22)==7, "plate mail AC 7");
    CHECK(armour_ac(23)==10, "magic armour AC 10");
    CHECK(armour_ac(999)==0, "non-armour AC 0");

    printf(fails? "\n%d CHECK(S) FAILED\n" : "\nALL COMBAT TESTS PASS\n", fails);
    return fails?1:0;
}
