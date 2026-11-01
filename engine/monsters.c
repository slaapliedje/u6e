// SPDX-License-Identifier: GPL-2.0-or-later
/* monsters.c — creature base stats + hatch stat roll. Implemented from docs/rules/eggs.md §3-§4. */
#include "monsters.h"

typedef struct { uint16_t obj_n; mon_base_t b; } mon_row_t;

static const mon_row_t MON[] = {
    { 98, { 22,  9,  8,  30,  15, ALIGN_CHAOTIC, 5 } },   /* mimic (chest) */
    { 342, { 5, 13,  4,   8,   6, ALIGN_CHAOTIC, 0 } },   /* giant rat */
    { 343, { 1, 40,  1,   1,   1, ALIGN_CHAOTIC, 0 } },   /* insects */
    { 344, { 5, 25,  4,   5,   6, ALIGN_CHAOTIC, 0 } },   /* giant bat */
    { 345, { 24, 20,  8,  50,  20, ALIGN_EVIL, 0 } },   /* giant squid */
    { 346, { 17, 17,  9,  70,  30, ALIGN_EVIL, 2 } },   /* sea serpent */
    { 347, { 21, 20, 16,  30,  20, ALIGN_CHAOTIC, 4 } },   /* reaper */
    { 348, { 5, 10,  4,   6,   2, ALIGN_NEUTRAL, 0 } },   /* sheep */
    { 349, { 5, 16,  8,   6,   3, ALIGN_NEUTRAL, 0 } },   /* dog */
    { 350, { 11, 18,  5,   8,   2, ALIGN_NEUTRAL, 0 } },   /* deer */
    { 351, { 8, 16,  8,   8,   4, ALIGN_CHAOTIC, 0 } },   /* wolf */
    { 352, { 1, 10,  9,  20,  12, ALIGN_EVIL, 0 } },   /* ghost */
    { 353, { 2, 26,  7,   5,   1, ALIGN_CHAOTIC, 0 } },   /* gremlin */
    { 354, { 2, 25,  3,   5,   2, ALIGN_NEUTRAL, 0 } },   /* mouse */
    { 355, { 2, 10, 21,  20,   1, ALIGN_EVIL, 0 } },   /* gazer */
    { 356, { 1, 28,  4,   3,   1, ALIGN_NEUTRAL, 0 } },   /* bird */
    { 357, { 17,  6,  3,  40,  15, ALIGN_CHAOTIC, 0 } },   /* corpser */
    { 358, { 3,  7,  6,   5,   2, ALIGN_CHAOTIC, 1 } },   /* snake */
    { 359, { 3,  9,  3,   5,   2, ALIGN_NEUTRAL, 0 } },   /* rabbit */
    { 360, { 3, 17,  2,   5,   2, ALIGN_CHAOTIC, 0 } },   /* rot worm */
    { 361, { 9, 11,  4,  10,   8, ALIGN_CHAOTIC, 0 } },   /* giant spider */
    { 362, { 15, 32, 33,  50,   6, ALIGN_EVIL, 5 } },   /* winged gargoyle */
    { 363, { 25, 19,  8,  30,  15, ALIGN_EVIL, 10 } },   /* gargoyle */
    { 364, { 5,  5,  2,  10,   1, ALIGN_CHAOTIC, 0 } },   /* acid slug */
    { 365, { 20,  5,  1,  30,   1, ALIGN_CHAOTIC, 4 } },   /* tangle-vine flower */
    { 366, { 20, 25,  1,  30,   1, ALIGN_CHAOTIC, 4 } },   /* tangle vine */
    { 367, { 35, 26, 31, 100,  20, ALIGN_EVIL, 10 } },   /* daemon */
    { 368, { 10, 14,  7,  20,   6, ALIGN_EVIL, 0 } },   /* skeleton */
    { 369, { 22, 22, 13,  50,  10, ALIGN_EVIL, 4 } },   /* drake */
    { 370, { 19, 13,  8,  20,  12, ALIGN_EVIL, 2 } },   /* headless */
    { 371, { 18, 14,  9,  15,   6, ALIGN_EVIL, 4 } },   /* troll */
    { 372, { 20, 27, 13,  30,  20, ALIGN_EVIL, 4 } },   /* mongbat */
    { 373, { 1, 40, 20,  40,   1, ALIGN_CHAOTIC, 0 } },   /* wisp */
    { 374, { 26, 11,  2,  50,  15, ALIGN_CHAOTIC, 0 } },   /* hydra */
    { 375, { 6,  6,  2,  15,   4, ALIGN_CHAOTIC, 0 } },   /* slime */
    { 376, { 20, 17, 11,  20,   6, ALIGN_CHAOTIC, 0 } },   /* fighter */
    { 377, { 15, 18, 12,  15,   4, ALIGN_CHAOTIC, 0 } },   /* swashbuckler */
    { 378, { 10, 14, 22,  30,   4, ALIGN_EVIL, 0 } },   /* mage */
    { 379, { 11, 12, 12,   8,   4, ALIGN_CHAOTIC, 0 } },   /* villager */
    { 380, { 12, 12, 18,   8,   4, ALIGN_EVIL, 0 } },   /* merchant */
    { 381, { 8,  8,  9,   6,   2, ALIGN_CHAOTIC, 0 } },   /* child */
    { 382, { 9, 30,  9, 100,   8, ALIGN_EVIL, 0 } },   /* guard */
    { 383, { 14, 16, 12,   8,   4, ALIGN_CHAOTIC, 0 } },   /* jester */
    { 384, { 8,  8,  9,   6,   3, ALIGN_CHAOTIC, 0 } },   /* peasant */
    { 385, { 15, 15, 10,   8,   4, ALIGN_NEUTRAL, 0 } },   /* farmer */
    { 386, { 12, 16, 14,   8,   4, ALIGN_CHAOTIC, 0 } },   /* musician */
    { 387, { 10, 14, 15,   8,   4, ALIGN_NEUTRAL, 0 } },   /* woman */
    { 388, { 4, 18,  8,   6,   2, ALIGN_NEUTRAL, 0 } },   /* cat */
    { 409, { 30, 30, 30, 255, 255, ALIGN_GOOD, 30 } },   /* Lord British */
    { 410, { 25, 25, 25,  30,   4, ALIGN_GOOD, 0 } },   /* Avatar */
    { 411, { 40, 35, 27, 150,  30, ALIGN_EVIL, 12 } },   /* dragon */
    { 413, { 60, 21,  8, 200,  60, ALIGN_CHAOTIC, 15 } },   /* silver serpent */
    { 424, { 27,  8,  7,  40,  20, ALIGN_EVIL, 4 } },   /* cyclops */
    { 426, { 13, 13,  3,  20,   8, ALIGN_CHAOTIC, 3 } },   /* giant scorpion */
    { 427, { 16, 15,  2,  10,   8, ALIGN_CHAOTIC, 3 } },   /* giant ant */
    { 428, { 14,  7,  5,  10,   3, ALIGN_NEUTRAL, 0 } },   /* cow */
    { 429, { 20, 10,  3,  30,  10, ALIGN_CHAOTIC, 6 } },   /* alligator */
    { 430, { 22, 27,  7,  15,   3, ALIGN_NEUTRAL, 0 } },   /* horse */
    { 431, { 22, 27,  7,  15,   3, ALIGN_NEUTRAL, 0 } },   /* horse with rider */
};

