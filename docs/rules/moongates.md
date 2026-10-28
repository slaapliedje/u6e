# Moongates, the Orb of the Moons and moonstones (U6)

Games: Ultima VI (SE/MD have no moongates).
Implemented by: /render gamedemo.c (moon phases, natural gates, the orb, gate entry, moonstone
bury/dig, the game date), /engine save.c (date write-back), docs/rules/magic_skills.md §5d
(Gate Travel reads the same moonstone table).

> Provenance
> - Derived from : READING ScummVM/Nuvie `usecode/u6_usecode.cpp` use_orb / enter_moongate /
>   use_moonstone and its red-gate destination table, `core/weather.cpp` is_moon_visible +
>   update_moongates, `core/game_clock.cpp` (hourly update, the objlist date bytes, 28-day
>   months), `core/nuvie_defs.h` (the two moon periods), and the U6 scripts in Nuvie's
>   `ultima.dat` (`u6/init.lua`: the moonstone table at objlist 0x1c1b, update_moongates) — to
>   understand the behaviour; re-expressed, no code transcribed. Positions checked against the
>   user's own pristine save (8 moonstones, one permanent gate on level 1, the orb in the
>   Avatar's pack).
> - References   : docs/formats/objlist.md (0x1bf3 clock/date, 0x1c1b moonstones), tileflag.md.
> - Tools        : none
> - Validated by  : MOONGATE.REQ headless hook (§6) on emulated TT + stock 4 MB Falcon.
> - Confidence   : confirmed for §1-§4 against the reference; the moon formulas are the
>   reference's own (it marks them "may be wrong") — probable for the original.
> - Last updated : 2026-10-27

## 1. The two moons and the game date

- The objlist holds the date after the clock: 0x1bf3 minute, 0x1bf4 hour, **0x1bf5 day,
  0x1bf6 month, 0x1bf7-8 year** (u16). A month has 28 days, a year 12 months. The new game
  starts 7-04-0161 at 08:00. The engine's clock counts minutes from the loaded time; the day,
  month and year advance as it passes midnight and are written back by S)ave.
- **Trammel** phase = round((day − 1) / 1.75) mod 8 (integer: ((8·(day−1) + 7) / 14) mod 8).
  **Felucca** phase = max(0, (day − 1) mod 9 − 1) (the reference's round(1.1667 · 8) = 9).
- Sky position: Trammel `posT = (hour + 1 + 3·phaseT) mod 24`, Felucca
  `posF = (hour − 1 + 3·phaseF) mod 24` (at hour 0 with phase 0 the reference's byte wraps to
  255 — mirrored). A moon is **visible** when its position is 5..19.

## 2. Natural (blue) moongates — object 85, frame 1

- The save keeps **8 moonstone positions** (objlist 0x1c1b, phases 1..8, each x/y/z as u16
  triples; (0,0,0) = the stone is not in the ground). The pristine data buries seven on the
  surface and one on dungeon level 1 (which already holds a permanent gate object).
- Every hour (and after any travel, level change or load) the gates are refreshed: while a
  moon is visible, a temporary gate object stands on every buried moonstone of the current
  level; when none is visible, the gates the engine raised vanish. Gates are never saved.
- **Entering** a blue gate (stepping onto its tile; not from a vehicle): the moon nearer to
  the zenith decides — `|posT − 12| < |posF − 12|` → Trammel, else Felucca — and the party is
  carried to the moonstone of phase **8 − winning phase** (phases 1..7 → stones 7..1; phase 0
  or a dug-up stone → the gate leads nowhere and the party stays). "You step through the
  moongate."

## 2b. Entering: the walk (2026-10-27; the reference's Party::walk / TimedPartyMove)
Stepping onto a gate (blue or red, the checks of §2 / §3 passed) does not teleport at once:
- The Avatar, already on the gate, **vanishes**: the sprite dissolves on the gate tile over four
  steps (an ordered dither thinning it out).
- Then the companions walk to the gate one step per 1/8 s (their own path steps, the world
  frozen, input ignored); whoever reaches the gate tile dissolves in turn, one at a time. A
  companion off the level, or one that cannot get there within 24 steps, is taken in without
  the walk.
- When everyone is in, the place changes: a **red gate is spent** (its object goes) and, for a red
  gate, the old view **dissolves into the new one** over eight steps (the reference captures the
  map window and pixel-fades it; blue gates change the view at once, as the reference does).
  Then "You step through the moongate." and the companions **fall in** beside the Avatar one per
  step. Should the other level not fit in memory, everyone comes back out beside the gate.

## 3. The Orb of the Moons (object 87) and red gates (object 84)

- U)se the orb from the pack. Requirements: the orb is carried; Lord British (NPC 5) has
  explained it — his talk-flag bit 0x20 — else "You can't figure out how to use it.".
  The game asks **Where:** — a tile within a 6×5 box around the Avatar (dx −3..+2, dy −2..+2;
  with the keyboard the eight neighbours, with the mouse any tile). The tile must be passable
  and empty of actors, else "Failed.".
