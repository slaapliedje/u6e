# USE verb — object usecode effects (U6)

System: object interaction ("use" command). Default = U6; Savage/Martian map to
RuleSet "use" overrides.

> Provenance
> - Derived from : READING Nuvie usecode/U6UseCode.cpp + U6objects.h (obj_n
>   numbers) + observing U6. Implemented from this spec, not transcribed.
> - References   : Nuvie U6UseCode::use_door / use_switch / object-type table.
> - Test hooks   : on-target `make m5` (use a door → it opens, tile changes).
> - Confidence   : confirmed (doors); others scoped below as TODO.
> - Last updated : 2026-06-24

## Dispatch model (IMPORTANT — not a bytecode file)
Unlike NPC conversations (CONVERSE.A/.B bytecode → our converse_vm), U6 OBJECT
use is **engine logic keyed by object number**, not a data-driven script. U6
hard-codes a table {obj_n → use handler}. We implement our own handlers from the
observed rules, behind a use dispatcher (a RuleSet seam for per-game deltas).

## Doors (IMPLEMENTED)
Object numbers: 297 oaken, 298 windowed, 299 cedar, 300 steel (334 = secret).
A door object's **frame** encodes its state (frame = tile − basetile(obj_n)):
- frame **0–3** = OPEN (the low bits are orientation/variant).
- frame **4–7** = CLOSED.

USE toggles state by ±4:
- open  → close: frame += 4 (U6 refuses if an actor/blocking object is on the tile).
- closed → open: frame -= 4.

The rendered tile is `basetile(obj_n) + frame`, so a ±4 frame change is a ±4 tile
change — opening/closing swaps the door graphic.

Locks (TODO): a door with `quality != 0` is locked and needs `OBJ_U6_KEY` (64)
of matching quality (or a lock pick, obj 63). "magically locked" doors can't be
opened normally. Our slice treats doors as unlocked (we don't yet ship per-object
quality); locked-door handling lands with the inventory/key model.

Blocking (TODO): U6 won't close a door onto an actor/object, and a CLOSED door
blocks movement. We don't block movement on object tiles yet, so closing is
always allowed and a closed door is still walkable — the visible effect today is
the graphic + message. Object-tile passability is a separate task.

## Containers (IMPLEMENTED 2026-07-02 — objblk.md §2 has the data spec)
Container types: chest 98, backpack 99, remains 168, drawer 176, desk 177,
barrel 186, bag 188, basket 191, crate 192, dead body 339. Behavior (verified
vs ScummVM/Nuvie U6UseCode::use_container / search):
- **U)se a chest/crate/barrel** toggles its open/close frame (0 = open, 1 =
  closed; tile = base+frame). OPENING also searches it. Chest frames beyond
  that: **2 = LOCKED, 3 = MAGICALLY locked** (chest only — crates/barrels
  have no lock frames). SEARCH applies to a chest/crate/barrel only when
  OPEN (frame 0); bags/drawers/desks/bodies search at any frame.
- **KEYS + LOCKS (verified vs Nuvie U6UseCode)**: the KEY is obj 64; a lock's
  **key id is the locked object's `quality`**, and a key opens it iff the
  key's own quality matches. U)sing a locked chest (frame 2) while carrying
  the matching key UNLOCKS it (frame 2→1, "unlocked"); no key / key id 0 →
  "No effect". A magically locked chest (frame 3) needs magic. DOORS
  (obj 297–300): frames 0–3 open, 4–7 closed, **9/11 locked**, **13/15
  magically locked**; lock/unlock = frame ±4. U)se on a locked door with
  the matching key → "unlocked" (frame −4 → closed); without → "locked";
  magically locked → "magically locked". Lockpick = obj 63 (not implemented).
- **SEARCH** ("Searching here, you find a, b, c." / "…nothing.") moves every
  DIRECT child out of the container onto the container's map tile, where it
  can be G)otten. Chained containers spill intact (a bag from a chest keeps
  its own contents and can be searched in turn).
- **L)ook at a world container** searches it too (authentic U6: looking is
  how you loot corpses/bags on the ground).
- **G)et takes the whole stack**: stackables carry a 16-bit quantity
  (qty16, objblk.md §2); "You get 30 gold." Inventory merges stacks by obj_n.
Validated headlessly (COMBAT.BIN byte 14): an unlocked castle chest opens
(frame 1→0), spills its 3 items (tile count 1→4) incl. a 5-gold stack, G)et
banks 5 gold as one inventory stack, U)se closes it (0→1); a locked chest
refuses; a dead body containing 150 gold loots via search.

## Lockpicks (IMPLEMENTED)
A **lockpick** (obj 63) in inventory opens any non-magically-locked door (frame 9/11)
or chest (frame 2) — no matching key needed: it drops the lock like the key path
(door frame −4 → closed-unlocked; chest frame 2→1). A *successful* pick doesn't break
the pick in U6, so nothing is consumed (and no save concern). Magically-locked
(door 13/15, chest 3) still refuse. Validated headless (USEFX.BIN): a forced-locked
door + a lockpick → frame 9→5 (picked).

