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
>   §3c: the reference (ScummVM nuvie) actor.lua actor_init and creature table, read for understanding.
> - Tools        : none
> - Validated by  : EGG.REQ headless hook (§6) on emulated TT + stock 4 MB Falcon.
> - Confidence   : confirmed for §1-§4; §5 lists our simplifications.
> - Last updated : 2026-11-01

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

## 3b. The embryo's worktype (2026-10-27; the reference's EggManager::get_worktype + the
actor script's worktype handlers, read to understand)
The embryo's quality IS the hatchling's worktype (gargoyles under the treaty get "wander near
player" instead). The user's data: of 961 creature embryos, 500 assault (0x08), 224 unfriendly
(0x0b), 63 flee (0x09), 43 wander (0x0c), 89 combat stances (0x03-0x06), 25 stationary (0x0e),
10 like (0x0a), 9 guard walks (0x0f/0x10), 2 motionless, 1 in-party (0x01), 1 retreat. The code
is forced on the hatchling where it stands (usecode_vm.md §6b's mechanism; no schedule, so it
holds until slain) and decides its turns:
| code | name | each turn |
|---|---|---|
| 0x08, 0x9b, 0x03-0x06 | assault, attack-party, the stances | the assault AI (combat.md §7) |
| 0x07, 0x09, 0x9a | retreat, flee, mousing | the coward AI: runs from a party it sees, cowers cornered |
| 0x0b | unfriendly | roams (half the turns a step toward a point ten tiles off along a random axis); a party member within two tiles turns it to **assault** with a 1-in-8 chance per turn |
| 0x0a | like | the same roam, but a party member within two draws it toward the party half the turns |
| 0x0c, 0x8f | wander near player / wander around | one turn in eight a step in a random direction |
| 0x0e, 0x0d | stationary, tangle | never moves; a hostile one strikes a party member standing beside it (the reference tries 16 random tiles of the 9×9 around it: ≈16/81 per neighbour per turn; the tangle vine strikes a neighbour every other turn) |
| 0x0f, 0x10, 0x8b-0x8e | guard walk east-west / north-south, walk N/E/S/W | half the turns a step along its facing (0x0f starts east, 0x10 north, 0x8b-0x8e as named); blocked, it turns back |
| 0x00, 0x01 | motionless | nothing: a creature embryo whose quality is 0 never takes a turn (the reference's "nothing" worktype), hostile or not |

## 3c. What a hatchling carries (2026-11-01; the reference's actor_init, read for understanding)

The readied ones and the ammunition are what it really fights with (combat.md §6c): it runs out.

A creature that hatches is given a kit from its type's row of the reference creature table
(four lists: innate spells, weapons, armour, treasure). The spells are the charges of
combat.md §7b. The other three lists are each rolled the same way: the chance starts at 1 in 2
and doubles with each entry of the list (1/2 for the first entry, 1/4 for the second, …), and
an entry that wins its roll is added:

- **weapon**: one of it, readied; a spear, throwing axe or dagger (36–38) comes as 6–12 of
  them. A bow or magic bow (41/54) adds 12–24 arrows (55), a crossbow or triple crossbow
  (42/50) 12–24 bolts (56).
- **armour**: one of it, readied.
- **treasure**:
  - **gold** (88): 2–60 coins (1–30 plus 1–30);
  - **a chest** (98, closed): inside it a trap (the effect 337) half the time; 1–100 gold
    3 times in 4; 1–10 of a food (meat 129, ham 133 or bread 128, one picked at random) 3
    times in 4; 1–6 torches (90) 1 time in 4; 1–4 gems (77) 1 time in 11;
  - **"spell"** (58, a caster's purse): 1–100 gold 3 times in 4 (never for a winged
    gargoyle, 362); 1–100 of a food 1 time in 4; one potion (275) of a random colour (frame
    0–7) 1 time in 4;
  - **anything else** (meat for beasts, the musician's lute 158): one of it.

| creature | weapons | armour | treasure |
|---|---|---|---|
| 98 mimic | — | — | chest, chest |
| 347 reaper | — | — | spell, gold |
| 348 sheep · 350 deer · 361 giant spider · 428 cow | — | — | meat |
| 362 winged gargoyle | 46, 41, 47, 49 | 18, 1 | spell |
| 363 worker gargoyle | 34, 49, 37 | 1, 9, 18, 20 | — |
| 368 skeleton | 43, 36, 37, 41, 38 | 9, 1 | gold |
| 369 drake | — | — | gold, gold |
| 370 headless | 34, 36 | 17, 9 | gold |
| 371 troll | 37, 36, 43, 41, 34 | 9, 18, 1, 19 | chest, gold, gold |
| 372 mongbat | 38, 43, 36, 37 | 9, 17, 18 | chest, gold |
| 376 fighter | 43, 42 | 21, 10, 3, 22 | gold, gold |
| 377 swashbuckler | 43, 41, 33 | 19, 1, 9 | gold, gold |
| 378 mage | 38 | 17, 1 | spell, chest, gold |
| 379 villager | 43, 33 | 17 | gold |
| 380 merchant | 38, 43 | 17, 1 | gold |
| 382 guard | 42, 47 | 22, 10, 3 | gold |
| 383 jester | 38, 33 | 17 | gold |
| 384 peasant | 38, 34 | 185 | — |
| 385 farmer | 105, 101 | 17 | gold |
| 386 musician | 41, 33, 43 | 19, 1, 9 | gold, lute 158 |
| 387 woman | 38 | 185 | gold |
| 411 dragon | — | — | chest, gold, gold |
| 424 cyclops | 44, 33, 34 | 20, 3, 13 | chest |

Every other type carries nothing. A readied item goes to the slot equipment.md gives it and
stays in the pack when that slot is taken; the creature fights with the best readied weapon and
the summed armour (equipment.md §4), as a person does. When it dies, all of it goes into its
body (combat.md §3c), the readied items unreadied; a creature that leaves only blood (338) or
nothing drops it on its tile. These objects are temporary like their owner but free to take,
and what the party takes is its own from then on.

Deviation: the reference readies ranged weapons and its foes shoot them; ours fight hand to hand
with whatever they readied (foes have no missile attacks yet; deviations.md).

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

**Innate spells** (the reference table's spell column, shipped in monsters.c `monster_spells`; combat.md §7b):
daemon 17/128/34/80, dragon 34/83/68/96/119, drake 34/68, winged gargoyle 68/65/69, gazer 20/68/80,
ghost 128/99, hydra 69/96/98/68, mage 17/34/20/49/68/80/69/83, mimic 18/80, reaper 17/80/66/68,
sea serpent 34/83/51, giant spider 89, giant squid 17/68, wisp 20/69/129, tangle-vine flower 20.

## 5. u6e v1 simplifications (documented deviations)

- Creatures hatch into the fixed 256-actor table's FREE slots (`g_atile == 0xffff`, high
  slots first, as summons do); when none is free the embryo is skipped.
- Hatched creatures never persist: the save carries eggs, not what hatched (as the original).
  A temporary actor is discarded on level change; on the surface it lives until slain or the
  party goes 20+ tiles away and the egg re-arms (it is then simply replaced next time).
- Worktypes (§3b): the coward's sight range is 5 for every flee code (the reference flees
  within 3 for flee/mousing and 8 for retreat, and a cornered retreater fights back); the
  stationary strike is a per-neighbour 16/81 chance, not 16 random picks; a step "toward a point
  ten tiles off" is one step in that direction; the tangle vine never crawls.
- Plain-object embryos hatch as ordinary map objects (dirty, not persisted): the reagent
  patches regrow every visit, like the original's temporaries.
- Toss-to-free-tile searches the 5×5 around the egg (the original tries 3×2 / 2×3 / 4×4
  spreads); no grouping by type.

## 6. Test hook

`EGG.REQ` byte: `'1'` = pick the surface egg nearest the start with a creature embryo and
chance ≥ 50, place the party 12 tiles from it, force a block change → print
`EGG kind= n=` (hatched creature count) and the nearest hatched creature's obj_n and distance;
`'2'` = the same with the party placed ON the egg then stepped once (must NOT hatch: within
8 tiles); `'4'` = hatched worktypes (§3b): the level's embryo worktype counts, then on an open 7×7:
a stationary reaper beside the party stays put and strikes, a guard walk paces its row, a
fleeing bird runs, a liking dog comes adjacent, an unfriendly rat beside the party turns to
assault within 60 turns, a wandering rabbit moves and harms nobody, a motionless rat stays,
an assault rat is hostile and alert; `'3'` = enter level 1 (as DUNGEON.REQ 0) and report how many creatures hatched
from the level's eggs within 20 tiles.

## 7. Persistence

The ACTIVE bit is written back into the template's eggs chunk (status 0x40) whenever the level
is reconciled (before a level switch / S)ave) and read on load, so a save made near a hatched
egg does not hatch it again on reload — exactly what the original's saved egg status does.

## Status
VERIFIED 2026-09-30; §3b added 2026-10-27 — EGG.REQ '4' on emulated TT and the stock 4 MB Falcon (identical): the
surface's 316 creature embryos count 96 assault / 64 unfriendly / 47 flee / 39 wander / 13 stationary / 8 like
/ 6 + 3 guard walks / 38 stances; on an open 7×7 a stationary reaper beside the party stays and strikes (17 HP
lost in 12 turns), a guard paces its row over a span of 7, a fleeing bird is 6 tiles off after 6 turns, a
liking dog comes adjacent, an unfriendly rat beside the party turns to assault on turn 11, a wandering rabbit
moves and harms nobody, a motionless rat stays, an assault rat is hostile and alert. EGG.REQ on emulated TT and the stock 4 MB Falcon: '1' the nearest
surface egg (3 gargoyles, chance 100, evil) hatches when the party is 12 tiles away — three
hostile gargoyles (hp ≈ 30 rolled) at distance 12; '2' standing on it hatches nothing; '3' level
1 holds 155 eggs, the 2 within 20 tiles of the cave entry are checked and hatch per their chance.
Start of play: the throne-room egg at (307,350) hatches its 3 gargoyles in front of Lord
British — Ultima VI's actual opening battle. LOAD.REQ '1': the hatch is remembered across a
save + load (0 re-hatched, 2 eggs active).
