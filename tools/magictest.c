// SPDX-License-Identifier: GPL-2.0-or-later
/* magictest.c — host unit tests for the magic rules (docs/rules/magic_skills.md §9).
 * Verifies: reagent bit->obj_n mapping, mana_cost==circle, the full spell table
 * is well-formed, gate-check ordering + messages, and consume-exactly-once via
 * the inventory store (no reagents/mana lost on a fizzle). */
#include <stdio.h>
#include <string.h>
#include "magic.h"
#include "objects.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("  [PASS] %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); fails++; } } while (0)

/* find a spell by name -> dense index */
static int spell_by_name(const char *n){
    for (int i = 0; i < SPELL_COUNT; i++)
        if (strcmp(g_spells[i].name, n) == 0) return i;
    return -1;
}

int main(void)
{
    printf("== reagent mapping ==\n");
    CHECK(magic_reagent_objn(0) == 65, "reagent bit 0 -> obj_n 65 (black pearl)");
    CHECK(magic_reagent_objn(7) == 72, "reagent bit 7 -> obj_n 72 (sulfurous ash)");
    CHECK(magic_reagent_objn(8) == 0,  "reagent bit 8 out of range -> 0");

    printf("== spell table well-formed ==\n");
    int ok_circles = 1, ok_cost = 1;
    for (int i = 0; i < SPELL_COUNT; i++) {
        if (g_spells[i].circle < 1 || g_spells[i].circle > 8) ok_circles = 0;
        if (magic_mana_cost(i) != g_spells[i].circle) ok_cost = 0;
    }
    CHECK(ok_circles, "every spell has a circle in 1..8");
    CHECK(ok_cost,    "mana_cost(spell) == circle for all spells");
    CHECK(SPELL_COUNT == 80, "80 spells total (8*10)");
    { int arm = spell_by_name("Armageddon"), dw = spell_by_name("Death Wind"), wc = spell_by_name("Wind Change");
      CHECK(arm >= 0 && magic_spell_num(arm) == 112 && g_spells[arm].reagents == 0, "Armageddon = circle 8 idx 0 (spell_num 112), no reagents");
      CHECK(dw >= 0 && magic_spell_num(dw) == 113, "Death Wind -> spell_num 113 (the reference's number)");
      CHECK(wc >= 0 && magic_spell_num(wc) == 57 && spell_by_name("Wild Change") < 0, "Wind Change (circle 4 idx 9 = 57); the old typo is gone"); }

    printf("== known spells (spot checks) ==\n");
    int heal = spell_by_name("Heal");
    CHECK(heal >= 0 && g_spells[heal].circle == 1, "Heal is 1st circle");
    CHECK(heal >= 0 && g_spells[heal].reagents == (R_GI|R_SS), "Heal needs Ginseng+SpiderSilk");
    int gt = spell_by_name("Gate Travel");
    CHECK(gt >= 0 && g_spells[gt].circle == 7, "Gate Travel is 7th circle");
    int help = spell_by_name("Help");
    CHECK(help >= 0 && g_spells[help].reagents == 0, "Help needs no reagents");
    int cf = spell_by_name("Create Food");
    CHECK(cf >= 0 && g_spells[cf].circle == 1 && g_spells[cf].reagents == (R_GA|R_GI|R_MA),
          "Create Food = circle 1, Garlic+Ginseng+Mandrake");
    int um = spell_by_name("Unlock Magic");
    CHECK(um >= 0 && g_spells[um].circle == 2 && g_spells[um].reagents == (R_SA|R_BM),
          "Unlock Magic = circle 2, SulfurousAsh+BloodMoss");
    int ff = spell_by_name("Fire Field");
    CHECK(ff >= 0 && g_spells[ff].circle == 4, "Fire Field = circle 4");
    int ef = spell_by_name("Energy Field");
    CHECK(ef >= 0 && g_spells[ef].circle == 5, "Energy Field = circle 5");
    int df = spell_by_name("Dispel Field");
    CHECK(df >= 0 && g_spells[df].circle == 3, "Dispel Field = circle 3");
    int po = spell_by_name("Poison");
    CHECK(po >= 0 && g_spells[po].circle == 2, "Poison = circle 2");
    int cu = spell_by_name("Curse");
    CHECK(cu >= 0 && g_spells[cu].circle == 3, "Curse = circle 3");
    int ch = spell_by_name("Charm");
    CHECK(ch >= 0 && g_spells[ch].circle == 6, "Charm = circle 6");
    int pr = spell_by_name("Protection");
    CHECK(pr >= 0 && g_spells[pr].circle == 3, "Protection = circle 3");
    int co = spell_by_name("Conjure");
    CHECK(co >= 0 && g_spells[co].circle == 4, "Conjure = circle 4");
    int cl = spell_by_name("Clone");
    CHECK(cl >= 0 && g_spells[cl].circle == 6, "Clone = circle 6");
    int su = spell_by_name("Summon");
    CHECK(su >= 0 && g_spells[su].circle == 8, "Summon = circle 8");
    int he = spell_by_name("Help");
    CHECK(he >= 0 && g_spells[he].circle == 1 && g_spells[he].reagents == 0, "Help = circle 1, no reagents");
    int gt2 = spell_by_name("Gate Travel");
    CHECK(gt2 >= 0 && g_spells[gt2].circle == 7, "Gate Travel = circle 7");
    int lo = spell_by_name("Locate");
    CHECK(lo >= 0 && g_spells[lo].circle == 4, "Locate = circle 4");
    int tk = spell_by_name("Telekinesis");
    CHECK(tk >= 0 && g_spells[tk].circle == 2, "Telekinesis = circle 2");
    int xr = spell_by_name("X-ray");
    CHECK(xr >= 0 && g_spells[xr].circle == 5, "X-ray = circle 5");
    int dm = spell_by_name("Detect Magic");
    CHECK(dm >= 0 && g_spells[dm].circle == 1, "Detect Magic = circle 1");
    /* authentic numbering: first spell of circle 3 == (3-1)*16 == 32 */
    int c3first = spell_by_name("Curse");
    CHECK(c3first >= 0 && magic_spell_num(c3first) == 32, "Curse (circle 3, idx 0) -> spell_num 32");

    printf("== max mana from intelligence (magic_skills.md §3) ==\n");
    CHECK(magic_max_mana(15, 1) == 30, "Avatar int 15 -> 30 max mana (2x)");
    CHECK(magic_max_mana(18, 0) == 9,  "companion int 18 -> 9 max mana (int/2)");
    CHECK(magic_max_mana(30, 1) == 60, "Avatar int 30 -> 60 max mana");

    printf("== castable circle by intelligence ==\n");
    CHECK(magic_castable_circle(15) == 3, "int 15 -> circles up to 3");
    CHECK(magic_castable_circle(30) == 7, "int 30 -> circles up to 7");
    CHECK(magic_castable_circle(3)  == 1, "low int still allows circle 1");

    printf("== gate-check ordering (Heal: Gi+SS, cost 1, circle 1) ==\n");
    uint8_t all = 0xFF;                 /* has every reagent */
    CHECK(magic_check(heal, 1, 15, all, 5) == CAST_OK,          "all preconditions met -> OK");
    CHECK(magic_check(heal, 0, 15, all, 5) == CAST_UNKNOWN,     "unknown spell refused first");
    /* Fireball is circle 3; at int 8 castable_circle==2 -> too low */
    int fb = spell_by_name("Fireball");
    CHECK(magic_check(fb, 1, 8, all, 8) == CAST_LOWLEVEL,       "circle over level -> LOWLEVEL");
    CHECK(magic_check(heal, 1, 15, R_SA, 5) == CAST_NOREAGENTS, "missing Gi/SS -> NOREAGENTS");
    CHECK(magic_check(heal, 1, 15, all, 0) == CAST_NOMANA,      "0 mana -> NOMANA");
    CHECK(magic_check(999, 1, 15, all, 5) == CAST_BADSPELL,     "bad index -> BADSPELL");

    printf("== consume exactly once (reagents + mana) ==\n");
    /* inventory: 2 ginseng (68), 2 spider silk (71), 1 sulfurous ash (72) */
    wobj_t none[1];
    static uint8_t gettable[1024];
    objworld_t o; objworld_init(&o, none, 0, gettable);
    o.inv[o.ninv++] = (invitem_t){68, 0, 2, 0};
    o.inv[o.ninv++] = (invitem_t){71, 0, 2, 0};
    o.inv[o.ninv++] = (invitem_t){72, 0, 1, 0};

    /* build have_reagents mask from inventory */
    uint8_t mask = 0;
    for (int b = 0; b < REAG_COUNT; b++)
        if (objworld_count(&o, (uint16_t)magic_reagent_objn(b)) > 0) mask |= (uint8_t)(1 << b);
    CHECK(mask == (R_GI|R_SS|R_SA), "reagent mask built from inventory (Gi|SS|SA)");

    int mana = 5;
    cast_result_t r = magic_check(heal, 1, 15, mask, mana);
    CHECK(r == CAST_OK, "Heal castable with this inventory");
    if (r == CAST_OK) {
        /* consume one of each required reagent + the mana */
        for (int b = 0; b < REAG_COUNT; b++)
            if (g_spells[heal].reagents & (1 << b))
                objworld_consume(&o, (uint16_t)magic_reagent_objn(b), 1);
        mana -= magic_mana_cost(heal);
    }
    CHECK(objworld_count(&o, 68) == 1, "one ginseng consumed (2->1)");
    CHECK(objworld_count(&o, 71) == 1, "one spider silk consumed (2->1)");
    CHECK(objworld_count(&o, 72) == 1, "sulfurous ash untouched (not required)");
    CHECK(mana == 4, "one mana spent (5->4)");

    /* a fizzle (no mana) must NOT change inventory */
    mana = 0;
    r = magic_check(heal, 1, 15, mask, mana);
    CHECK(r == CAST_NOMANA, "second cast fizzles on mana");
    CHECK(objworld_count(&o, 68) == 1 && objworld_count(&o, 71) == 1,
          "fizzle consumed nothing (check happens before consume)");

    printf("\nRESULT: %s\n", fails ? "FAIL" : "ALL PASS");
    return fails ? 1 : 0;
}