- `position = (3 + dx) + 5·(2 + dy)` picks the destination (positions 0 and 12..14 lead
  nowhere; the gate still appears). "a red moon gate appears." — a temporary red gate
  (frame 1) whose quality is the position.
- **Entering** a red gate needs the orb ("You forgot the Orb of the Moons!") and carries the
  party to the destination of its quality:

| pos | destination | pos | destination | pos | destination |
|---|---|---|---|---|---|
| 1 | (899,499) | 9 | (188,45) level 5 | 18 | (108,221) level 5 |
| 2 | (935,262) | 10 | (159,942) | 19 | (923,876) |
| 3 | (435,395) | 11 | (739,699) | 20 | (295,38) |
| 4 | (503,358) | 15 | (227,131) | 21 | (75,507) |
| 5 | (147,883) | 16 | (23,22) level 1 | 22 | (327,822) |
| 6 | (919,934) | 17 | (128,86) level 5 | 23 | (387,787) |
| 7 | (68,45) level 5 | | | 24 | (831,166) |
| 8 | (307,352) Lord British's throne room | | | 25 | (667,67) |

  (surface unless a level is given; 0, 12, 13, 14 = nowhere). The keyboard's eight
  neighbours reach positions 7, 8, 9 (north row), 17, 18, 19 (south row); the middle row
  (12..14) is the "nowhere" row.

## 4. Moonstones (object 73, frame = phase − 1)

- G)et a moonstone: its table entry becomes (0,0,0) — that gate is gone until the stone is
  buried again.
- U)se a carried moonstone: it is **buried** at the Avatar's tile when the map tile there is
  bare ground (map tile 1..7 or 16..111: grass, dirt, sand …) — "buried." — the stone lies on
  the map (take-able) and its table entry is the Avatar's position; elsewhere "Cannot be
  buried here!". The gates refresh at once.

## 5. u6e v1 simplifications (documented deviations)

- No "party mode" check: u6e has no solo mode, so the reference's "You must be in party mode
  to enter." never arises.
- The vanish is an ordered-dither thinning of the member's sprite; the reference fades random
  pixels of a capture of the whole map window. On the ST's planar path a red gate changes the
  view at once (no chunky view to dissolve).
- The red gate appears without the reference's vanish effect.
- Travel between levels goes through the same level switch as ladders (objects reconciled,
  the destination level decoded); on a 4 MB machine that can fail for lack of memory, in
  which case the party stays.
- Gate Travel (the spell) keeps its own rule (next surface moonstone, magic_skills.md §5d).

## 6. Test hook

`MOONGATE.REQ` byte modes: `'1'` orb: give the orb, set Lord British's flag, U)se it, aim the
first free neighbour → a red gate with the expected quality; step onto it → the party at the
table's destination (level + x/y printed); `'2'` blue: force day 5 / hour 12 (Trammel phase 2
visible), stand by moonstone 1 (935,262) → the gate object appears; step through → stone
8−2 = 6 → (23,22) on level 1 → printed; then hour 2 → the gate is gone; `'5'` = the blue gate of '2' held mid-walk (the first companion half dissolved) for a frame;
`'6'` = the red gate of '1' held mid-dissolve of the view (level 8 of 16) for a frame; `'3'` moonstone: bury
a carried stone on grass → table entry = the Avatar's tile and a stone on the map; G)et it →
entry cleared; `'4'` date: advance the clock 50 hours → day 6 09:00 (from 7-04 08:00),
S)ave → objlist bytes day/month/year.

## Status
VERIFIED 2026-10-27 (the walk added the same day: '1' takes 50 walk steps to the gargoyle world, '2' 28
steps with all four members dissolving and the three companions shown again at the destination;
frames of '5' and '6' captured; TT and Falcon identical). MOONGATE.REQ on emulated TT and the stock 4 MB Falcon (identical): '1' with the orb
and Lord British's flag the pack U)se asks "Where:", the first free neighbour (dx 1, dy −1 → position 9)
raises a red gate of quality 9, stepping in carries the party to (188,45) on level 5; '2' day 5 at 12:00
(Trammel phase 2, Felucca 3; visible at 12, not at 16) a gate stands on moonstone 1 (935,262), vanishes at
16:00 and returns at 12:00, and stepping in leads to stone 6 → (23,22) on level 1; '3' burying is refused
on the throne-room floor, works on bare ground (table entry = the Avatar's tile, the stone lies there,
a gate rises on it) and G)et takes the stone from under the gate and clears the entry; '4' 50 hours
move 7-04 08:00 to day 6 10:00 and S)ave writes day 6 / month 7 / year 161.
