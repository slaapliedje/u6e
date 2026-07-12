/* magic.c — U6 spell table + cast gate-check. See magic.h + docs/rules/magic_skills.md.
 * Clean-room: table built from public U6 magic references (understood, restructured
 * here); reagent obj_n confirmed from the user's own LOOK.LZD data (tools/magicprobe). */
#include "magic.h"
#include <string.h>

/* The 79 U6 spells, dense circle-major order (magic_skills.md §5). Reagent masks
 * use the R_* bits from magic.h. */
const spell_t g_spells[SPELL_COUNT] = {
    /* --- Circle 1 (cost 1) --- */
    { 1, R_GA|R_GI|R_MA,           "Create Food",  "IN MANI YLEM" },
    { 1, R_NI|R_SA,                "Detect Magic", "WIS ORT" },
    { 1, R_NI|R_SA,                "Detect Trap",  "WIS JUX" },
    { 1, R_GA|R_GI,                "Dispel Magic", "AN JUX ORT" },
    { 1, R_GA|R_BP,                "Douse",        "AN FLAM" },
    { 1, R_NI|R_SS,                "Harm",         "AN MANI" },
    { 1, R_GI|R_SS,                "Heal",         "IN MANI" },
    { 1, 0,                        "Help",         "KAL LOR" },
    { 1, R_SA|R_BP,                "Ignite",       "IN FLAM" },
    { 1, R_SA,                     "Light",        "IN LOR" },
    /* --- Circle 2 (cost 2) --- */
    { 2, R_NI|R_SA,                "Infravision",  "QUAS LOR" },
    { 2, R_SA|R_BP,                "Magic Arrow",  "ORT JUX" },
    { 2, R_NI|R_BM|R_BP,           "Poison",       "IN NOX POR" },
    { 2, R_SS|R_BM|R_BP,           "Reappear",     "IN YLEM" },
    { 2, R_NI|R_SS|R_BP,           "Sleep",        "IN ZU" },
    { 2, R_BM|R_MA|R_BP,           "Telekinesis",  "ORT POR YLEM" },
    { 2, R_SS|R_NI,                "Trap",         "IN JUX" },
    { 2, R_SA|R_BM,                "Unlock Magic", "EX POR" },
    { 2, R_SA|R_BM,                "Untrap",       "AN JUX" },
    { 2, R_GA|R_BM|R_BP,           "Vanish",       "AN YLEM" },
    /* --- Circle 3 (cost 3) --- */
    { 3, R_SA|R_NI|R_GA,           "Curse",        "AN SANCT" },
    { 3, R_BP|R_SA,                "Dispel Field", "AN GRAV" },
    { 3, R_SA|R_BP,                "Fireball",     "POR FLA" },
    { 3, R_SA|R_MA,                "Great Light",  "VAS LOR" },
    { 3, R_SA|R_BM|R_GA,           "Magic Lock",   "AN POR" },
    { 3, R_GI|R_GA,                "Mass Awaken",  "AN VAS ZU" },
    { 3, R_GI|R_NI|R_SS,           "Mass Sleep",   "VAS ZU" },
    { 3, R_NI|R_MA,                "Peer",         "VAS WIS YLEM" },
    { 3, R_SA|R_GI|R_GA,           "Protection",   "IN SANCT" },
    { 3, R_GA|R_SA,                "Repel Undead", "AN XEN CORP" },
    /* --- Circle 4 (cost 4) --- */
    { 4, R_SA|R_MA|R_BM,           "Animate",      "ORT YLEM" },
    { 4, R_SS|R_MA,                "Conjure",      "KAI XEN" },
    { 4, R_NI|R_SS|R_MA,           "Disable",      "AN VAN MANI" },
    { 4, R_BP|R_SA|R_SS,           "Fire Field",   "IN FLAM GRAV" },
    { 4, R_GI|R_SS|R_MA,           "Great Heal",   "VAS MANI" },
    { 4, R_NI,                     "Locate",       "IN WIS" },
    { 4, R_GA|R_GI,                "Mass Dispel",  "VAS AN JUX ORT" },
    { 4, R_NI|R_SS|R_BP,           "Poison Field", "IN NOX GRAV" },
    { 4, R_GI|R_SS|R_BP,           "Sleep Field",  "IN ZU GRAV" },
    { 4, R_SA|R_BM,                "Wild Change",  "REL HUR" },
    /* --- Circle 5 (cost 5) --- */
    { 5, R_MA|R_SS|R_BP,           "Energy Field", "IN SANCT GRAV" },
    { 5, R_MA|R_SA|R_BP,           "Explosion",    "VAS POL FLAM" },
    { 5, R_BM|R_SS|R_SA,           "Insect Swarm", "KAL BET XEN" },
    { 5, R_NI|R_BM,                "Invisibility", "SANCT LOR" },
    { 5, R_BP|R_MA|R_SA,           "Lightning",    "ORT GRAV" },
    { 5, R_SS|R_SA|R_NI|R_BP,      "Paralyze",     "AN XEN POR" },
    { 5, R_BM|R_SS|R_NI,           "Pickpocket",   "POR YLEM" },
    { 5, R_SS|R_NI|R_MA,           "Reveal",       "AN SANCT LOR" },
    { 5, R_BM|R_MA|R_NI|R_SS|R_SA, "Seance",       "KAL MANI CORP" },
    { 5, R_MA|R_SA,                "X-ray",        "WIS YLEM" },
    /* --- Circle 6 (cost 6) --- */
    { 6, R_BP|R_NI|R_SS,           "Charm",        "AN XEN EX" },
    { 6, R_SA|R_SS|R_BM|R_GI|R_NI|R_MA, "Clone",   "IN QUAS XEN" },
    { 6, R_MA|R_NI,                "Confuse",      "VAS QUAS" },
    { 6, R_SA|R_BM|R_MA,           "Flame Wind",   "FLAM HUR" },
    { 6, R_BM|R_BP|R_MA,           "Hail Storm",   "KAL DES YLEM" },
    { 6, R_SA|R_GI|R_GA|R_MA,      "Mass Protect", "VAS IN SANCT" },
    { 6, R_GA|R_MA|R_SA,           "Negate Magic", "AN ORT" },
    { 6, R_NI|R_SA|R_BM,           "Poison Wind",  "NOX HUR" },
    { 6, R_SA|R_SS|R_BM|R_GI|R_NI, "Replicate",    "IN QUAS YLEM" },
    { 6, R_SS,                     "Web",          "IN DES POR" },
    /* --- Circle 7 (cost 7) --- */
    { 7, R_BP|R_MA|R_SA|R_BM,      "Chain Bolt",   "VAS ORT GRAV" },
    { 7, R_SS|R_MA|R_SA,           "Enchant",      "IN ORT YLEM" },
    { 7, R_MA|R_NI|R_SA|R_BM,      "Energy Wind",  "GRAV HUR" },
    { 7, R_NI|R_MA|R_GA,           "Fear",         "QUAS CORP" },
    { 7, R_SA|R_BP|R_MA,           "Gate Travel",  "VAS REL POR" },
    { 7, R_BP|R_NI|R_SA,           "Kill",         "IN CORP" },
    { 7, R_SA|R_NI|R_GA|R_MA,      "Mass Curse",   "VAS AN SANCT" },
    { 7, R_MA|R_NI|R_BM|R_BP,      "Mass Invisibility", "VAS SANCT LOR" },
    { 7, R_BM|R_SS|R_MA|R_SA,      "Wing Strike",  "KAL ORT XEN" },
    { 7, R_BM|R_NI|R_MA|R_SA|R_BP|R_SS, "Wizard Eye", "POR ORT WIS" },
    /* --- Circle 8 (cost 8) --- */
    { 8, R_MA|R_NI|R_SA|R_BM,      "Death Wind",   "CORP HUR" },
    { 8, R_MA|R_SA|R_NI|R_GA|R_BM, "Eclipse",      "VAS AN LOR" },
    { 8, R_BP|R_NI|R_SS|R_MA,      "Mass Charm",   "VAS AN XEN EX" },
    { 8, R_BP|R_NI|R_MA|R_SA,      "Mass Kill",    "VAS CORP" },
    { 8, R_GA|R_GI|R_SS|R_SA|R_BM|R_MA, "Resurrect", "IN MANI CORP" },
    { 8, R_BM|R_NI|R_MA,           "Slime",        "VAS REL XEN" },
    { 8, R_MA|R_GA|R_BM|R_SS,      "Summon",       "KAL XEN CORP" },
    { 8, R_MA|R_GA|R_BM,           "Time Stop",    "AN TYM" },
    { 8, R_BM|R_SA|R_MA,           "Tremor",       "VAS POR YLEM" }
};

