# R)est — camping (U6)

Implemented by: /render gamedemo.c (the refusals, the prompts, the campfire circle, mealtime,
the sleep, the heal, the rest counter in the save).

> Provenance
> - Derived from : the reference's `core/party.cpp` (can_rest, rest_gather, rest_sleep),
>   `core/events.cpp` (rest, rest_input), `core/timed_event.cpp` (TimedRestGather, TimedRest:
>   eat / bard_play / sleep / the heal in its destructor), `gui/widgets/map_window.cpp` in_town,
>   `actors/actor.cpp` find_enemies, `actors/u6_actor.cpp` wt_sleep / wt_play_lute,
>   `save/obj_list.h` (the rest counter's objlist offset) — READ to understand; re-expressed.
> - References   : as above (Nuvie / ScummVM Ultima engine). No code transcribed.
> - Tools        : headless hook REST.REQ (§6) on emulated TT + 4 MB Falcon.
> - Validated by  : REST.REQ 1-2 (§7).
> - Confidence   : probable (the reference's rules; its own FIXMEs noted in §5).
> - Last updated : 2026-10-27

## 1. Refusals (checked in this order; the scroll shows "Rest" then the reason)
| condition | message |
|---|---|
| combat mode is on | `-Not while in Combat!` |
| mounted on a horse | `-Dismount first!` (the reference tests this last; u6e first — see §5) |
| aboard a vehicle that is not a ship (skiff, raft, balloon) | `-Can not be repaired!` |
| **in town**: any MAP tile within 5 (Chebyshev) of the Avatar carries the Wall flag (tileflags1 bit 2) AND a wall-direction bit (bits 4..7) | `-Only in the wilderness!` |
| **foes near**: any living hostile actor (evil / chaotic toward the party; charmed or allied ones are not foes) within 24 tiles on this level, invisible ones excepted | `-Not while foes are near!` |
| **others near** (not aboard a vehicle): any actor outside the party within 5 tiles | `-Not while others are near!` |
| not in party mode (solo) | `-Not in solo mode!` — u6e has no solo mode: never shown |
| not aboard a vehicle and any of the 3×3 tiles around the Avatar is impassable | `-Not enough room!` |

Aboard a **ship** the command is the hull repair of cannons.md §2 (no prompts).

## 2. Prompts
- `How many hours? ` — digits; Enter commits, Esc or 0 cancels.
- With more than one member: `Who will guard? ` — a member number 1..n (0, Enter or a number
  past the party = `none`; otherwise the member's name is echoed). The reference also accepts a
  click on a member; u6e takes the digits.

## 3. The camp
- A **campfire** (object 253, frame 1 = lit) is placed on the Avatar's tile as a temporary
  object (never saved). The party gathers in the 3×3 ring around it, by member index:
  0 north, 1 south, 2 east, 3 west, 4 north-east, 5 south-west, 6 south-east, 7 north-west;
  a taken or impassable slot falls back to the first free ring tile. Everyone faces the fire.
  (The reference walks each member to the slot with its pathfinder for up to 50 moves; u6e
  places them.)
- **Mealtime** (one line per rest turn, in this order): `Mealtime!`; a bard in the party
  (actor type 386) takes the lute-playing shape (392): `<name> plays a tune.`; then each member in
  party order: a food item (is_food class 1: not drink) is consumed from the pooled pack —
  `<name> has food.` and the member counts as fed — else `<name> has no food.`; then the sleep:
  the bard's shape reverts, the guard `<name> stands guard while the party rests.`, every
  other member lies down (drawn as a body, object 339 frame 0) and sleeps.
- **The sleep**: the clock runs `hours` × 6 turns of ten minutes (one turn per 1/8 s of wall
  time); the world ticks as usual (NPC schedules, torches, poison, mana regeneration), the
  party does not move.
- **Someone comes (DOS GAME.EXE 4.5, read 2026-11-05):** on turns whose number has bits 0 and 2 clear, one chance in 64 of a
  band: one type of 351, 358, 342 or 353, d8 of them, each on a free spot on the edge of the view (8 tries each), evil and
  assaulting. If any arrived the rest ends there, with no healing: with a guard, `You hear something!`; with none, the
  party wakes asleep.

## 4. Waking
- The campfire goes out (frame 0) and stays as a burnt campfire.
- Healing happens only after a rest of 4 hours or more that nobody broke, if the **rest counter** is 0. Then, for each member in party order
  `s`: if the member slept (not the guard) and `s` < the number of members that had food,
  the deficit `d = max − hp` (a deficit of 1 counts as 2) heals by `d/2 + rand(0..d/2−1)`:
  `<name> has healed.` (the reference's `s < fed` is by index, not by who ate — kept).
  A dead companion (0 HP, combat.md §3d) takes no part in the camp: it never heals, never plays the
  lute, is not laid down or stood up, and cannot be picked to guard (the pick becomes "none").
  Before 2026-10-31 the camp put a dead bard back on the map (playing his lute), and the game then
  killed him again, 5 karma and a new body each time (found by the bot).
  A dead companion never heals by resting. Until 2026-10-31 it did, and stood
  up again with half its HP back (found by the bot after its death segment).
- If healing was allowed the rest counter becomes **12** (hours). It counts down once per game
  hour and lives in the objlist at **0x1bf2** (one byte; formats/objlist.md), so a save
  remembers when the party may heal by resting again.
- Magic points are NOT refilled by the rest itself; they regenerate with the turns slept, as
  always (magic_skills.md).
- Everyone stands up (their shapes revert); the map re-centres.

## 5. u6e deviations
- The horse refusal is tested before the vehicle one (the reference's "in a vehicle" means the
  vehicle actor is the player; a rider there is still the Avatar, so its order coincides).
- "Not in solo mode!" never fires (no solo mode).
- No pathfinding to the circle.
- **Mealtime pacing** (timed in DOS 2026-11-05, GOG copy, 3000 cycles): `Mealtime!`, the bard's line and the first member's line come together; then one member a line every 4 DOS ticks (0.22 s); the sleep starts 3 s after `Mealtime!` (2.9 s with music, 3.0 s without; DOS ends it with a tune's cycle). u6e: 44 and 600 ticks of 200 Hz. The sleep that follows took 5.6 s for 8 hours in DOS; u6e's 1/8 s a turn takes 6 s.
- The "in town" scan takes every map tile within 5, not only the currently visible ones.

## 6. Test hook
`REST.REQ` byte modes (seeded RNG):
- `'1'` refusals: at the castle `Only in the wilderness!`; combat mode on → `Not while in
  Combat!`; the party moves to the first open tile (no walls within 5, 3×3 passable) found by
  scanning outward from (307,352); a hostile hatched 10 tiles away → `Not while foes are near!`;
  removed, a neutral creature 3 tiles away → `Not while others are near!`; removed, an impassable
  object beside the Avatar → `Not enough room!`; removed → the prompt `How many hours? ` arms.
- `'2'` the camp at that open tile, 20:00: a torn party (Avatar at 1 HP, the others at half), one
  portion of meat for the first two members; keys `8`, Enter, `2`, Enter (member 2 guards):
  prints the campfire index, each member's slot offset, the mealtime lines, the hours passed
  (8), the guard unhealed, the fed members healed, the rest counter 12; a second rest right
  after heals nobody (counter 12); S)ave → the objlist byte at 0x1bf2 is 12; the counter after
  12 game hours is 0.
- `'3'` the camp held for a capture (21:00, the fire lit, the party asleep around it, member 2 on
  guard).

## 7. Status
VERIFIED 2026-10-27: REST.REQ 1-3 on emulated TT and the stock 4 MB Falcon, logs identical (timing
aside): '1' at the castle `-Only in the wilderness!`, combat mode `-Not while in Combat!`, at the open
tile (296,337) a hatched gargoyle 10 east `-Not while foes are near!`, a dog 3 east `-Not while others
are near!`, a chest beside `-Not enough room!`, then `Rest` with the hours prompt armed; '2' at 20:00:
the prompts take 8 and member 2 (Dupre guards), the lit campfire (253 frame 1) sits on the Avatar's old
tile with the party N/S/E/W of it facing in, `Mealtime! Iolo plays a tune.`, the Avatar and Dupre eat
the two portions of meat, Shamino and Iolo have none, `Dupre stands guard while the party rests.`, the
Avatar lies as a body (339) while the guard stands, 8 hours pass (20:00 → 04:00), `Avatar has healed.`
(1 → 87 of 90 HP; the guard and the unfed member only regenerate), the rest counter is 12, the fire is
out; a second camp heals nobody and the counter is 4 after its 8 hours; S)ave writes 4 at 0x1bf2, a load
restores it, 4 more game hours bring it to 0; '3' the captured frame shows the night camp. The camp
spot is found by scanning outward from (307,352) for a tile where the refusals pass; arriving there
hatches an egg's gargoyles (cleared by the hook). Found on the way: a world turn at 20:00-21:00 costs
~1.7 s on the TT (0.35-0.39 s by day, 166 actors pathing home: checklist item 17), so eight hours take
~85 s on a TT and ~4 min on a 4 MB Falcon; and the status line's clock piled up under the transparent
border whenever the clock changed without a scroll (fixed: draw_status repaints its band).

## 7. As DOS has it (read 2026-11-05; overrides the above)

**Refusals, in order:** combat `-Not while in Combat!`; a vehicle but a ship `-Can not be repaired!`; town (any tile within 5 with walls and impedance) or, on foot, a wet tile underfoot `-Only in the wilderness!`; any evil or chaotic actor on the level inside the 39-tile window from ((x-16)&~7, (y-16)&~7) `-Not while foes are near!`; anyone not of the party within 5 but a type 430 horse `-Not while others are near!`; `-Not in solo mode!`; the 3x3 `-Not enough room!`. Aboard these all apply, then the hull gains 5. Last, any rider `-Dismount first!`.

**Prompts:** `How many hours? ` takes one key, 1-9, anything else 0 and it's over. `Who will guard? ` takes a digit up to the party's size less one, or the crosshair; anything else `none`.

**Camp:** a lit campfire on the tile. Up to 20 rounds each walks to its ring place, dx 0,0,1,-1,1,-1,1,-1, dy -1,1,0,0,-1,1,1,-1; the Avatar too, each round (until 2026-11-07 ours moved him only in the first round, so a follower standing on his place left him in the fire all night: found by the bot); one that can't stays where it is, turned to the fire. **Mealtime:** each eats its own food, else anyone's. **Healing** (4+ hours, counter 0, unbroken) goes to members before the first who went hungry, not the guard: deficit d (1 reads as 2), d/2 + 0..d/2, and **magic full**. The guard walks about under worktype 0xc. Broken with no guard: everyone stays asleep, lying down, until the usual roll.
