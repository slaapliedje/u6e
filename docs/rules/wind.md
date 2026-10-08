# Wind, sailing costs, rafts and the balloon (U6)

Games: Ultima VI (SE/MD have rafts/canoes with their own rules — RuleSet later).
Implemented by: /engine save.c (wind byte), /render gamedemo.c (wind state + timer, vehicle
movement costs and drift, balloon inflate/board/land, the fan), docs/rules/vehicles.md (boarding).

> Provenance
> - Derived from : READING ScummVM/Nuvie `core/weather.cpp` (wind storage, change timer, calm),
>   `core/player.cpp` Player::moveRelative + pass (ship/skiff cost tables, raft steering table,
>   balloon drift), `actors/u6_actor.cpp` U6Actor::check_move (air movement boundary),
>   `usecode/u6_usecode.cpp` use_fan / use_balloon, `save/obj_list.h` (direction codes) — to
>   understand the original behaviour. Re-expressed; no code transcribed.
> - References   : docs/formats/objlist.md (0x1bfa), tileflag.md (Impassable/Wall/Wet bits,
>   flowing-water tiles 8..15), vehicles.md.
> - Tools        : none
> - Validated by  : WIND.REQ headless hook (§7) on emulated TT + stock 4 MB Falcon.
> - Confidence   : confirmed for §1-§5; §6 lists our simplifications.
> - Last updated : 2026-10-07

## 1. The wind

- One global wind direction, saved in the objlist at **0x1bfa**: `0..7` = N, NE, E, SE, S, SW,
  W, NW (clockwise from north — the original's 8-direction code), **0xff = calm**. The byte is
  the direction the wind blows FROM (a balloon drifts the opposite way; a ship heading into
  that direction is sailing into the wind).
- It changes on a timer: after each change the next one comes in **1..30 turns** (uniform);
  the new wind is uniform over the 9 states (8 directions + calm). On a dungeon level the
  wind is calm. The status line shows `Wind:XX` (or `Wind:C`).
- The **fan** (object 212): U)se it to turn the wind **45° clockwise** ("You feel a breeze.");
  from calm it becomes north.

## 2. Movement points: how a vehicle's speed works

Each step costs movement points; the world advances one turn (NPCs act, the clock ticks)
per 5 points (a walking step costs 5: one turn, as today). A vehicle's step cost:

| vehicle | cost of a step |
|---|---|
| ship (412) | `ship_cost[d]`, d = |heading − wind| (8-direction codes), = `{10, 5, 3, 4, 5, 4, 3, 5}`: into the wind 10, close-hauled 5, beam reach 3, broad reach 4, running 5. Calm: 5 (§6). |
| skiff (414) | `skiff_cost[d]`, d = |heading − current|, current = the flowing-water tile under the skiff (map tile 8..15 → direction tile−8) or the heading itself when the water is still: `{3, 4, 5, 7, 10, 7, 5, 4}`: with the current 3, against it 10. |
| raft (415), balloon (423) | 5 |

So a ship on a beam reach moves ~1.7 tiles per world turn and a ship beating into the wind
half a tile per turn.

## 3. Raft (415)

Boards and exits like a skiff (any water). Steering:
- on **shallow water** (a Wet tile that is not Impassable) the player steers normally;
- on **deep water** (Wet + Impassable) the player's input is ignored: the raft goes where the
  water and the wind take it. Base direction = the current (tile 8..15 → tile−8), north when
  the water is still; with wind the base is combined with the direction the wind blows TO
  through this table (rows = current, columns = wind-to, all 8-direction codes):

```
            wind-to:  N  NE  E  SE  S  SW  W  NW
  current N        {  N,  N, NE, NE,  N, NW, NW,  N }
  current NE       { NE, NE, NE,  E,  E, NE,  N,  N }
  current E        { NE,  E,  E,  E, SE, SE,  E, NE }
  current SE       {  E,  E, SE, SE, SE,  S,  S, SE }
  current S        {  S, SE, SE,  S,  S,  S, SW, SW }
  current SW       {  W, SW,  S,  S, SW, SW, SW,  W }
  current W        { NW, NW,  W, SW, SW,  W,  W,  W }
  current NW       { NW,  N,  N, NW,  W,  W, NW, NW }
```
- **Passing a turn** (standing still) on a raft or in the balloon moves it one tile by this
  rule when there is wind.

## 4. Balloon

- The **balloon** is carried as an item (object 420). U)se it: it is placed on a free tile
  beside the party and inflated (object 423, frame 3) — "Done!". Not usable on dungeon
  levels.
- U)se the inflated balloon to board (any terrain). Aboard, the balloon cannot be steered:
  every move attempt (or a passed turn) carries it one tile in the direction the wind blows
  TO; in calm: "Thou canst not move without wind!" and nothing moves.
- It flies LOW: it cannot enter a map tile that has the Wall flag, nor a tile holding an
  object whose tile has the Wall flag (buildings, mountains); everything else — water, land,
  trees — passes beneath.
- U)se while aboard: the party lands on adjacent passable land (N/E/S/W, like a boat) and
  the balloon deflates into the item (420) lying at the landing tile.

## 5. Persistence

The wind byte is saved and restored; the raft and the inflated/deflated balloon are map
objects and persist like boats (vehicles.md §6): a sailed raft's record is re-anchored, an
inflated balloon is the carried balloon record relocated onto the map and retyped (420 →
423), a landing retypes it back (423 → 420) at the landing tile.

## 6. u6e v1 simplifications (documented deviations)

- A ship in calm still sails, at the base cost (the reference implementation deducts no
  points at all in calm, which would freeze the world).
- Diagonal vehicle moves use the 8-direction heading for the cost but the four sprite facings.
- Ship cannons and the balloon's altitude/"high air" variant: not modelled.
- The wind timer counts turns, not real time; the change rhythm is the original's range.

## 7. Test hooks

`WIND.REQ` byte modes: `'1'` = wind mechanics: print the loaded wind, force each direction and
report the ship step cost per heading (10/5/3/4/5/4/3/5 pattern), use the fan (rotation),
save → load (byte round-trip); `'2'` = balloon: give the party a balloon, inflate it, board,
force wind NW, move (drifts SE), set calm (refused), land → balloon item lies at the landing
tile; `'3'` = raft: board a real raft (415) of the save on deep water, force wind, step and
report the forced direction vs the table.

## Status
VERIFIED 2026-10-07. WIND.REQ on emulated TT and the stock 4 MB Falcon: '1' a ship under a
north wind costs 10 / 3 / 5 / 3 for N / E / S / W, 5 in calm, five beam-reach steps advance the
world three turns, the fan turns calm → N and NW → N, the wind byte survives save → load;
'2' a carried balloon inflates beside the party, boards, drifts SE under a NW wind (input
ignored), refuses to move in calm, drifts on a passed turn, lands with the balloon item lying
at the landing tile — one balloon item and no inflated balloon after a save → load;
'3' a real raft on shallow water steers; a raft on deep flowing water (tile 9, current NE)
under a west wind is carried NE as the table says, whatever the player asks.
