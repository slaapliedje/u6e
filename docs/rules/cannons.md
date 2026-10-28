# Ship cannons, hull strength and the fixed cannon (U6)

Games: Ultima VI (SE/MD have no ships).
Implemented by: /engine combat.c (the ship as a weapon), save.c (hull write-back, record
deletion), /render gamedemo.c (the shot, hull damage, a ship breaking, repair by resting, the
fixed cannon), docs/rules/vehicles.md (boarding), wind.md (sailing).

> Provenance
> - Derived from : READING ScummVM/Nuvie `actors/u6_actor_types.h` (the ship's combat type),
>   `actors/u6_actor.cpp` get_hand_combat_type, `core/player.cpp` attack_select_weapon_at_location
>   + repairShip, `core/party.cpp` can_rest, `core/effect.cpp` CannonballEffect + ExpEffect,
>   `usecode/u6_usecode.cpp` use_cannon + use_boat (hull <-> qty), `core/events.cpp` rest,
>   and the U6 actor scripts in Nuvie's `ultima.dat` (`u6/actor.lua`: weapon range / damage /
>   projectile tables, actor_hit for a ship, skiff or raft defender, the ship-destruction branch)
>   — to understand the original behaviour. Re-expressed; no code transcribed.
> - References   : docs/rules/combat.md (§2 hit roll, §3 damage, §4 damage table), vehicles.md
>   (§1 ship parts, §3 boarding, §6 persistence), docs/formats/tileflag.md (tileflags2 bit5
>   "no-shoot-through" = the missile boundary), objblk.md (records, chunk-local links).
> - Tools        : none
> - Validated by  : CANNON.REQ headless hook (§6) on emulated TT + stock 4 MB Falcon; ScummVM
>   cross-load of a save holding a broken ship (tools/savecheck.sh).
> - Confidence   : confirmed for §1-§4; §5 lists our simplifications.
> - Last updated : 2026-10-27

## 1. The ship's cannons

- Aboard a **ship** (412) the party's attack IS the cannons: A)ttack prompts
  `Attack with ship cannons-` and the **crosshair** (combat.md §6b) picks any tile within
  **range 5**; the ship is the weapon: **damage 1..30** (weapon tables, combat.md §4 / §6), the
  missile tile is the **cannonball** (tile 399), the attacker's dexterity is the SHIP's (30).
- The shot follows combat.md §6b: the hit roll against whoever stands on the aimed tile (a
  miss scatters the aim one tile), then the ball flies the line from the ship's centre and
  lands where a **missile boundary** (tileflags2 bit5, base or object tile) stops it, else on
  the aimed tile; it **bursts** where it lands (the explosion tile 382 for a moment). Whoever
  stands there takes 1..30 less armour: `Hit X!` / `Killed X!` — the vehicle, not the Avatar,
  is the slayer, so no experience is awarded (the original's exp goes to vehicle actor 0);
  the centre part of **another ship** there takes the hit as hull damage (§2); any other
  object there takes the blow as combat.md §3b (a countable one breaks).
- One shot = one action (the world advances as for any attack).

## 2. Hull strength

- A ship's **hull strength** is the centre part's record `qty` (0..100; the shipwright's new
  ships hold 100). While aboard it is the vessel's hit points.
- An enemy who attacks the party aboard a ship attacks the SHIP: the hit roll is made against
  the ship's dexterity (30), the attacker's damage roll (no armour) comes off the hull —
  `X hits the ship!`. Aboard a **skiff or raft** an enemy's hit falls on a RANDOM living party
  member instead (the vessel itself cannot be hurt).
- A **fixed cannon's** ball (§3) hitting a ship's centre part takes 20 off its hull.
- At **0 the ship breaks**: `Ship broke!` — the three ship parts are destroyed and the party
  finds itself on a **raft** (415) that is **tossed out** from the centre: the first of the tiles
  north, south, east, west that a raft can enter (water, nothing standing there), else the centre
  tile itself. The ship is gone for good: its front/back records are deleted from the save and
  the centre record is retyped into the raft and re-anchored where the raft lies (§4). A boarded
  ship whose hull is already 0 breaks at the first hit.
- **Repair**: R)est aboard a ship repairs the hull by **5** (to at most 100) and advances the
  clock 5 minutes: `Hull Strength: nn%`. Resting in any other vehicle: `Can not be repaired!`
  (the original's wording).

## 3. The fixed cannon (object 221)

- U)se an adjacent cannon: `Fire!` — a cannonball flies **5 tiles** in the cannon's facing
  (frame 0 N, 1 E, 2 S, 3 W). The first actor on the line takes a flat **32** damage (no hit
  roll, no armour); a ship's centre part on the line takes 20 off its hull; a missile-boundary
  or sight-blocking tile stops the ball with a burst; otherwise it bursts at the end.
- Aiming: a cannon is re-aimed by M)oving (pushing) it towards the wanted direction; a push along
  its facing rolls it one tile instead (usecode_use.md, M)ove).
- The starting data holds fixed cannons: the batteries around Buccaneer's Den (393..436, 722..757),
  a pair by the docks at (582,625)/(582,630), one at (307,345) by Lord British's castle and more.

## 4. Persistence

- The hull is written back into the centre record's `qty` whenever the level is reconciled
  (save_load.md §5 step 2), at the record's current position (a sailed ship is re-anchored
  first).
