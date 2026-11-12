// SPDX-License-Identifier: GPL-2.0-or-later
/* equip.c — readied-equipment slots, implemented from docs/rules/equipment.md.
 * Pure integer table + placement logic; no I/O, no globals (unit-testable on host). */
#include "equip.h"
#include "combat.h"   /* weapon_dmg / armour_ac for the §4 fold */

/* §3 readiable table: obj_n -> class (preferred slot; EQ_ARM2 = two-handed, EQ_CLS_OFFHAND = off hand first).
 * Ids as DOS readies them (§9). */
typedef struct { uint16_t obj_n; uint8_t cls; } eqent_t;
static const eqent_t EQTAB[] = {
    /* HEAD: helms */
    {1,EQ_HEAD},{2,EQ_HEAD},{3,EQ_HEAD},{4,EQ_HEAD},{5,EQ_HEAD},{6,EQ_HEAD},{7,EQ_HEAD},{8,EQ_HEAD},
    /* ARM: shields */
    {9,EQ_CLS_OFFHAND},{10,EQ_CLS_OFFHAND},{11,EQ_CLS_OFFHAND},{12,EQ_CLS_OFFHAND},{13,EQ_CLS_OFFHAND},{14,EQ_CLS_OFFHAND},{15,EQ_CLS_OFFHAND},{16,EQ_CLS_OFFHAND},
    /* BODY: armour + clothes + belts + cloak */
    {17,EQ_BODY},{18,EQ_BODY},{19,EQ_BODY},{20,EQ_BODY},{21,EQ_BODY},{22,EQ_BODY},{23,EQ_BODY},
    {25,EQ_BODY},{81,EQ_BODY},{124,EQ_BODY},{126,EQ_BODY},{137,EQ_BODY},
    /* NECK */
    {24,EQ_NECK},{26,EQ_NECK},{74,EQ_NECK},{75,EQ_NECK},{76,EQ_NECK},
    /* FOOT */
    {27,EQ_FOOT},{28,EQ_FOOT},
    /* ARM: one-handed weapons + tools */
    {33,EQ_ARM},{34,EQ_ARM},{35,EQ_CLS_OFFHAND},{36,EQ_ARM},{37,EQ_ARM},{38,EQ_ARM},{39,EQ_ARM},{40,EQ_ARM},
    {43,EQ_ARM},{48,EQ_ARM},{49,EQ_ARM},{57,EQ_ARM},{78,EQ_ARM},{79,EQ_ARM},{80,EQ_ARM},{83,EQ_ARM},
    {90,EQ_ARM},{91,EQ_ARM},{100,EQ_ARM},{101,EQ_ARM},{102,EQ_ARM},{103,EQ_ARM},{104,EQ_ARM},
    {105,EQ_ARM},{109,EQ_ARM},{113,EQ_ARM},{114,EQ_ARM},{115,EQ_ARM},{116,EQ_ARM},{141,EQ_ARM},{158,EQ_ARM},
    /* TWO-HANDED (class stored as EQ_ARM2) */
    {41,EQ_ARM2},{42,EQ_ARM2},{44,EQ_ARM2},{45,EQ_ARM2},{46,EQ_ARM2},{47,EQ_ARM2},{50,EQ_ARM2},{54,EQ_ARM2},
    /* HAND: rings */
    {82,EQ_HAND},{256,EQ_HAND},{257,EQ_HAND},{258,EQ_HAND},
};