int monster_base(int obj_n, mon_base_t *out)
{
    for (unsigned i = 0; i < sizeof MON / sizeof MON[0]; i++)
        if (MON[i].obj_n == obj_n){ *out = MON[i].b; return 1; }
    out->str = 10; out->dex = 10; out->intel = 10; out->hp = 30; out->dmg = 4; out->align = ALIGN_DEFAULT; out->xp = 0;
    return 0;
}

int monster_roll_stat(int base, rng_t *r)
{
    int t = base / 2;
    if (t == 0) return base;
    return base - t + rng_range(r, t + 1) + rng_range(r, t + 1);
}

/* Movement types (combat.md §7c; the reference's actor type table): low air = insects, giant bat, bird,
 * drake, wisp, dragon, the inflated balloon; high water = giant squid, sea serpent, a ship; low water =
 * skiff, raft; a tangle vine pod never moves; everything else walks. */
int monster_movetype(int obj_n)
{
    switch (obj_n){
    case 343: case 344: case 356: case 369: case 373: case 411: case 423: return MT_AIR_LOW;
    case 345: case 346: case 412: return MT_WATER_HIGH;
    case 414: case 415: return MT_WATER_LOW;
    case 365: return MT_NONE;
    default: return MT_LAND;
    }
}

int monster_remains(int obj_n, int *frame)
{
    int fr = 0, body = 0;
    switch (obj_n){
    /* blood (338), a random frame rolled by the caller */
    case 344: case 353: case 355: case 356: case 357: case 359: case 360: case 374: case 342: case 348:
    case 358: case 361: case 426: case 427: case 428: case 429: case 431: body = 338; break;
    /* the creature's own object in the frame it died */
    case 347: case 354: case 388: case 366: case 349: case 350: case 351: case 369: case 372: body = obj_n; fr = 255; break;
    /* dead body (339) by type */
    case 367: case 371: case 363: body = 339; fr = 0; break;          /* daemon, troll, gargoyle */
    case 368: body = 339; fr = 9; break;                                /* skeleton */
    case 370: body = 339; fr = 1; break;                                /* headless */
    case 376: body = 339; fr = 6; break;                                /* fighter */
    case 377: case 384: body = 339; fr = 5; break;                      /* swashbuckler, peasant */
    case 378: case 387: body = 339; fr = 3; break;                      /* mage, woman */
    case 379: case 380: case 409: body = 339; fr = 2; break;            /* villager, merchant, Lord British */
    case 381: case 383: case 385: body = 339; fr = 8; break;            /* child, jester, farmer */
    case 382: body = 339; fr = 4; break;                                /* guard */
    case 386: case 410: body = 339; fr = 7; break;                      /* musician, the Avatar */
    case 424: body = 340; fr = 3; break;                                /* cyclops */
    case 362: body = 341; fr = 3; break;                                /* winged gargoyle */
    case 430: body = 134; fr = 1; break;                                /* horse -> carcass */
    default: body = 0; break;                                           /* insects, squid, ghost, slug, wisp, serpents, dragon, vehicles ... */
    }
    if (frame) *frame = fr;
    return body;
}

