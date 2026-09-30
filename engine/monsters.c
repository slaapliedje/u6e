// SPDX-License-Identifier: GPL-2.0-or-later
/* monsters.c — creature base stats + hatch stat roll. Implemented from docs/rules/eggs.md §3-§4. */
#include "monsters.h"

typedef struct { uint16_t obj_n; mon_base_t b; } mon_row_t;

static const mon_row_t MON[] = {
    { 98,  { 22,  9,  8,  30,  15, ALIGN_CHAOTIC } },   /* mimic (chest) */
    { 342, {  5, 13,  4,   8,   6, ALIGN_CHAOTIC } },   /* giant rat */
    { 343, {  1, 40,  1,   1,   1, ALIGN_CHAOTIC } },   /* insects */
    { 344, {  5, 25,  4,   5,   6, ALIGN_CHAOTIC } },   /* giant bat */
    { 345, { 24, 20,  8,  50,  20, ALIGN_EVIL    } },   /* giant squid */
    { 346, { 17, 17,  9,  70,  30, ALIGN_EVIL    } },   /* sea serpent */
    { 347, { 21, 20, 16,  30,  20, ALIGN_CHAOTIC } },   /* reaper */
    { 348, {  5, 10,  4,   6,   2, ALIGN_NEUTRAL } },   /* sheep */
    { 349, {  5, 16,  8,   6,   3, ALIGN_NEUTRAL } },   /* dog */
    { 350, { 11, 18,  5,   8,   2, ALIGN_NEUTRAL } },   /* deer */
    { 351, {  8, 16,  8,   8,   4, ALIGN_CHAOTIC } },   /* wolf */
    { 352, {  1, 10,  9,  20,  12, ALIGN_EVIL    } },   /* ghost */
    { 353, {  2, 26,  7,   5,   1, ALIGN_CHAOTIC } },   /* gremlin */
    { 354, {  2, 25,  3,   5,   2, ALIGN_NEUTRAL } },   /* mouse */
    { 355, {  2, 10, 21,  20,   1, ALIGN_EVIL    } },   /* gazer */
    { 356, {  1, 28,  4,   3,   1, ALIGN_NEUTRAL } },   /* bird */
    { 357, { 17,  6,  3,  40,  15, ALIGN_CHAOTIC } },   /* corpser */
    { 358, {  3,  7,  6,   5,   2, ALIGN_CHAOTIC } },   /* snake */
    { 359, {  3,  9,  3,   5,   2, ALIGN_NEUTRAL } },   /* rabbit */
    { 360, {  3, 17,  2,   5,   2, ALIGN_CHAOTIC } },   /* rot worm */
    { 361, {  9, 11,  4,  10,   8, ALIGN_CHAOTIC } },   /* giant spider */
    { 362, { 15, 32, 33,  50,   6, ALIGN_EVIL    } },   /* winged gargoyle */
    { 363, { 25, 19,  8,  30,  15, ALIGN_EVIL    } },   /* gargoyle */
    { 364, {  5,  5,  2,  10,   1, ALIGN_CHAOTIC } },   /* acid slug */
    { 365, { 20,  5,  1,  30,   1, ALIGN_CHAOTIC } },   /* tangle-vine flower */
    { 366, { 20, 25,  1,  30,   1, ALIGN_CHAOTIC } },   /* tangle vine */
    { 367, { 35, 26, 31, 100,  20, ALIGN_EVIL    } },   /* daemon */
    { 368, { 10, 14,  7,  20,   6, ALIGN_EVIL    } },   /* skeleton */
    { 369, { 22, 22, 13,  50,  10, ALIGN_EVIL    } },   /* drake */
    { 370, { 19, 13,  8,  20,  12, ALIGN_EVIL    } },   /* headless */
    { 371, { 18, 14,  9,  15,   6, ALIGN_EVIL    } },   /* troll */
    { 372, { 20, 27, 13,  30,  20, ALIGN_EVIL    } },   /* mongbat */
    { 373, {  1, 40, 20,  40,   1, ALIGN_CHAOTIC } },   /* wisp */
    { 374, { 26, 11,  2,  50,  15, ALIGN_CHAOTIC } },   /* hydra */
    { 375, {  6,  6,  2,  15,   4, ALIGN_CHAOTIC } },   /* slime */
    { 376, { 20, 17, 11,  20,   6, ALIGN_CHAOTIC } },   /* fighter */
    { 377, { 15, 18, 12,  15,   4, ALIGN_CHAOTIC } },   /* swashbuckler */
    { 378, { 10, 14, 22,  30,   4, ALIGN_EVIL    } },   /* mage */
    { 379, { 11, 12, 12,   8,   4, ALIGN_CHAOTIC } },   /* villager */
    { 380, { 12, 12, 18,   8,   4, ALIGN_EVIL    } },   /* merchant */
    { 381, {  8,  8,  9,   6,   2, ALIGN_CHAOTIC } },   /* child */
    { 382, {  9, 30,  9, 100,   8, ALIGN_EVIL    } },   /* guard */
    { 383, { 14, 16, 12,   8,   4, ALIGN_CHAOTIC } },   /* jester */
    { 384, {  8,  8,  9,   6,   3, ALIGN_CHAOTIC } },   /* peasant */
    { 385, { 15, 15, 10,   8,   4, ALIGN_NEUTRAL } },   /* farmer */
    { 386, { 12, 16, 14,   8,   4, ALIGN_CHAOTIC } },   /* musician */
    { 387, { 10, 14, 15,   8,   4, ALIGN_NEUTRAL } },   /* woman */
    { 388, {  4, 18,  8,   6,   2, ALIGN_NEUTRAL } },   /* cat */
    { 409, { 30, 30, 30, 255, 255, ALIGN_GOOD    } },   /* Lord British */
    { 410, { 25, 25, 25,  30,   4, ALIGN_GOOD    } },   /* Avatar */
    { 411, { 40, 35, 27, 150,  30, ALIGN_EVIL    } },   /* dragon */
    { 413, { 60, 21,  8, 200,  60, ALIGN_CHAOTIC } },   /* silver serpent */
    { 424, { 27,  8,  7,  40,  20, ALIGN_EVIL    } },   /* cyclops */
    { 426, { 13, 13,  3,  20,   8, ALIGN_CHAOTIC } },   /* giant scorpion */
    { 427, { 16, 15,  2,  10,   8, ALIGN_CHAOTIC } },   /* giant ant */
    { 428, { 14,  7,  5,  10,   3, ALIGN_NEUTRAL } },   /* cow */
    { 429, { 20, 10,  3,  30,  10, ALIGN_CHAOTIC } },   /* alligator */
    { 430, { 22, 27,  7,  15,   3, ALIGN_NEUTRAL } },   /* horse */
    { 431, { 22, 27,  7,  15,   3, ALIGN_NEUTRAL } },   /* horse with rider */
};

int monster_base(int obj_n, mon_base_t *out)
{
    for (unsigned i = 0; i < sizeof MON / sizeof MON[0]; i++)
        if (MON[i].obj_n == obj_n){ *out = MON[i].b; return 1; }
    out->str = 10; out->dex = 10; out->intel = 10; out->hp = 30; out->dmg = 4; out->align = ALIGN_DEFAULT;
    return 0;
}

int monster_roll_stat(int base, rng_t *r)
{
    int t = base / 2;
    if (t == 0) return base;
    return base - t + rng_range(r, t + 1) + rng_range(r, t + 1);
}
