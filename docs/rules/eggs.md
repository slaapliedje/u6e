# Eggs: monster and object spawning (U6)

Games: Ultima VI (SE/MD use the same egg object model — RuleSet deltas later).
Implemented by: /engine monsters.c (creature base stats), /render gamedemo.c (egg table per
level, hatching, temp actors), no pack change (eggs come from the save template's eggs chunk).

> Provenance
> - Derived from : READING ScummVM/Nuvie `core/egg_manager.cpp` (spawn_eggs / spawn_egg /
>   get_worktype), `core/obj_manager.cpp` ObjManager::update (when eggs are checked),
>   `core/obj.h` (status bits), `actors/actor_manager.cpp` create_temp_actor, and the U6 actor
>   stat table + actor_init/actor_randomise_stat in Nuvie's `ultima.dat` scripts
>   (`u6/actor.lua`, `common/actor.lua`) — to understand the original behaviour. Re-expressed
>   below; no code transcribed.
> - References   : docs/formats/nuvie_save.md (eggs chunk), objblk.md (records, containment),
>   docs/rules/combat.md (hostility by alignment, damage).
> - Tools        : none
> - Validated by  : EGG.REQ headless hook (§6) on emulated TT + stock 4 MB Falcon.
> - Confidence   : confirmed for §1-§4; §5 lists our simplifications.
> - Last updated : 2026-09-30

## 1. The egg object

An **egg** is object 335 on the map (invisible in play). Its record fields:
- `quality` = **alignment + period**: `quality % 10` is the alignment given to what hatches
  (1 neutral, 2 evil, 3 good, 4 chaotic; 0 = the creature's own default), `quality / 10` the
  hatch PERIOD: 0 = always, 1 = day only, 2 = night only, 3 = day and night. Day is 06:00–18:59.
- `qty` = **hatch chance** in percent. 100 = always, and then the embryo counts are exact.
- **contents** (in-container records linked to the egg) = the **embryos**: each has an obj_n,
  a `qty` (how many) and a `quality` (a creature's schedule worktype; 0 for plain objects).
- status bit **0x40 = ACTIVE** (already hatched on this approach); 0x20 = TEMPORARY marks
  what an egg produced (never saved).

All eggs of a game live in the save's dedicated EGGS chunk (nuvie_save.md), each followed by
its embryos, with their level in z.

## 2. When eggs are checked

The player's position is watched in 8×8-tile BLOCKS. Whenever the party enters a NEW block, or
changes map level, or is teleported (a load counts), every egg on the party's level is checked:
- an ACTIVE egg farther than 20 tiles (Chebyshev, on either axis) or on another level is
  RE-ARMED (active bit cleared) — it may hatch again on the next approach;
- an egg **closer than 20 tiles** that is NOT active and — unless the party was teleported —
  **farther than 8 tiles** (so what hatches appears off-screen) becomes active and gets ONE
  hatch roll: `roll = 1..100`; it hatches if `qty == 100 || roll <= qty`, and only when its
  period matches the clock (always / day / night / day+night).

## 3. What hatches

For each embryo of a hatching egg:
- count = embryo `qty`, then `1..qty` at random unless the egg's chance is 100 (exact); a
  silver serpent (413) embryo always hatches exactly one;
- an embryo is a **creature** when its obj_n ≥ 342 (the first monster type) or it is a chest
  (98, the mimic) or its quality (worktype) ≠ 0; else it is a plain **object** (e.g. reagents)
  placed at the egg's tile, temporary and take-able;
- a creature becomes a **temporary actor** at the egg's tile — tossed to a free tile nearby
  when the tile is occupied — with the egg's alignment (or its own default), its embryo's
  worktype, and stats rolled from the creature table (§4): str/dex/int/hp each
  `base − t + rand(0..t) + rand(0..t)` with `t = base/2` (the base itself when t = 0),
  level = (hp + 29) / 30, exp 100.
- Gargoyles (362/363) hatch as peaceful wanderers when the party carries the Amulet of
  Submission or has Beh Lem (actor 164) — the treaty rule.

Temporary actors are discarded when the party leaves the level; their bodies/loot are the
combat system's concern.

## 4. Creature table (str, dex, int, hp, base damage, alignment)

| obj_n | creature | str | dex | int | hp | dmg | align |
|---|---|---|---|---|---|---|---|
| 98 | mimic (chest) | 22 | 9 | 8 | 30 | 15 | chaotic |
| 342 | giant rat | 5 | 13 | 4 | 8 | 6 | chaotic |
| 343 | insects | 1 | 40 | 1 | 1 | 1 | chaotic |
| 344 | giant bat | 5 | 25 | 4 | 5 | 6 | chaotic |
| 345 | giant squid | 24 | 20 | 8 | 50 | 20 | evil |
| 346 | sea serpent | 17 | 17 | 9 | 70 | 30 | evil |
| 347 | reaper | 21 | 20 | 16 | 30 | 20 | chaotic |
| 348 | sheep | 5 | 10 | 4 | 6 | 2 | neutral |
| 349 | dog | 5 | 16 | 8 | 6 | 3 | neutral |
| 350 | deer | 11 | 18 | 5 | 8 | 2 | neutral |
| 351 | wolf | 8 | 16 | 8 | 8 | 4 | chaotic |
| 352 | ghost | 1 | 10 | 9 | 20 | 12 | evil |
| 353 | gremlin | 2 | 26 | 7 | 5 | 1 | chaotic |
| 354 | mouse | 2 | 25 | 3 | 5 | 2 | neutral |
| 355 | gazer | 2 | 10 | 21 | 20 | 1 | evil |
| 356 | bird | 1 | 28 | 4 | 3 | 1 | neutral |
| 357 | corpser | 17 | 6 | 3 | 40 | 15 | chaotic |
| 358 | snake | 3 | 7 | 6 | 5 | 2 | chaotic |
| 359 | rabbit | 3 | 9 | 3 | 5 | 2 | neutral |
| 360 | rot worm | 3 | 17 | 2 | 5 | 2 | chaotic |
| 361 | giant spider | 9 | 11 | 4 | 10 | 8 | chaotic |
| 362 | winged gargoyle | 15 | 32 | 33 | 50 | 6 | evil |
| 363 | gargoyle | 25 | 19 | 8 | 30 | 15 | evil |
| 364 | acid slug | 5 | 5 | 2 | 10 | 1 | chaotic |
| 365 | tangle-vine flower | 20 | 5 | 1 | 30 | 1 | chaotic |
| 366 | tangle vine | 20 | 25 | 1 | 30 | 1 | chaotic |
| 367 | daemon | 35 | 26 | 31 | 100 | 20 | evil |
| 368 | skeleton | 10 | 14 | 7 | 20 | 6 | evil |
| 369 | drake | 22 | 22 | 13 | 50 | 10 | evil |
| 370 | headless | 19 | 13 | 8 | 20 | 12 | evil |
| 371 | troll | 18 | 14 | 9 | 15 | 6 | evil |
| 372 | mongbat | 20 | 27 | 13 | 30 | 20 | evil |
| 373 | wisp | 1 | 40 | 20 | 40 | 1 | chaotic |
| 374 | hydra | 26 | 11 | 2 | 50 | 15 | chaotic |
| 375 | slime | 6 | 6 | 2 | 15 | 4 | chaotic |
| 376 | fighter | 20 | 17 | 11 | 20 | 6 | chaotic |
| 377 | swashbuckler | 15 | 18 | 12 | 15 | 4 | chaotic |
| 378 | mage | 10 | 14 | 22 | 30 | 4 | evil |
| 379 | villager | 11 | 12 | 12 | 8 | 4 | chaotic |
| 380 | merchant | 12 | 12 | 18 | 8 | 4 | evil |
| 381 | child | 8 | 8 | 9 | 6 | 2 | chaotic |
| 382 | guard | 9 | 30 | 9 | 100 | 8 | evil |
| 383 | jester | 14 | 16 | 12 | 8 | 4 | chaotic |
| 384 | peasant | 8 | 8 | 9 | 6 | 3 | chaotic |
| 385 | farmer | 15 | 15 | 10 | 8 | 4 | neutral |
| 386 | musician | 12 | 16 | 14 | 8 | 4 | chaotic |
| 387 | woman | 10 | 14 | 15 | 8 | 4 | neutral |
| 388 | cat | 4 | 18 | 8 | 6 | 2 | neutral |
| 409 | Lord British | 30 | 30 | 30 | 255 | 255 | good |
| 410 | Avatar | 25 | 25 | 25 | 30 | 4 | good |
| 411 | dragon | 40 | 35 | 27 | 150 | 30 | evil |
| 413 | silver serpent | 60 | 21 | 8 | 200 | 60 | chaotic |
| 424 | cyclops | 27 | 8 | 7 | 40 | 20 | evil |
| 426 | giant scorpion | 13 | 13 | 3 | 20 | 8 | chaotic |
| 427 | giant ant | 16 | 15 | 2 | 10 | 8 | chaotic |
| 428 | cow | 14 | 7 | 5 | 10 | 3 | neutral |
| 429 | alligator | 20 | 10 | 3 | 30 | 10 | chaotic |
| 430 | horse | 22 | 27 | 7 | 15 | 3 | neutral |
| 431 | horse with rider | 22 | 27 | 7 | 15 | 3 | neutral |

(Human-type rows are what a hatched bandit / guard uses; vehicles omitted.) A type not in the
table hatches with 10/10/10, 30 hp, level 1 — the original's fallback.

## 5. u6e v1 simplifications (documented deviations)

- Creatures hatch into the fixed 256-actor table's FREE slots (`g_atile == 0xffff`, high
  slots first, as summons do); when none is free the embryo is skipped.
- Hatched creatures never persist: the save carries eggs, not what hatched (as the original).
  A temporary actor is discarded on level change; on the surface it lives until slain or the
  party goes 20+ tiles away and the egg re-arms (it is then simply replaced next time).
- Worktypes: hostile alignment (evil/chaotic) → the combat AI; neutral/good → wander. The
  finer schedule worktypes (guard posts, etc.) are not modelled for hatched creatures.
- Plain-object embryos hatch as ordinary map objects (dirty, not persisted): the reagent
  patches regrow every visit, like the original's temporaries.
- Toss-to-free-tile searches the 5×5 around the egg (the original tries 3×2 / 2×3 / 4×4
  spreads); no grouping by type.

## 6. Test hook

`EGG.REQ` byte: `'1'` = pick the surface egg nearest the start with a creature embryo and
chance ≥ 50, place the party 12 tiles from it, force a block change → print
`EGG kind= n=` (hatched creature count) and the nearest hatched creature's obj_n and distance;
`'2'` = the same with the party placed ON the egg then stepped once (must NOT hatch: within
8 tiles); `'3'` = enter level 1 (as DUNGEON.REQ 0) and report how many creatures hatched
from the level's eggs within 20 tiles.

## 7. Persistence

The ACTIVE bit is written back into the template's eggs chunk (status 0x40) whenever the level
is reconciled (before a level switch / S)ave) and read on load, so a save made near a hatched
egg does not hatch it again on reload — exactly what the original's saved egg status does.

## Status
VERIFIED 2026-09-30. EGG.REQ on emulated TT and the stock 4 MB Falcon: '1' the nearest
surface egg (3 gargoyles, chance 100, evil) hatches when the party is 12 tiles away — three
hostile gargoyles (hp ≈ 30 rolled) at distance 12; '2' standing on it hatches nothing; '3' level
1 holds 155 eggs, the 2 within 20 tiles of the cave entry are checked and hatch per their chance.
Start of play: the throne-room egg at (307,350) hatches its 3 gargoyles in front of Lord
British — Ultima VI's actual opening battle. LOAD.REQ '1': the hatch is remembered across a
save + load (0 re-hatched, 2 eggs active).