## Food / drink (IMPLEMENTED — consume + hunger; see the Food/hunger section above)
USE on a food/drink object **consumes one** (`qty−1`, or the object is removed at
qty 0) with a "You eat/drink the X." message. U6's hunger/nutrition model is not yet
implemented, so there is no HP/hunger change — this is the authentic consume behaviour
(eaten food is not persisted to the save yet, so it reverts on reload — acceptable).
The food/drink obj_n set is **derived from the shipped LOOK name table** (clean-room),
NOT cross-checked against Nuvie's `use_food` registration (no source on hand) — so it is
*probable*, not exhaustive:

| Kind  | obj_n                                              |
|-------|----------------------------------------------------|
| eat   | 95 grapes, 96 butter, 128 bread, 129 meat-portion, 130 rolls, 131 cake, 132 cheese, 133 ham, 209 meat, 210 ribs, 265 fish, 335 egg |
| drink | 115 wine, 116 mead, 117 ale                        |

Deliberately excluded: reagents (67 garlic, 70 nightshade), potions (275 blue potion —
own effect), and containers (jars/buckets/vases/casks). Validated headless (USEFX.BIN):
a loaf (obj 128, qty 2) → qty 2→1 on first eat, removed on the second.

## Levers / switches / cranks (IMPLEMENTED)
Actuators: **lever 268, switch 174, crank 288, pull-chain 419**. USE toggles the
actuator's own frame and drives its linked target. The LINK was derived clean-room
from the world objects (a MECHDUMP dump grouped every mechanism by quality):

- **Quality link (CONFIRMED, 1:1).** Every quality Q≥1 in the world is exactly one
  **lever** + one **force field** (51/175/… — force/electric/fire/poison/protection/
  sleep). USE a lever/switch of quality Q toggles ON/OFF every field of that quality
  (they may be far apart — a lever here drops a field across the map). Validated
  (LEVER.BIN): quality-1 lever @(303,383) toggles the quality-1 force field @(503,359)
  alive 1→0→1 (reversible).
- **Positional link (portcullis).** Cranks/pull-chains are all quality 0, clustered
  beside the portcullis (310) they raise; a quality-0 actuator raises/lowers the
  NEAREST portcullis within 5 tiles (frame 0↔1). Validated (LEVER.BIN): a crank raises
  the portcullis 4 tiles away (frame 1→0). Heuristic (position, not an explicit link) —
  refine if a counter-example turns up.

- **Drawbridges (269).** A crank/pull-chain also raises/lowers a nearby drawbridge span
  (all obj-269 tiles within 8 tiles): each tile toggles between frames **6-8 (raised,
  impassable)** and **9-11 (down, passable)** — the ±3 pair derived from the tiles'
  tileflags1 passability (basetile 914: frames 6-8 Impassable, 9-11 clear). build_blocked
  updates crossing. Validated (DBTEST.BIN): a crank lowers an adjacent bridge frame 6→9.

Persistence: portcullis + drawbridge frames persist (sav_patch_obj_frame, like a door);
the field on/off and the actuator's own frame are NOT yet saved, so they revert together
on reload (consistent).

## Food / hunger (IMPLEMENTED)
USE on a food object consumes one and restores the Avatar's food level (`g_afood[]`,
eat +40 / drink +15, cap 255). The hunger model: each party member's food ebbs by 1
every 4 turns; at the `Hungry` threshold (40) a "X is hungry." warning fires; at 0 the
member starves, losing 1 HP every 8 turns ("X is starving!"). The ztats screen shows a
Food row (Full/Fed/Hungry/Starving). Not yet save-backed (begins Full each load).
Validated (HUNGER.BIN): food 5 → 0 over turns → Avatar loses 3 HP → eat a loaf → food 40.

## Secret doors, passthroughs, fire devices, torches, powder kegs (IMPLEMENTED 2026-10-26)

> Derived from: READING ScummVM/Nuvie `usecode/u6_object_types.h` (which objects have handlers),
> `usecode/u6_usecode.cpp` use_secret_door / use_passthrough / use_firedevice (+ message) /
> torch, light_torch, extinguish_torch / use_powder_keg, `actors/u6_actor.cpp`
> handle_lightsource, and the U6 scripts in Nuvie's `ultima.dat` (`u6/init.lua` use_keg /
> explode_keg / explode_obj / explode_surrounding_objects, `u6/actor.lua` the lit-torch
> burn in advance_time), `core/effect.cpp` ExpEffect (the 16 blast rays within 2 tiles) — to
> understand the behaviour; re-expressed, no code transcribed. Object counts from the user's
> own data (the pristine save): 40 secret doors (9 in dungeons), 16 passthroughs (surface),
> 17 torches on the map, 79 powder kegs (26 in dungeon levels 2-4).

- **Secret door (334).** Two orientations: frames 0 / 2 are HIDDEN (they look like wall, and
  pass as their tile says), 1 / 3 are open. L)ook at a hidden one reveals it: "You see a secret
  door." and the frame goes 0→1 / 2→3 (the original's Search). U)se toggles it: an open one
  closes (frame −1), a hidden or closed one opens (frame +1). Passability follows the frame's
  tile; the frame persists like a door's.
