// SPDX-License-Identifier: GPL-2.0-or-later
/* monsters.h — creature base stats (docs/rules/eggs.md §4) + the stat roll (§3).
 * Portable engine code: no TOS, no render. */
#ifndef U6E_MONSTERS_H
#define U6E_MONSTERS_H

#include <stdint.h>
#include "rng.h"

enum { ALIGN_DEFAULT = 0, ALIGN_NEUTRAL = 1, ALIGN_EVIL = 2, ALIGN_GOOD = 3, ALIGN_CHAOTIC = 4 };

typedef struct { uint8_t str, dex, intel, hp, dmg, align, xp; } mon_base_t;   /* xp: the reference table's per-type experience term (combat.md §3) */

/* Base stats of creature type obj_n. Returns 1 if the type is in the table, else 0 and the
 * original's fallback (10/10/10, 30 hp, dmg 4, default alignment). */
int monster_base(int obj_n, mon_base_t *out);

/* The hatch roll of one stat: t = base/2; base when t == 0, else base - t + rand(0..t) + rand(0..t). */
int monster_roll_stat(int base, rng_t *r);

/* Per-type immunities (magic_skills.md §5b; the creature table's flag columns). 0 for humans. */
enum { MF_POISON_IMMUNE = 1, MF_UNDEAD = 2, MF_SLEEP_IMMUNE = 4, MF_FLIES = 8, MF_REPEL_UNDEAD = 16,
       MF_DOUBLE_FIRE = 32, MF_FIRE_IMMUNE = 64 };
int monster_flags(int obj_n);
int monster_str_based(int obj_n);   /* its own blow rolls to hit on strength (combat.md §2) */
/* movement types (combat.md §7c): what a creature type may step onto */
enum { MT_LAND = 0, MT_AIR_LOW = 1, MT_WATER_HIGH = 2, MT_WATER_LOW = 3, MT_NONE = 4 };
int monster_movetype(int obj_n);
/* What a slain creature leaves (the reference's actor-type dead_obj_n / dead_frame_n; combat.md §3c,
 * usecode_use.md "Created and destroyed objects persist"): the remains' obj_n, or 0 for nothing; *frame = its
 * frame, 255 = the creature's own frame as it died (the remains are then the creature's own object). */
int monster_remains(int obj_n, int *frame);
/* The creature type's innate SPELLS as U6 spell numbers (combat.md §7b): *out points at the list,
 * returns its length (0 = casts nothing). */
int monster_spells(int obj_n, const uint8_t **out);
int monster_kit(int obj_n, int list, const uint8_t **out);   /* a hatchling's kit (eggs.md §3c): list 0 weapons, 1 armour, 2 treasure */
/* 1 if this alignment fights the party (evil / chaotic). */
static inline int align_is_hostile(int align){ return align == ALIGN_EVIL || align == ALIGN_CHAOTIC; }

#endif