/* Immunities by creature type (magic_skills.md §5b): poison-immune, undead (death spells), sleep-immune,
 * flies (tremor-immune), repels undead, double fire damage, fire-immune. Types not listed have none. */
static const struct { uint16_t obj_n; uint8_t f; } MF[] = {
    { 364, MF_DOUBLE_FIRE|MF_SLEEP_IMMUNE },                                  /* acid slug */
    { 429, MF_POISON_IMMUNE },                                                /* alligator */
    { 427, MF_SLEEP_IMMUNE },                                                 /* giant ant */
    { 344, MF_FLIES }, { 356, MF_FLIES }, { 372, MF_FLIES }, { 423, MF_FLIES },   /* bat, bird, mongbat, balloon */
    { 357, MF_DOUBLE_FIRE|MF_SLEEP_IMMUNE },                                  /* corpser */
    { 367, MF_FLIES|MF_REPEL_UNDEAD|MF_FIRE_IMMUNE|MF_UNDEAD|MF_SLEEP_IMMUNE },   /* daemon */
    { 411, MF_FLIES|MF_FIRE_IMMUNE }, { 369, MF_FLIES|MF_FIRE_IMMUNE },      /* dragon, drake */
    { 362, MF_FLIES|MF_SLEEP_IMMUNE }, { 355, MF_FLIES|MF_SLEEP_IMMUNE },    /* winged gargoyle, gazer */
    { 352, MF_REPEL_UNDEAD|MF_FIRE_IMMUNE|MF_POISON_IMMUNE|MF_UNDEAD|MF_SLEEP_IMMUNE },   /* ghost */
    { 368, MF_REPEL_UNDEAD|MF_FIRE_IMMUNE|MF_POISON_IMMUNE|MF_UNDEAD|MF_SLEEP_IMMUNE },   /* skeleton */
    { 374, MF_FIRE_IMMUNE|MF_SLEEP_IMMUNE },                                  /* hydra */
    { 343, MF_FLIES|MF_SLEEP_IMMUNE },                                        /* insects */
    {  98, MF_POISON_IMMUNE|MF_SLEEP_IMMUNE },                                /* mimic */
    { 342, MF_POISON_IMMUNE },                                                /* giant rat */
    { 347, MF_DOUBLE_FIRE|MF_SLEEP_IMMUNE },                                  /* reaper */
    { 360, MF_DOUBLE_FIRE|MF_POISON_IMMUNE|MF_SLEEP_IMMUNE },                 /* rot worm */
    { 426, MF_SLEEP_IMMUNE }, { 345, MF_SLEEP_IMMUNE },                       /* giant scorpion, giant squid */
    { 375, MF_DOUBLE_FIRE|MF_SLEEP_IMMUNE },                                  /* slime */
    { 366, MF_DOUBLE_FIRE|MF_SLEEP_IMMUNE }, { 365, MF_DOUBLE_FIRE|MF_SLEEP_IMMUNE },   /* tangle vine, its flower */
    { 373, MF_FIRE_IMMUNE|MF_UNDEAD|MF_SLEEP_IMMUNE },                        /* wisp */
};
/* creature types whose own blow rolls to hit on strength, not dexterity (combat.md §2) */
static const uint16_t STRB[] = { 98, 347, 355, 362, 363, 365, 366, 367, 368, 370, 371, 373, 374, 376, 377, 378, 379,
    380, 381, 382, 383, 384, 385, 386, 387, 409, 410, 424, 427, 428, 429, 430, 431 };