- **Passthrough (278 vertical, 280 horizontal).** Frame < 2 = closed. U)se OPENS it: the object
  moves ONE tile up (278) / left (280) and takes frame 2 — "Open the passthrough."; U)se again
  CLOSES it: it moves back down / right and takes frame 0 — "Close the passthrough.". Refused
  ("Not now!") when an actor stands on the tile it would move to. The moved object re-anchors
  its save record like a sailed boat.
- **Fire devices.** Fireplace (164): frames 0↔1 and 2↔3 (odd = lit). Candle (122), candelabra
  (145), brazier (206): frame 0↔1; a brazier at frame 2 is a holy flame and cannot be doused.
  U)se toggles: "<name> is lit." / "<name> is doused.". The lit frame's tile is the light
  emitter (tileflags2 bits 0-1), so the lighting follows the frame at once.
- **Torch (90).** U)se a torch from the pack: one torch is READIED into a free hand (ARM, else
  ARM2 — "No free hand to hold the torch." when both are taken) and lit: "Torch is lit."; the
  holder sheds light level 3 (the brightest emitter class) while it burns. It burns for
  200 turns of fuel, losing one each turn with probability 1/2 (≈400 turns): "A torch burned
  out." — the torch is gone. U)se the pack's torch again while one is lit, or unready the lit
  torch: it is extinguished and used up (the original destroys an extinguished torch).
  U)se a torch lying on the map lights it in place (frame 1: a light source on the ground);
  U)se it again and it burns out (removed). Only the Avatar's torch is modelled (companions
  and NPCs do not light theirs).
- **Powder keg (223).** U)se a keg (on the map or carried): "Powder lit!" — frame 1 and a
  3-turn fuse (one fuse at a time: "Not now" while one burns; a lit keg: "No effect"). When the
  fuse runs out every lit keg on the level explodes (a carried one at the party's feet): the keg
  is gone, a fireball (tile 393) bursts, every actor within 2 tiles (the 5×5 — probable, the
  original throws 16 rays 2 tiles out) takes a flat 1..60 — the party included; then within 2
  tiles every other keg explodes too (chain) and every door (297-300) that is not magically
  locked (frame ≤ 12) is destroyed: "The door is blown up!". Destroyed kegs and doors are
  deleted from the save (cannons.md §4).
- **Persistence.** Frames (secret doors, fire devices, lit map kegs, lit map torches) persist as
  object state; moved passthroughs re-anchor; destroyed objects are deleted. v1 does not save a
  readied torch's lit state or fuel (it is unlit after a load — relight it) nor the 3-turn fuse.

Test hook `DUNGUSE.REQ`: '1' secret door: the nearest hidden one — L)ook reveals (frame +1,
passable?), U)se closes, U)se opens; '2' passthrough: U)se moves it up/left + frame 2, U)se
moves it back; '3' fire device: U)se a fireplace/candle toggles its frame and its tile's emitter
level; '4' torch: give a torch, U)se from the pack → readied + lit (fuel 200), the Avatar's cell
is lit to the level-3 base, 600 turns later it has burned out and the hand is free; '5' keg: a
keg beside a spawned door with a gargoyle 2 tiles away — U)se, three turns → keg and door gone,
the gargoyle hurt, the message printed.

Status: VERIFIED 2026-10-26 — DUNGUSE.REQ 1-5 on emulated TT and the stock 4 MB Falcon (identical):
'1' the nearest hidden secret door (on passable floor tile 210) blocks while hidden, L)ook reveals
it ("You see a secret door.", frame 1, now passable), U)se closes (frame 0) and opens again;
'2' the vertical passthrough at (325,399) opens to (325,398) frame 2 and closes back to (325,399)
frame 0 — found by type, since a floor object shares its tile; '3' a brazier: "brazier is doused."
(frame 0, emitter 0) then "flaming brazier is lit." (frame 1, emitter 3); '4' a pack torch: "Torch is
lit.", fuel 200, in hand, one torch fewer in the pack, burned out after 411 turns with the hand
free; '5' a lit keg: "Powder lit!", three turns later the keg and the door beside it are gone,
"The door is blown up!", the gargoyle two tiles away hurt (200 → 194), the party 3 tiles away unhurt.
Found on the way: object blocking used Impassable|Wall; the reference blocks on Impassable only, and
the Wall-only tiles are doorways, arches and the open secret door frames — fixed (tileflag.md).

## Other use handlers (TODO — same dispatcher, future slices)
- Magic lock/unlock spells; persisting field/consumed-item/hunger state to the save.
- Readables (signs/books/tombstones) → show text — DONE (docs/formats/strings.md;
  L)ook / U)se on a readable pages its text). Clock/sundial time-display TODO.
- Ladders/holes/stairs → change level. Needs the multi-level world model.

## Status: IN PROGRESS (doors + containers/stacks + keys/locks + LOCKPICKS + FOOD/HUNGER +
## readables + LEVERS/SWITCHES/CRANKS + DRAWBRIDGES + ladders/holes/caves (map_levels.md) +
## SECRET DOORS / PASSTHROUGHS / FIRE DEVICES / TORCHES / POWDER KEGS done)