- A broken ship: the front and back records are **deleted** from the template in place —
  the chunk count drops by one, every in-container link in that chunk that pointed past the
  deleted index is decremented, the buffer tail moves down 8 bytes and the ScummVM trailer's
  header offset (the file's last u32) is corrected; the engine's cached objlist offset is
  recomputed. The centre record is retyped 412 → 415 (frame 0, qty 0) through the moved-object
  path (save_load.md §5). The save therefore holds one raft where the ship was.
- Destroyed objects in general: a world object marked `WOBJ_DIRTY_DESTROYED` has its record
  deleted at reconcile time (step 0, before the taken/moved passes). Only childless records
  are deleted this way (a container's children would dangle); callers destroy leaves.

## 5. u6e deviations (2026-10-27)

- The ship's ball bursts where it lands (the reference's ship shot is a plain projectile; its
  FIXED cannon bursts). The fixed cannon (§3) keeps stopping at the first actor on its line,
  as the reference's cannonball effect does.
- Another ship's centre hit by the party's cannons loses the rolled damage from its hull and
  the ship "breaks" at 0 without a raft; the reference's generic object rule would remove just
  the struck part once its count ran out.
- NPC ships never fire back: the user's objlist holds no ship actor and no egg on the surface
  hatches one (CANNON.REQ 4 counts them), so the rule is unreachable in U6's data.
- The rest refusals ("Only in the wilderness!", "Not while foes are near!" …) are rest.md §1;
  they also gate the ship's repair.

## 6. Test hook

`CANNON.REQ` byte modes:
- `'1'` ship's cannons: spawn a ship on open water and board it; hatch a hostile gargoyle 3
  tiles east (placed on the water); aim at its tile → print `CANNON hit= hp_before= hp_after=
  dist_east=` (hit or killed at distance 3); aim at the open sea 5 north → `CANNON burst_north=`
  5; aim 6 east → `Out of range!`. The held frame shows a cannonball beside the ship.
- `'2'` hull: board a ship (hull 100), let a hatched monster strike the party (enemy_attack on
  the Avatar) → `HULL after= ` < 100; set the hull to 5, strike again → `HULL broke= 1
  veh=415`, the raft lies at the centre and no ship part survives; S)ave → the written file's
  surface chunk holds a 415 record at the centre tile and no 412 record for the three parts
  (`HULL saved raft= parts=`); rest → `HULL rest= ` (+5); after the break the raft's offset
  from the centre (`HULL raft dx+5= dy+5= tossed= party on the raft=`) and why north was not
  taken (an object on that tile).
- `'4'` attacks on objects and the crosshair (combat.md §3b / §6b), on land at the start: with a
  bow, a barrel of count 1 three tiles east breaks at one arrow; a chest of count 25 drops to 15
  and breaks at the third arrow, spilling its gold; a keg (countable, not stackable in the data)
  breaks; a magically locked door is proof; 6 tiles is `Out of range!`; the crosshair walks 5
  steps with the bow and 1 with the hands; the data facts (eggs hatching ships: 0; doors with a
  count: 262 of 357; countable objects: ~400); then the nearest REAL countable object that is
  the top thing on its tile (one with contents first) is struck with the hands until it breaks:
  its record leaves the template and its contents' records are on the map at its tile; S)ave.
- `'3'` fixed cannon: place a cannon facing east with a hatched monster 2 tiles east → U)se:
  `FIXED hp_before= hp_after=` (32 less, or slain); a ship centre 3 tiles east → `FIXED
  hull= 80`.

## Status
VERIFIED 2026-10-27. CANNON.REQ on emulated TT and the stock 4 MB Falcon (identical results):
'1' aboard a ship on open sea the cannons, aimed at the gargoyle's tile 3 east, hit it on the
first shot (40 → 34 hp), a shot aimed at the sea 5 north flies the 5 tiles, 6 east is "Out of
range!", the captured frame shows the cannonball tile beside the ship; '2' the real ship at
Buccaneer's Den (600,119): hull 100 → 89 under a gargoyle's blow, R)est → 94 ("Hull Strength:
94%"; since rest.md the hook lets the gargoyle step away first — a rest with foes near is refused), at hull 5 the next blow breaks it — the party is on a raft at the centre tile, no ship
part survives — the raft is tossed SOUTH (600,120) because the north tile holds object 286 and
the party rides it, R)est there says "Can not be repaired!"; S)ave shrinks the template by 16
bytes (two records deleted), the file holds one 415 record (qty 0) within a tile of (600,119),
no 412 record there, a consistent trailer offset, and the cached objlist offset matches; '4' see
§6: barrel, chest (25 → 15 → broken, "gold coin" spilled), keg, magic door, range, crosshair,
the data facts, and a real crate (192, count 10, two yellow potions inside) broken in 3 strikes of
the hands with its record gone from the template and the potions' records on the map at its tile
(status location bits 0; ScummVM loads the written save); '3' a fixed
cannon's ball hits a gargoyle 2 tiles away for exactly 32 (50 → 18) and takes a ship centre
3 tiles away from 100 to 80. ScummVM cross-load (tools/savecheck.sh) of the broken-ship save:
"Game Loaded", the party on the raft at the dock. `make savetest` (synthetic buffer) covers the
deletion's count / link re-index / tail / trailer fix-up and the hull qty patch; `make check`
and the logic suite pass.
Found and fixed on the way: S)ave captured the template length + objlist offset BEFORE the
reconcile that may now shrink the template (a stale trailer offset in the written file).