int monster_str_based(int obj_n)
{
    for (unsigned i = 0; i < sizeof STRB / sizeof STRB[0]; i++) if (STRB[i] == obj_n) return 1;
    return 0;
}

int monster_flags(int obj_n)
{
    for (unsigned i = 0; i < sizeof MF / sizeof MF[0]; i++) if (MF[i].obj_n == obj_n) return MF[i].f;
    return 0;
}

/* innate spells per creature type (combat.md §7b; the reference's creature table's spell column, read
 * for understanding): U6 spell numbers (circle-1)*16+idx. 128 = the monsters' own "become invisible",
 * 129 = the wisp's teleport. A hatched creature carries one charge per entry and never uses it up. */
typedef struct { uint16_t obj_n; uint8_t n; uint8_t sp[8]; } mon_spells_t;
static const mon_spells_t MSP[] = {
    { 367, 4, { 17, 128, 34, 80 } },                 /* daemon: magic arrow, invisible, fireball, charm */
    { 411, 5, { 34, 83, 68, 96, 119 } },             /* dragon: fireball, flame wind, lightning, chain bolt, summon */
    { 369, 2, { 34, 68 } },                          /* drake: fireball, lightning */
    { 362, 3, { 68, 65, 69 } },                      /* winged gargoyle: lightning, explosion, paralyze */
    { 355, 3, { 20, 68, 80 } },                      /* gazer: sleep, lightning, charm */
    { 352, 2, { 128, 99 } },                         /* ghost: invisible, fear */
    { 374, 4, { 69, 96, 98, 68 } },                  /* hydra: paralyze, chain bolt, energy wind, lightning */
    { 378, 8, { 17, 34, 20, 49, 68, 80, 69, 83 } },  /* mage: magic arrow, fireball, sleep, conjure, lightning, charm, paralyze, flame wind */
    {  98, 2, { 18, 80 } },                          /* mimic: poison, charm */
    { 347, 4, { 17, 80, 66, 68 } },                  /* reaper: magic arrow, charm, insect swarm, lightning */
    { 346, 3, { 34, 83, 51 } },                      /* sea serpent: fireball, flame wind, fire field */
    { 361, 1, { 89 } },                              /* giant spider: web */
    { 345, 2, { 17, 68 } },                          /* giant squid: magic arrow, lightning */
    { 373, 3, { 20, 69, 129 } },                     /* wisp: sleep, paralyze, teleport */
    { 365, 1, { 20 } },                              /* tangle-vine flower: sleep */
};
int monster_spells(int obj_n, const uint8_t **out)
{
    for (unsigned i = 0; i < sizeof MSP / sizeof MSP[0]; i++)
        if (MSP[i].obj_n == obj_n){ if (out) *out = MSP[i].sp; return MSP[i].n; }
    if (out) *out = 0;
    return 0;
}

