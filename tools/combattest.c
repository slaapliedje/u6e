// SPDX-License-Identifier: GPL-2.0-or-later
/* combattest.c — host unit test for engine/combat.c (docs/rules/combat.md §2-3).
 * Verifies the hit-chance distribution + damage roll/armour behave per spec. */
#include <stdio.h>
#include "combat.h"
#include "equip.h"
#include "monsters.h"
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

    /* Savage Empire (se_combat.md): its tables, and a hit on the threshold itself */
    rules_select(RULES_SE);
    CHECK(weapon_dmg(40)==35 && weapon_range(40)==8, "SE modern rifle 35, range 8");
    CHECK(weapon_dmg(24)==7 && weapon_range(24)==1, "SE club 7, melee");
    CHECK(weapon_dmg(43)==0 && !weapon_is(91), "SE: U6 weapons are not SE's");
    CHECK(armour_ac(67)==7 && armour_ac(8)==3 && armour_ac(22)==0, "SE armour: kotl shield 7, leather 3, none for U6 plate");
    CHECK(equip_class(84)==EQ_NECK && equip_class(31)==EQ_ARM2 && equip_class(2)==EQ_CLS_OFFHAND && equip_class(24)==EQ_ARM && equip_class(1)==EQ_ARM && equip_class(22)<0, "SE ready classes");
    { mon_base_t mb; CHECK(monster_base(294,&mb) && mb.str==60 && mb.hp==255 && mb.dmg==150 && mb.align==ALIGN_CHAOTIC, "SE tyrannosaurus row");
      CHECK(monster_base(323,&mb) && mb.align==ALIGN_EVIL && monster_base(310,&mb) && mb.align==ALIGN_GOOD && monster_base(303,&mb) && mb.align==ALIGN_NEUTRAL, "SE alignments"); }
    CHECK((monster_type_flags(302)&0x200) && monster_movetype(302)==MT_WATER_HIGH && monster_movetype(288)==MT_AIR_LOW && monster_str_based(294) && !monster_str_based(323), "SE creature flags");
    CHECK(weapon_shoots(31) && weapon_shoots(39) && weapon_shoots(241) && !weapon_shoots(26) && !weapon_shoots(21), "SE: what always shoots");
    CHECK(weapon_missile(31)==566 && weapon_missile(40)==263 && weapon_missile(26)==547 && weapon_missile(21)==266, "SE missile tiles");
    CHECK(weapon_ammo(31)==45 && weapon_ammo(40)==41 && weapon_ammo(32)==36 && weapon_ammo(37)==26 && weapon_ammo(50)==0, "SE ammunition");
    hits=0; for(int i=0;i<N;i++) hits += combat_hit(16,16,&r);    /* threshold 15: rolls 15..30 hit, 16 in 30 */
    { double p=100.0*hits/N; printf("SE hit%% 16v16 = %.1f (expect ~53.3)\n", p); CHECK(p>50.5 && p<56, "SE hits on the threshold"); }
    rules_select(RULES_U6);
    CHECK(weapon_dmg(43)==15 && armour_ac(22)==7 && equip_class(22)==EQ_BODY, "U6 tables back");
    CHECK(weapon_ammo(41)==0 && weapon_missile(41)==566 && weapon_shoots(41), "U6 shooting back");

    printf(fails? "\n%d CHECK(S) FAILED\n" : "\nALL COMBAT TESTS PASS\n", fails);
    return fails?1:0;
}