/* Savage Empire's classes (se_combat.md §1, 0xe5f3) */
static const eqent_t SE_EQTAB[] = {
    {84,EQ_NECK},{85,EQ_NECK},{135,EQ_NECK},
    {7,EQ_BODY},{8,EQ_BODY},{9,EQ_BODY},
    {31,EQ_ARM2},{32,EQ_ARM2},{34,EQ_ARM2},{40,EQ_ARM2},{50,EQ_ARM2},{94,EQ_ARM2},{118,EQ_ARM2},{128,EQ_ARM2},{131,EQ_ARM2},{175,EQ_ARM2},{212,EQ_ARM2},
    {2,EQ_CLS_OFFHAND},{13,EQ_CLS_OFFHAND},{14,EQ_CLS_OFFHAND},{15,EQ_CLS_OFFHAND},{67,EQ_CLS_OFFHAND},
    {1,EQ_ARM},{24,EQ_ARM},{25,EQ_ARM},{26,EQ_ARM},{27,EQ_ARM},{28,EQ_ARM},{29,EQ_ARM},{33,EQ_ARM},{37,EQ_ARM},{39,EQ_ARM},{44,EQ_ARM},
    {52,EQ_ARM},{119,EQ_ARM},{137,EQ_ARM},{177,EQ_ARM},{208,EQ_ARM},{206,EQ_ARM},{240,EQ_ARM},{21,EQ_ARM},{138,EQ_ARM},{209,EQ_ARM},{241,EQ_ARM},
};

int equip_class(int obj_n)
{
    if (rules_game() == RULES_SE){
        for (unsigned i = 0; i < sizeof SE_EQTAB / sizeof SE_EQTAB[0]; i++)
            if (SE_EQTAB[i].obj_n == (uint16_t)obj_n) return SE_EQTAB[i].cls;
        return -1; }
    for (unsigned i = 0; i < sizeof EQTAB / sizeof EQTAB[0]; i++)
        if (EQTAB[i].obj_n == (uint16_t)obj_n) return EQTAB[i].cls;
    return -1;
}

/* is the item occupying ARM two-handed? (then ARM2 is blocked) */
static int arm_is_twohanded(const uint16_t set[EQ_NSLOTS])
{
    return set[EQ_ARM] && equip_class(set[EQ_ARM]) == EQ_ARM2;
}

int equip_ready(uint16_t set[EQ_NSLOTS], int obj_n)
{
    int cls = equip_class(obj_n);
    if (cls < 0) return EQUIP_ENOTREADIABLE;

    int slot = cls;
    switch (cls) {
    case EQ_ARM:                                   /* one-handed: main hand, else off hand */
        if (set[EQ_ARM]) {
            if (arm_is_twohanded(set)) return EQUIP_EFULL;
            slot = EQ_ARM2;
        }
        break;
    case EQ_CLS_OFFHAND:                           /* shields, main gauche: off hand, else main hand (§9) */
        if (arm_is_twohanded(set)) return EQUIP_EFULL;
        slot = set[EQ_ARM2] ? EQ_ARM : EQ_ARM2;
        break;
    case EQ_ARM2:                                  /* two-handed: needs BOTH arms free */
        if (set[EQ_ARM] || set[EQ_ARM2]) return EQUIP_EFULL;
        slot = EQ_ARM;                             /* occupies ARM; class marks it two-handed */
        break;
    case EQ_HAND:                                  /* rings: two fingers */
        if (set[EQ_HAND]) slot = EQ_HAND2;
        break;
    default: break;                                /* HEAD/NECK/BODY/FOOT: exactly that slot */
    }
    if (set[slot]) return EQUIP_EFULL;
    set[slot] = (uint16_t)obj_n;
    return slot;
}

int equip_unready(uint16_t set[EQ_NSLOTS], int slot)
{
    if (slot < 0 || slot >= EQ_NSLOTS || !set[slot]) return 0;
    int obj_n = set[slot];
    set[slot] = 0;
    return obj_n;
}

void equip_fold(const uint16_t set[EQ_NSLOTS], uint16_t *weapon, uint8_t *ac)
{
    int best = 0, bestdmg = 0, acsum = 0;
    for (int s = 0; s < EQ_NSLOTS; s++) {
        if (!set[s]) continue;
        int d = weapon_dmg(set[s]);
        if (d > bestdmg) { bestdmg = d; best = set[s]; }
        acsum += armour_ac(set[s]);
    }
    if (weapon) *weapon = (uint16_t)best;
    if (ac)     *ac = (uint8_t)(acsum > 255 ? 255 : acsum);
}
