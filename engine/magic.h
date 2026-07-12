// SPDX-License-Identifier: GPL-2.0-or-later
/* magic.h — U6 spellcasting rules (docs/rules/magic_skills.md).
 *
 * Portable engine logic: the spell TABLE (circle, reagents, name, words) and the
 * cast GATE-CHECK. Pure + deterministic so it unit-tests; the UI (render/gamedemo)
 * builds the inventory reagent mask + known-spell state and owns per-spell world
 * effects. No TOS, no render, no file I/O.
 *
 * Spell index here is DENSE 0..SPELL_COUNT-1 in circle-major order (matches the
 * §5 table). The authentic U6 number (circle-1)*16+idx is recoverable via
 * magic_spell_num() for save/spellbook interop.
 */
#ifndef U6E_MAGIC_H
#define U6E_MAGIC_H

#include <stdint.h>

#define SPELL_COUNT      79       /* circles 1-7 x10 + circle 8 x9 (magic_skills.md §5) */
#define REAG_FIRST_OBJ   65       /* black pearl; reagents are obj_n 65..72 (§2)        */
#define REAG_COUNT       8

/* Reagent bitmask (bit i => obj_n REAG_FIRST_OBJ+i). Order per magic_skills.md §2. */
enum {
    R_BP = 0x01,  /* black pearl    (obj 65) */
    R_BM = 0x02,  /* blood moss     (obj 66) */
    R_GA = 0x04,  /* garlic         (obj 67) */
    R_GI = 0x08,  /* ginseng        (obj 68) */
    R_MA = 0x10,  /* mandrake root  (obj 69) */
    R_NI = 0x20,  /* nightshade     (obj 70) */
    R_SS = 0x40,  /* spider silk    (obj 71) */
    R_SA = 0x80   /* sulfurous ash  (obj 72) */
};

typedef struct {
    uint8_t     circle;    /* 1..8 == mana cost (§3) */
    uint8_t     reagents;  /* R_* bitmask (§2)       */
    const char *name;      /* "Heal"                 */
    const char *words;     /* "IN MANI"              */
} spell_t;

extern const spell_t g_spells[SPELL_COUNT];

/* Why a cast is refused (checked in this order; magic_skills.md §6). */
typedef enum {
    CAST_OK = 0,
    CAST_BADSPELL,      /* spell index out of range          */
    CAST_UNKNOWN,       /* not in the caster's spellbook      */
    CAST_LOWLEVEL,      /* circle > castable_circle(int)      */
    CAST_NOREAGENTS,    /* a required reagent is missing       */
    CAST_NOMANA         /* current_mana < circle              */
} cast_result_t;

/* circle == mana cost; 0 if spell is out of range. */
int  magic_mana_cost(int spell);

/* Highest circle castable at this Intelligence (magic_skills.md §3, first cut). */
int  magic_castable_circle(int intelligence);

/* Maximum magic points from Intelligence (magic_skills.md §3): the Avatar gets
 * 2x Intelligence, a magic-using companion gets Intelligence/2. is_avatar!=0
 * selects the Avatar formula. */
int  magic_max_mana(int intelligence, int is_avatar);

/* obj_n (65..72) for reagent bit index 0..7, or 0 if out of range. */
int  magic_reagent_objn(int bit_index);

/* Dense spell index for a spell name (exact match), or -1 if not found. */
int  magic_find(const char *name);

/* Authentic U6 spell number (circle-1)*16 + index_in_circle, for save interop. */
int  magic_spell_num(int spell);

/* The full gate check (pure). `known` = 1 if the spell is in the caster's book;
 * `have_reagents` = bitmask of reagents the caster HAS at least one of (bit i =>
 * obj_n 65+i present). Returns CAST_OK or the first failing precondition. */
cast_result_t magic_check(int spell, int known, int intelligence,
                          uint8_t have_reagents, int current_mana);

#endif