int magic_mana_cost(int spell)
{
    if (spell < 0 || spell >= SPELL_COUNT) return 0;
    return g_spells[spell].circle;
}

int magic_castable_circle(int intelligence)
{
    int c = intelligence / 4;         /* magic_skills.md §3 first cut */
    if (c < 1) c = 1;                 /* everyone can attempt circle 1 */
    if (c > 8) c = 8;
    return c;
}

int magic_max_mana(int intelligence, int is_avatar)
{
    if (intelligence < 0) intelligence = 0;
    return is_avatar ? intelligence * 2 : intelligence / 2;   /* magic_skills.md §3 */
}

int magic_reagent_objn(int bit_index)
{
    if (bit_index < 0 || bit_index >= REAG_COUNT) return 0;
    return REAG_FIRST_OBJ + bit_index;
}

int magic_find(const char *name)
{
    for (int i = 0; i < SPELL_COUNT; i++)
        if (strcmp(g_spells[i].name, name) == 0) return i;
    return -1;
}

int magic_spell_num(int spell)
{
    if (spell < 0 || spell >= SPELL_COUNT) return -1;
    /* dense index -> authentic (circle-1)*16 + index_in_circle; count spells of
     * earlier circles to get the within-circle index. */
    int circle = g_spells[spell].circle;
    int idx = 0;
    for (int i = 0; i < spell; i++)
        if (g_spells[i].circle == circle) idx++;
    return (circle - 1) * 16 + idx;
}

cast_result_t magic_check(int spell, int known, int intelligence,
                          uint8_t have_reagents, int current_mana)
{
    if (spell < 0 || spell >= SPELL_COUNT) return CAST_BADSPELL;
    const spell_t *sp = &g_spells[spell];
    if (!known)                                   return CAST_UNKNOWN;
    if (sp->circle > magic_castable_circle(intelligence)) return CAST_LOWLEVEL;
    if ((have_reagents & sp->reagents) != sp->reagents)   return CAST_NOREAGENTS;
    if (current_mana < sp->circle)                return CAST_NOMANA;
    return CAST_OK;
}
