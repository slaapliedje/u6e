# Vehicles: skiff, ship, horse (U6)

Games: Ultima VI (SE/MD have rafts/canoes — RuleSet later; raft + balloon deferred here too).
Implemented by: /engine world.c (water passability), /render gamedemo.c (board/exit + movement),
no pack change (tileflags1 already ships as U6WG).

> Provenance
> - Derived from : READING ScummVM/Nuvie `usecode/u6_usecode.cpp` (use_boat + find_center +
>   find_land, the horse mount/dismount handler), `core/player.cpp` (vehicle movement costs,
>   party drag-along), `actors/u6_actor.cpp` U6Actor::check_move (movetype water rules),
>   `actors/u6_actor_types.h` (vehicle movetypes), `core/map.cpp` Map::is_water — to understand
>   the original behavior. Behavior re-expressed; no code transcribed.
> - References   : docs/formats/tileflag.md (tileflags1 bit0 Wet), objblk.md (object records).
> - Tools        : none (rule spec)
> - Validated by  : VEHICLE.REQ headless modes (§7) on emulated TT — board, sail on water,
>   blocked by land/shallows, disembark, horse mount/dismount.
> - Confidence   : confirmed for the rules below; §5 lists our v1 simplifications.
> - Last updated : 2026-09-20

## 1. Objects and actors

| obj_n | thing | movement class |
|---|---|---|
| 412 | ship (three map objects: front / centre / back) | deep water only |
| 414 | skiff | any water |
| 415 | raft | any water, wind-drifted (DEFERRED) |
| 430 | horse (an actor) | land |
| 431 | horse with rider (the mounted actor's obj_n) | land |
| 149 | ship deed | key item for buying boats |

Ship frames encode the part + facing: frame 0-7 = front, 8-15 = centre, 16-23 = back;
`direction = (frame % 8) / 2` (0 N, 1 E, 2 S, 3 W); +1 within the pair = the animation phase.
The three parts line up along the facing axis: facing N, the front part is one tile NORTH of
the centre and the back one tile SOUTH (and rotated accordingly).

## 2. Water rules (from U6Actor movement + Map::is_water)

- A map tile is WATER iff its tileflags1 bit0 (Wet) is set — the ORIGINAL map tile, ignoring
  objects; any object ON the tile blocks a vehicle.
- **Skiff/raft** may enter any water tile.
- **Ship** may enter only OPEN water: water AND the map tile number is NOT 16..47 (the
  shoreline/shallows tile band).
- Land actors may never enter water (already enforced); vehicles may never leave it.

## 3. Boarding and exiting a boat (use_boat)

(U)se on a skiff/ship — all checks in this order:
1. If already aboard: find adjacent PASSABLE land, trying North, East, South, West of the
   vehicle's position; found → the party disembarks there, the boat stays as map objects
   (ship keeps its hull strength in the centre object's qty); none → "Only next to land."
2. A skiff/raft being boarded must sit on water ("You must place it in water first.").
3. Using any part of a ship redirects to its CENTRE object (offset from the front/back part
   by the §1 facing geometry); no centre found → "Ship not usable".
4. Deed check: a boat with quality != 0 is for sale — the party must carry a SHIP DEED
   (obj 149) whose quality matches, else "A deed is required." (with the deed, the boat's
   quality is cleared = owned).
5. The party must be adjacent to the boat (use range 1).
6. Board: the party vanishes into the vehicle (roster stays, sprites hidden), and the player
   now steers the vehicle; the camera follows it. Ship movement turns the three parts as a
   unit and re-faces them (frames per §1); a move's target tiles (all parts) must pass §2.

## 4. Horses

- (U)se a wandering horse (obj 430, a schedule-less animal actor) while adjacent: the player
  actor MOUNTS — the rider's map sprite becomes obj 431 (horse-with-rider) at the horse's
  position, and the horse actor is absorbed. Sherry and BehLem refuse (flavour, party-member
  gated); mounting is refused while aboard a boat, and a rider cannot mount again.
- (U)se the horse-with-rider: DISMOUNT — the rider reverts to their own sprite, and a horse
  reappears beside/at the spot (a wandering animal again).
- v1 models the AVATAR as the rider (the original mounts individual members).

## 5. u6e v1 simplifications (documented deviations)

- Raft drift and the balloon (wind system) are DEFERRED — the wind model doesn't exist yet.
- Movement COSTS (ship faster downwind, skiff slower per original cost tables) are deferred
  with the wind model; a vehicle step costs a normal turn.
- The original replaces the boat objects with a "vehicle actor 0" while aboard; u6e keeps the
  boat as its map object(s) and moves them each step — same visible result, no vehicle actor.
- Party members individually skiff-capable / multiple skiffs: not modeled; the party moves as
  one vehicle.
- Ship cannons (broadside firing) deferred.
- Combat while aboard: enemies may still act; the party cannot melee from the boat in v1.

## 6. Save + load of vehicle state (2026-09-29)

- A boat's map records are located by position in the save, so boarding registers where each
  part's record still says it is; S)ave writes the sailed position + facing frame into that
  record (and the cleared price tag of a bought boat) and re-anchors it. save_load.md §5.
- The party aboard is saved AT the boat (avatar + hidden companions all at its tile); a load
  that finds the avatar on a water tile with a skiff / a ship's centre part there re-boards it.
  Saved on a horse's tile (the ridden horse is absorbed and saved at the rider's spot), the
  party is mounted again on load; a dismounted horse's new position is saved too.
- Not yet: a boat ferried and left is fine, but boats you never boarded keep their template
  records (they never move anyway); horses other than the one you rode keep theirs.

## 6. Pack data

None needed: the pack already ships tileflags1 verbatim (U6WG section, docs/formats/tileflag.md),
so the engine tests water as `tileflags1[map_tile] & 1` (Wet) directly; the shoreline band 16..47
is a tile-number test. Surface only (vehicles never exist on dungeon levels).

## 7. Test hooks

`VEHICLE.REQ` byte modes: '1' = spawn a skiff on adjacent water, board it, sail 3 tiles along
open water, print `VEH x=.. y=..` positions; '2' = attempt to sail onto land (must refuse) and
disembark (must land); '3' = ship: board via a non-centre part, sail, verify all 3 parts moved
+ re-faced; '4' = horse mount/dismount round-trip (sprite obj_n 431 check). Each holds a frame
for capture.

## Status
VERIFIED 2026-09-20 — VEHICLE.REQ modes 1-4 on emulated TT: skiff boards/sails 3 tiles on the
castle-moat river and refuses land, disembark lands on passable ground; ship spawns on open water,
boards via its FRONT part (centre found by the §1 geometry), sails 2 with the three parts colinear
and re-faced; horse mounts (avatar sprite becomes obj 431), rides, dismounts and the horse actor
reappears. Captures: skiff + ship frames (party hidden, camera on the vehicle, message shown).
Deferred per §5: raft/balloon/wind, movement costs, cannons. Save persistence of moved boats +
re-boarding on load: DONE 2026-09-29 (§6), verified by LOAD.REQ mode '2' on TT + 4 MB Falcon.