/* a hatchling's kit per creature type (eggs.md §3c; the reference creature table's weapon, armour and
 * treasure columns, read for understanding). Each list is rolled 1 in 2, 1 in 4, ... in order. */
typedef struct { uint16_t obj_n; uint8_t wp[5], ar[4]; uint8_t tr[3]; } mon_kit_t;
static const mon_kit_t MKIT[] = {
    {  98, {0},                  {0},              { 98, 98 } },
    { 347, {0},                  {0},              { 58, 88 } },
    { 348, {0},                  {0},              { 129 } },
    { 350, {0},                  {0},              { 129 } },
    { 361, {0},                  {0},              { 129 } },
    { 428, {0},                  {0},              { 129 } },
    { 362, { 46, 41, 47, 49 },   { 18, 1 },        { 58 } },
    { 363, { 34, 49, 37 },       { 1, 9, 18, 20 }, {0} },
    { 368, { 43, 36, 37, 41, 38 },{ 9, 1 },        { 88 } },
    { 369, {0},                  {0},              { 88, 88 } },
    { 370, { 34, 36 },           { 17, 9 },        { 88 } },
    { 371, { 37, 36, 43, 41, 34 },{ 9, 18, 1, 19 },{ 98, 88, 88 } },
    { 372, { 38, 43, 36, 37 },   { 9, 17, 18 },    { 98, 88 } },
    { 376, { 43, 42 },           { 21, 10, 3, 22 },{ 88, 88 } },
    { 377, { 43, 41, 33 },       { 19, 1, 9 },     { 88, 88 } },
    { 378, { 38 },               { 17, 1 },        { 58, 98, 88 } },
    { 379, { 43, 33 },           { 17 },           { 88 } },
    { 380, { 38, 43 },           { 17, 1 },        { 88 } },
    { 382, { 42, 47 },           { 22, 10, 3 },    { 88 } },
    { 383, { 38, 33 },           { 17 },           { 88 } },
    { 384, { 38, 34 },           { 185 },          {0} },
    { 385, { 105, 101 },         { 17 },           { 88 } },
    { 386, { 41, 33, 43 },       { 19, 1, 9 },     { 88, 158 } },
    { 387, { 38 },               { 185 },          { 88 } },
    { 411, {0},                  {0},              { 98, 88, 88 } },
    { 424, { 44, 33, 34 },       { 20, 3, 13 },    { 98 } },
};
int monster_kit(int obj_n, int list, const uint8_t **out)
{
    for (unsigned i = 0; i < sizeof MKIT / sizeof MKIT[0]; i++) if (MKIT[i].obj_n == obj_n){
        const uint8_t *l = list == 0 ? MKIT[i].wp : list == 1 ? MKIT[i].ar : MKIT[i].tr;
        int cap = list == 0 ? 5 : list == 1 ? 4 : 3, n = 0;
        while (n < cap && l[n]) n++;
        if (out) *out = l;
        return n; }
    if (out) *out = 0;
    return 0;
}
