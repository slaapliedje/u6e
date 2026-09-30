// SPDX-License-Identifier: GPL-2.0-or-later
/* monsters.h — creature base stats (docs/rules/eggs.md §4) + the stat roll (§3).
 * Portable engine code: no TOS, no render. */
#ifndef U6E_MONSTERS_H
#define U6E_MONSTERS_H

#include <stdint.h>
#include "rng.h"

enum { ALIGN_DEFAULT = 0, ALIGN_NEUTRAL = 1, ALIGN_EVIL = 2, ALIGN_GOOD = 3, ALIGN_CHAOTIC = 4 };

typedef struct { uint8_t str, dex, intel, hp, dmg, align; } mon_base_t;

/* Base stats of creature type obj_n. Returns 1 if the type is in the table, else 0 and the
 * original's fallback (10/10/10, 30 hp, dmg 4, default alignment). */
int monster_base(int obj_n, mon_base_t *out);

/* The hatch roll of one stat: t = base/2; base when t == 0, else base - t + rand(0..t) + rand(0..t). */
int monster_roll_stat(int base, rng_t *r);

/* 1 if this alignment fights the party (evil / chaotic). */
static inline int align_is_hostile(int align){ return align == ALIGN_EVIL || align == ALIGN_CHAOTIC; }

#endif
