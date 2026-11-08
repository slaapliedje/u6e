# USE verb — object usecode effects (U6)

System: object interaction ("use" command). Default = U6; Savage/Martian map to
RuleSet "use" overrides.

> Provenance
> - Derived from : READING Nuvie usecode/U6UseCode.cpp + U6objects.h (obj_n
>   numbers) + observing U6. Implemented from this spec, not transcribed.
> - References   : Nuvie U6UseCode::use_door / use_switch / object-type table.
> - Test hooks   : on-target `make m5` (use a door → it opens, tile changes).
> - Confidence   : confirmed (doors); others scoped below as TODO.
> - Last updated : 2026-11-05

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

A door often shares its tile with a **doorway** arch (301) that the object list holds above it;
U)se on that tile works the door (the arch has no use). Found 2026-10-30 by the bot harness: the
castle door at (312,370) answered "Nothing happens with the doorway." and never opened.

USE toggles state by ±4:
- open  → close: frame += 4 (U6 refuses if an actor/blocking object is on the tile).
- closed → open: frame -= 4.

The rendered tile is `basetile(obj_n) + frame`, so a ±4 frame change is a ±4 tile
change — opening/closing swaps the door graphic.

Locks: frames **8-11 = LOCKED** (a key of matching `quality`, obj 64, or a lockpick 63 opens
them: frame −4 → closed) and **12-15 = MAGICALLY locked** (Unlock Magic: frame −8 → closed; Magic
Lock puts a closed door there: +8). The data only uses the odd frames (9/11 and 13/15 — the two
door orientations), the engine treats the whole ranges alike. A magically locked door survives a
keg blast; the others are blown up.

**Traps** (magic_skills.md §5f): a door or chest may hold a child "effect" object (337). Opening it
(USE on a closed door / a closed chest) fires the trap on the Avatar and removes it: Acid (1..20
damage), Poison, Bomb (1..20 fire damage to everyone within the 21-cell area) or Gas (everyone within
the area poisoned). The Trap / Untrap / Detect Trap spells add, remove and sense them. Six traps ship
in the pristine save (chests + steel doors); a removed one is deleted from the save record.

Blocking (TODO): U6 won't close a door onto an actor/object, and a CLOSED door
blocks movement. We don't block movement on object tiles yet, so closing is
always allowed and a closed door is still walkable — the visible effect today is
the graphic + message. Object-tile passability is a separate task.

## Containers (IMPLEMENTED 2026-07-02 — objblk.md §2 has the data spec)
Container types: chest 98, backpack 99, remains 168, drawer 176, desk 177,
barrel 186, bag 188, basket 191, crate 192, dead body 339, and the remains that
carry a slain creature's kit (eggs.md §3c; added 2026-11-01 from the reference's
object-type table): dead animal 211, dead cyclops 340, dead gargoyle 341, reaper
347, deer 350, mouse 354, drake 369, mongbat 372 (searched at any frame, like a
body). Behavior (verified vs ScummVM/Nuvie U6UseCode::use_container / search):
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
  (qty16, objblk.md §2); "You get 30 gold coin." Inventory merges stacks by obj_n.
  **Gold coins (88) join the party's gold** (the count merchants take from and the save
  writes back as coins), never a separate pack stack (CORRECTED 2026-11-01: U6 used to put
  them in the pack, where no merchant or save could see them).
Validated headlessly (COMBAT.BIN byte 14): an unlocked castle chest opens
(frame 1→0), spills its 3 items (tile count 1→4) incl. a 5-gold stack, G)et
adds 5 to the party's gold, U)se closes it (0→1); a locked chest
refuses; a dead body containing 150 gold loots via search.

## Lockpicks (IMPLEMENTED)
A **lockpick** (obj 63) in inventory opens any non-magically-locked door (frame 9/11)
or chest (frame 2) — no matching key needed: it drops the lock like the key path
(door frame −4 → closed-unlocked; chest frame 2→1). A *successful* pick doesn't break
the pick in U6, so nothing is consumed (and no save concern). Magically-locked
(door 13/15, chest 3) still refuse. Validated headless (USEFX.BIN): a forced-locked
door + a lockpick → frame 9→5 (picked).

## Food / drink (REFERENCE-FAITHFUL 2026-10-28 — no hunger model)

> Provenance: READING ScummVM/Nuvie `usecode/u6_usecode.cpp` use_food, `core/player.cpp` (alcohol,
> moveRelative), the U6 `actor.lua` hourly block, `core/obj_list.h` — to understand; re-expressed.

- U)se on a food or drink object **consumes one** (a stack loses one, a single object is gone) and
  prints **`You eat the food.`** for food or **`You drink it.`** for wine, mead and ale. Nothing else:
  **U6 has no hunger, nutrition or healing from food** — the engine's earlier "Food" stat (ebbing
  food, "X is hungry.", starvation, the ztats Food row) was an invention and is REMOVED.
- **Drink**: each drink adds one to the party's **alcohol** count (objlist byte **0x1c17**, saved and
  loaded). Above **3**, three Avatar steps in four go astray: the step is replaced by a random
  direction (which may be no move at all) and **`Hic!`** is printed. The count drops by one **every
  game hour** (the reference's hourly actor tick).
- A world food object eaten from the map is persisted at once: its record's count drops, or the
  record is deleted when the last one is eaten (save_load.md §5 step 0). Pack food is already
  covered by the inventory rebuild (§6).
- The food/drink obj_n set is derived from the shipped LOOK name table (clean-room), not
  cross-checked against the reference's use_food registration — *probable*, not exhaustive:

| Kind  | obj_n                                              |
|-------|----------------------------------------------------|
| eat   | 95 grapes, 96 butter, 128 bread, 129 meat-portion, 130 rolls, 131 cake, 132 cheese, 133 ham, 209 meat, 210 ribs, 265 fish, 335 egg |
| drink | 115 wine, 116 mead, 117 ale                        |

Deliberately excluded: reagents (67 garlic, 70 nightshade), potions (275 — own effect), and
containers (jars/buckets/vases/casks). Hook: PERSIST.REQ '4' (below).

## Levers, switches, cranks, pull-chains (REFERENCE-FAITHFUL 2026-10-28)

> Provenance: READING ScummVM/Nuvie `usecode/u6_usecode.cpp` use_switch, use_crank,
> drawbridge_find / _remove / _open / _close, use_bell + bell_find — to understand; re-expressed and
> CONFIRMED against the user's own data (the scan below). The earlier "quality lever ↔ quality force
> field" and "nearest portcullis / ±3 frame drawbridge" rules were our heuristics and are gone.

- **Lever (268) and switch (174)**: the actuator's **quality Q names DOORWAYS (301) of quality Q on
  the same level**. U)se toggles the actuator's own frame (0↔1, persisted as a state patch) and, at
  EVERY such doorway's tile: if the **barrier** object stands there it is **destroyed**, else one is
  **created** on the doorway's tile — a lever's barrier is a **portcullis (310)** with the doorway's
  frame (a frame-9 doorway gets a frame-1 portcullis — the reference's "cream buildings" fix-up),
  a switch's barrier is an **electric field (175)** frame 0; both quality 0. Messages: lever
  `Switch the lever, you hear a noise.` / `Switch the lever, strange, nothing happened.` (no doorway
  of that quality), switch `Operate the switch, you hear a noise.` / `Operate the switch, strange,
  nothing happened.`
- The Covetous switch (quality 113 at (139,0) level 1) first gives the doorway at (160,3) level 1
  quality 113 (the reference's hack: the data has it at quality 0), then works as any switch.
- Data (the user's save): 71 levers, qualities 1-73, each with a doorway of its quality except
  lever 46 at (116,53) level 4; 18 switches 101-113, each matched except 113 (the hack above). All
  73 portcullises and all 13 electric fields in the world stand on doorway tiles (frames: a frame-1
  doorway carries a frame-1 portcullis 47×, frame-3 → 3 24×, frame-9 → 1 2×). The force fields (51,
  qualities 0-7) are NOT lever targets (they are the shrine mechanism).
- **Crank (288)**: drives a **drawbridge (269)**. Find the bridge as DOS does (see "Crank: which bridge"
  below, seen in DOS 2026-11-05; the position search this line used to give is gone): from a
  closed row's left end (frame 6) the open rows start one tile right of it; from a top open row's
  left end (frame 3) they start there. The bridge is OPEN if that piece has frame 3. Remove every piece: the end piece at (x−1,y), then rows from y
  downward, each row from x rightward until a tile without a piece (the first row's length = the
  width W). Then **open** (`Open the drawbridge.`): rows from y+1 down — frame 3 at x, frame 5 at
  x+W−1, frame 4 between — until the tile below the row is passable map, then a last row of frame 1
  between frame 0 (x) and frame 2 (x+W−1); or **close** (`Close the drawbridge.`): one row at y−1:
  frame 6 at x−1, frame 7 at x..x+W−2, frame 8 at x+W−1. The crank's frame goes 0→3 on opening and
  back on closing (the reference animates it 3 frames); the chain (293) beside the crank only
  animates. Every piece created or removed is persisted (a new or deleted record).
- Data: the castle bridge — cranks (303,384) and (311,384), chains (304,384)/(310,384), the closed
  row at y=385: frame 6 at 303, 7 at 304..309, 8 at 310 (W = 7); a second bridge at (785..788,959)
  with cranks at 785 and 789. Exactly the shape the close rule produces.
- **Pull-chain (419) = a bell pull**: U)se rings the **bell (236)** within 8 tiles (the reference
  animates the bell and plays a bell sound; we have neither a bell sample nor a message — the turn
  passes silently). It is NOT a mechanism. Data: 6 pull-chains, bells at (126,12)/(128,12) level 4
  and (360,397).
- Passability follows the objects: a portcullis / electric field / raised bridge piece blocks by
  its tile flags; `build_blocked` runs after each change.
- Hook: PERSIST.REQ '1' (lever/switch), '2' (crank), below.

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
  pass as their tile says), 1 / 3 are open. L)ook at a hidden one reveals it: "Searching here, you find a secret
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
it ("Searching here, you find a secret door.", frame 1, now passable), U)se closes (frame 0) and opens again;
'2' the vertical passthrough at (325,399) opens to (325,398) frame 2 and closes back to (325,399)
frame 0 — found by type, since a floor object shares its tile; '3' a brazier: "brazier is doused."
(frame 0, emitter 0) then "flaming brazier is lit." (frame 1, emitter 3); '4' a pack torch: "Torch is
lit.", fuel 200, in hand, one torch fewer in the pack, burned out after 411 turns with the hand
free; '5' a lit keg: "Powder lit!", three turns later the keg and the door beside it are gone,
"The door is blown up!", the gargoyle two tiles away hurt (200 → 194), the party 3 tiles away unhurt.
Found on the way: object blocking used Impassable|Wall; the reference blocks on Impassable only, and
the Wall-only tiles are doorways, arches and the open secret door frames — fixed (tileflag.md).

## Well, cow, churn, fishing pole, sextant, potions, runes, peer gem (IMPLEMENTED 2026-10-27)

> Derived from: READING ScummVM/Nuvie `usecode/u6_usecode.cpp` fill_bucket / use_well / use_cow /
> use_churn / use_fishing_pole / use_sextant / use_potion (+ the potion colour order) / use_rune
> (+ the eight mantras) / remove_gargoyle_egg / use_peer_gem, `core/effect.cpp` PeerEffect — to
> understand the behaviour; re-expressed, no code transcribed. Object presence checked in the
> user's own data (9 wells, 2 churns, 13 buckets, 62 potions, 8 shrine force fields, 3 runes on
> the map and 3 carried by NPCs, 13 gems).

- **Bucket (178) / well (233) / cow (actor 428) / churn (181).** U)se a well or a cow with an
  empty bucket in the pack: the bucket becomes a bucket of water (179) / of milk (180) — "Done";
  without one: "You need a bucket." (or "You need an empty bucket." when only full ones are
  carried). U)se a churn with a bucket of milk: butter (96) plus the empty bucket back — "Done";
  else "You need some milk.".
- **Fishing pole (264).** U)se it standing next to water: a 1-in-5 chance of a fish (265) into
  the pack; away from water "You need to stand next to water.".
- **Sextant (93).** U)se on the surface or in the gargoyle world: the position as
  `lat{N|S, lon{E|W` (the font's `{` is the degree sign): surface x/8 and y/8 against the
  meridians 38 / 45, the gargoyle world x/2, y/2. Elsewhere "Not usable".
- **Potions (275, frame = colour).** U)se asks "On whom:" — a party member by number 1..5 (only
  the party can be dosed). blue 0 wakes a sleeper, red 1 cures poison, yellow 2 heals to full,
  green 3 poisons, orange 4 puts to sleep, purple 5 protects, black 6 makes invisible, white 7 gives the X-ray view and prints `Done`; nothing else is printed (seen in DOS 2026-11-05 for white, black, purple and orange; superseding the next words), white 7
  (the original's x-ray vision) has no effect here. The potion is used up. A dead companion
  (0 HP, combat.md §3d) cannot be dosed: "No effect", and the potion is kept.
- **Runes (242..249 = the eight virtues, Honesty first).** U)se asks "Mantra:" — a typed word.
  The right mantra (Honesty AHM, Compassion MU, Valor RA, Justice BEH, Sacrifice CAH, Honor SUMM,
  Spirituality OM, Humility LUM) while standing next to the shrine's force field (51, quality =
  the rune's index) removes the field for good — "Done!" — and silences any gargoyle egg within
  20 tiles (the shrine guardians); elsewhere "No effect!"; a wrong word "Wrong mantra!".
- **Peer gem (77).** U)se: the map window shows the 48×48-tile area around the party (aligned
  to the 8-tile chunk, each tile one 3×3 block of its centre colour, the party a white dot)
  until a key is pressed; the gem is used up.

Persistence: a removed shrine field is deleted from the save; a silenced egg is only silenced
for the session (v1); buckets/butter/fish are ordinary pack items.

Status: VERIFIED 2026-10-27 — SMALLUSE.REQ 1-8 on emulated TT and the stock 4 MB Falcon (identical):
'1' a well fills the bucket (water 1, empties 0), the churn refuses without milk then makes butter
and returns the bucket, a cow beside the party fills it with milk (the actor on a tile is tried
before the tile's objects — a table under the cow had been answering first); '2' no water at the
start, on a shore a fish on the fifth cast; '3' the sextant says 1{N, 0{W at (307,352); '4' the
eight potions on the Avatar: poisoned → cured → healed to full → asleep → awake → protected →
invisible → white "No effect"; '5' the Honesty shrine's field (quality 1 nearest the start): "Wrong
mantra!" leaves it, the right mantra typed in lower case removes it — "Done!"; '6' the gem is used
up and the overview drawn; '7' a lit torch (fuel 123) and a 2-turn keg fuse come back through
S)ave + load with the torch in hand; '8' with NPCs 44 and 46 moved beside Culham the VM's
NPCNEARBY sees them and the brawl sets 0x99 on 44, 46, 45 and 50.

## Torch and keg state across a save (2026-10-27)
A lit readied torch's record is saved with frame 1 and its fuel in qty, and relit with that fuel
on load (every party member's); the keg fuse (objlist 0x1c4b, u16) is saved and restored, and a lit
keg carried in the pack is saved as a 223 frame-1 record in the party's inventory (the reference's
explode_keg looks for exactly that) and comes back as the carried fuse. A shrine egg silenced by a
mantra has its hatch chance (the egg record's qty byte) written as 0, so neither our load nor
ScummVM's hatches it again.

## M)ove — pushing objects and actors (IMPLEMENTED 2026-10-27; containers + Support tops 2026-10-27)
> Derived from: READING Nuvie `core/events.cpp` push_start / pushTo (both forms),
> `gui/widgets/map_window.cpp` can_drop_or_move_obj + blocked_by_wall, `core/obj_manager.cpp`
> (the weight table, can_get_obj, can_store_obj, moveto_container), `core/obj.cpp` (add_and_stack),
> `usecode/u6_usecode.cpp` use_cannon (MOVE) and amulet_of_submission — to understand; re-expressed.
> Weights + Support flag: docs/formats/tileflag.md (the pack's U6WB / U6W3 sections). Container
> records: docs/formats/objblk.md §2.

- **M**, then a direction (or a click on a neighbouring tile), picks the thing to move: an ACTOR on
  that tile first, else the topmost object; nothing there → "what?". The original selects with a
  free cursor anywhere in the window; u6e moves what stands beside the party.
- **Unmovable**: a type whose WEIGHT (tileflag block 0x1000, by obj_n) is 0 — fixed scenery,
  statues (the "255" class — big furniture — IS movable, just not gettable) — and dragons (411):
  "Not possible". Ship parts (412) are sailed, not pushed.
- Then **To-** a direction: the thing moves ONE tile from where it stands (the original pushes one
  space at a time). The direction is echoed ("North.").
- **An object**:
  - standing on a WALL map tile (tileflags1 Wall bit) that is not a door (297..300) → "Blocked."
    (the original also reads the wall's facing bits; u6e refuses any wall tile);
  - the target tile must be free of actors (the party included) → else "Blocked.";
  - a skiff / raft (414/415) goes onto open WATER only, never onto a tile holding an object →
    "Not possible";
  - the target holds an object, and that object TAKES the pushed thing (see *Holders* below) →
    the thing goes INSIDE it and leaves the map (no further message);
  - else the target's topmost object must have a **Support** tile (tileflags3 bit1: tables, end
    tables, the cutting table, stone tables, book stands, altars, a CLOSED crate) — or be passable
    with neither its own tile nor the map tile a boundary (a rug, a floor plaque, a chest of drawers,
    a spellbook) → the thing lands ON TOP (it becomes the tile's topmost object: drawn over it,
    picked first by G)et / L)ook); otherwise "Not possible";
  - a bare map tile: water takes nothing (but a boat); otherwise Support, or passable and not a
    boundary → the thing moves; else "Not possible". (No map tile carries Support in U6; the rule is
    kept for symmetry with the original.)
  - a **cannon** (221) pushed along its facing rolls one tile; pushed any other way it is RE-AIMED
    to that direction and stays put (use_cannon's MOVE rule);
  - success: the object moves (its record is relocated at S)ave through the moved-object path, like
    a passthrough's) and the turn passes.
- **Holders** (the original's can_store rule for a container ON THE MAP): the pushed thing must be
  gettable, never a trap (173 / the 337 trap effect), never a chest / barrel / crate (98/186/192);
  the holder takes it when it is a bag (188), a backpack (99), a basket (191), an OPEN crate (192
  frame 0), an OPEN barrel (186 frame 0) or an OPEN chest (98 frame 0); a spellbook (57) takes a
  spell (58) it does not hold yet (a spell of that quality, or one of quality 255 = all spells,
  already inside refuses); the vortex cube (62) takes a moonstone (73). Anything else — a closed
  chest, a drawer, a desk, a body — is not a holder on the map (the original opens those only from
  an inventory / a gump). A stackable thing joins a stack of the same type, frame and quality
  already inside (counts add, the pushed stack is gone). A pushed bag goes in WITH its contents.
  Searching the holder spills the pushed things like any contents.
- **Persistence**: the move into a holder is carried into the save template at once (after a
  reconcile of pending takes / drops / moves, so a thing taken and dropped this session has an
  on-map record to move): the thing's record, with every record contained in it, leaves its chunk
  and is appended to the holder's chunk as an in-container record linked to the holder (objblk.md
  §2: links are chunk-local, contents follow their holder) — across super-chunks when the two
  differ; a merged stack patches the inside stack's count and deletes its own record; a holder
  without a record (a runtime object: a Replicated copy, a body) takes the record away with it.
  Our load and ScummVM's see the thing inside. Up to 64 records travel in one push.
- **An actor**: never the Avatar; a seated actor (on a chair or throne — the original's
  can_move=false) → "Failed."; the **strength test** with `S` the pushed actor's strength and
  `P` the pusher's (each 3 less when cursed): `(S/2 + 30 − P)/2 > rand(1..30)` → "Failed."; the
  target tile must be passable and free of blocking objects and actors → else "Blocked."; success
  moves the actor one tile (a companion too) and the turn passes. (The original charges 5
  movement points; u6e's turn model spends the turn.)
- Mouse: the first click selects a neighbouring tile; the second click's tile RELATIVE TO THE
  pushed thing gives the direction (sign-clamped to one step).
- Deviations: the original refuses a target it cannot see (a dark tile) and tests the line from the
  pusher to the target for missile boundaries; u6e checks the wall under the pushed thing only. A
  target more than one tile away is "Out of range!" (u6e pushes what stands beside the party).
- Test hook `PUSH.REQ`: '1' a chair placed east of the Avatar pushed east moves one tile; a doorway
  (weight 0) is "Not possible"; a creature on the target → "Blocked."; '2' a cannon facing east
  pushed north is re-aimed (frame 0, same tile), pushed north again it rolls; '3' a hatched dog
  (weight 110) of strength 1 pushed east moves, of strength 200 → "Failed.", a gargoyle (weight 0)
  → "Not possible"; '4' the nearest real chair pushed one tile and S)aved → its record sits at the
  new tile and none at the old; '5' a chair pushed onto a table / a closed crate lands on top as
  the topmost object, onto a cask → "Not possible"; '6' gold pushed into an open chest goes inside,
  a second gold stack merges (5+7 = 12), two keys of different quality stay apart, a closed chest
  and a chest-into-chest refuse, a search spills the three; a key lands ON a spellbook, a spell goes
  in, a spell it holds stays out; the vortex cube refuses a key and takes a moonstone; '7' REAL
  objects: the nearest real chest (opened) takes the nearest real gettable item, a real bag WITH
  contents from another super-chunk, and an item taken and dropped this session — each time the
  template shows the record under the chest and none at the old place; then S)ave.
  VERIFIED 2026-10-27, modes 1-4 identical on emulated TT and the stock 4 MB Falcon (the pack's
  weights: chair 74, doorway 0, cannon 255, throne 0, dragon 130, gargoyle 0).
  Modes 5-7 VERIFIED 2026-10-27, identical on emulated TT and the stock 4 MB Falcon: the table / closed
  crate take the chair on top (it is the topmost object), the cask refuses; gold 5+7 merges to 12, the
  chest holds 3, a search spills 3; the real chest at (293,351) takes a real item (124), a real bag
  (188, with 6 meat) from another super-chunk and a taken-and-dropped item (27) — the written save
  (host check: 1710 in-container records, 0 bad links; the chest record lists all three, the bag's
  meat under the bag) loads in ScummVM.

## Staff, vortex cube, white potion, holy flames, NPC torches (IMPLEMENTED 2026-10-27)
> Derived from: READING Nuvie `usecode/u6_usecode.cpp` use_staff / use_vortex_cube / use_potion
> (white) / holy_flame, `actors/u6_actor.cpp` handle_lightsource + `actors/actor_manager.cpp` (its
> hourly call), `core/effect.cpp` U6WhitePotionEffect, the U6 script `u6/actor.lua` (party torches
> burn, NPC torches do not) — to understand; re-expressed.

- **Staff** (78) U)se: it holds CHARGES (336, one per stored spell; a charge's quality is the
  spell number): one charge is consumed and that spell is cast at once WITHOUT mana or reagents (a
  targeted spell asks the direction as C)ast does); no charge left → "Nothing happens.". Our pooled
  pack flattens containers, so the staff's charges are pack items, and the pack list holds unreadied
  items only, so the staff is used from the pack (the original's "Not readied." gate is dropped;
  deviation). The staff at (915,410) in the starting data holds five charges.
- **Vortex cube** (62) U)se: the endgame. Needs the Codex (59, quality 128) within 11 tiles on the
  current level, the Britannian lens (394) at (921,851) and the gargoyle lens (396) at (925,851) on
  the surface, and all EIGHT moonstones (73, frames 0..7) carried along with the cube (the pool
  stands for the cube's contents) → "The Codex has vanished!": the Codex is removed (destroyed in
  the save) and the game is won — the ending cutscene plays (docs/design/ending.md; without its
  assets a short notice in our own words) and a key returns to the desktop. Else "No Effect!".
- **White potion** (275 frame 7): x-ray sight — see through walls for 30 turns (the X-ray spell's
  effect and duration, magic_skills.md §5e; the original times it in real seconds).
- **Holy flames**: L)ook at a lit brazier (206 frame 2) of quality 1/2/3 says "The flame of
  truth / love / courage." instead of the object's name (the shrines' braziers; quality 0 or >3 is
  an ordinary brazier).
- **NPC torches**: every hour (and after a load or a level change) each actor OUTSIDE the party
  with a torch readied lights it when the hour is before 6 or after 18, during an **eclipse**
  (timer 15, magic_skills.md §5f), or when the ACTOR's own level is other than the surface and
  level 5 (the gargoyle world), and puts it out otherwise; NPC torches never burn down. The check
  runs on the hour, as the reference's schedule tick does: an eclipse cast mid-hour lights their
  torches only if it still runs at the next hour, and a torch lit during it stays lit until the
  first hour after it ends. A COMPANION's lit torch
  burns down like the Avatar's (1-in-2 per turn) and is saved with its fuel. Every lit torch on the
  level lights its bearer's tile at emitter level 3.
- Test hook `READYFX.REQ`: '1' rings + cloak + amulet (equipment.md §8); '2' the staff fires a
  Light charge for no mana, a targeted charge arms C)ast's direction; '3' the white potion → 30
  x-ray turns, a quality-1 brazier → "The flame of truth."; '4' NPC torches light at 22:00 and
  douse at 12:00, a companion's torch burns down; '5' the cube: "No Effect!" without the lenses,
  the Codex vanishes with them + 8 stones; '6' a silenced egg's chance byte and a carried lit keg
  survive S)ave → load; '7' the eclipse: the sky strip's sun becomes tile 363 and the moons go, the
  noon ambient drops to night's, an NPC's torch is out at noon, lit by the hour tick during the
  eclipse, still lit when the eclipse ends mid-hour, out at the next hour (lighting.md §2b).
  VERIFIED 2026-10-27, modes 1-6 identical on emulated TT and the stock 4 MB Falcon: a Light charge
  gives 30 light turns for no mana and leaves one charge, the Magic Arrow charge arms the direction
  prompt, an empty staff says "Nothing happens."; the white potion gives 30 x-ray turns; the
  quality-1 brazier reads "The flame of truth."; a torch handed to NPC 6 is lit at 22:00 and doused
  at 12:00; a companion's torch burns 50 → 35 fuel in 40 turns; without the lenses the cube says
  "No Effect!", with them and the 8 stones at the Codex (923,851) it vanishes; the silenced egg's
  chance byte (75 → 0) and the carried keg (fuse 2) come back from the save.

## Clock and sundial — L)ook tells the time (IMPLEMENTED 2026-10-27)

> Provenance: READING ScummVM/Nuvie `usecode/u6_usecode.cpp` look_clock + is_readable,
> `core/tile_manager.cpp` update_timed_tiles, `core/game_clock.cpp` get_time_string and the U6
> `init.lua` look_obj — to understand; re-expressed, no code transcribed.

- A **clock** (object 159) and a **sundial** (235) are "readable" things: L)ooking at one is
  the whole act — no search, no weight / weapon / armour lines — and prints, after the name line,
  **`The time is h:mm A.M.`** on its own line. The hour is 1..12 (0 → 12 A.M., 12 → 12 P.M.,
  13..23 → 1..11 P.M.), the minutes two digits, the period `A.M.` before noon else `P.M.`
  (the reference's `%u:%02u %c.M.`): `8:00 A.M.`, `12:05 A.M.`, `12:30 P.M.`, `11:59 P.M.`.
- The **sundial says nothing at night**: from 20:00 to 4:59 the look is just the name line
  ("Thou dost see sundial." — the data gives the sundial no article).
- The **sundial's shadow** moves with the sun: its object tile (861) is drawn as one of the eight
  shadow tiles 328..335 by the hour — 5-6h → 328, 7-8h → 329, 9-10h → 330, 11-12h → 331,
  13-14h → 332, 15-16h → 333, 17-18h → 334, 19-20h → 335 — and as the plain dial (861 itself)
  from 21:00 to 4:59. The substitution is draw-time only (`obj_tile`); the object keeps tile 861
  for passability (tileflags1 bit1) and in the save. The shadow tiles carry no flags
  (tileflags1/2/3 all 0), so the foreground / light indexes built from the object's tile family
  are unaffected (animdata.md §3b).
- The **look line itself** (every L)ook, not only these): `Thou dost see ` + the tile's **article** +
  name — the article is tileflags3 bits 6-7 (tileflag.md: "", "a ", "an ", "the "), printed as the data
  has it, quirks included ("a earth", "the water"); the reference does the same. Before 2026-10-27 the
  engine printed "You see " + the bare name.
- Secret doors (above) keep their reveal line in the reference's search wording ("Searching here, you
  find a secret door."); the reference prints a name line before it ("Thou dost see a …" from the hidden
  frame's look entry, which the data run-fills from an earlier "sign" entry) — not reproduced until the
  real game's wording is verified.
- The **clock** is two tiles tall: the face (tile 698, foreground, drawn over whoever stands before it)
  above the pendulum slot 699, an ANIMDATA slot swinging between tiles 298 and 299 — nothing
  clock-specific in the engine. Fixed on the way: the face was never drawn, because the multi-tile
  size was read from the resolved pendulum frame instead of the object's own tile, and the foreground
  index tested the wrong tile for a height-only object's top (animdata.md §3b). SUNDIAL.REQ '3' frame:
  the face stands above the pendulum, over the wall, as in the ScummVM throne-room screenshot.
- U)se on either: the generic "Nothing happens with the …" (the reference has no USE event for
  them either).
- Data: the user's world holds 33 clocks (all frame 1, tile 699) and 4 sundials — (129,219),
  (235,167), (307,420) and (595,99) — all on the surface.
- Deviations: none of substance. The reference's generic look prints the object's weight
  ("It weighs 0.0 stones" is skipped for weightless things such as these); our L)ook prints no
  weights anywhere (an existing engine-wide difference, not a clock one).
- Hook: `SUNDIAL.REQ` byte mode — `'1'` stand south of the real sundial at (307,420): for hours
  4, 5, 7, 9, 11, 13, 15, 17, 19, 20, 21 print the drawn tile (`SUNDIAL h= tile=`), L)ook north at
  8:00 → `Thou dost see sundial.` + `The time is 8:00 A.M.`, at 22:00 the name line only; then the
  real clock at (300,350): L)ook at 15:00 → `3:00 P.M.`, 0:05 → `12:05 A.M.`, 12:30 →
  `12:30 P.M.`, 23:59 → `11:59 P.M.`; the time line is absent from a plain object's look; leave
  the party by the sundial at 15:00 for the frame (shadow tile 333). `'2'` the same spot at
  22:00 (plain dial 861) for the frame. `'3'` by the clock at 10:00 for the frame.


## L)ook and the verb cursor as DOS has them (2026-11-04)

> Provenance: READING GAME.EXE 4.5 (disassembly stays in ~/src/u6e-re, notes in its REVERSE.md, "DOS Look"):
> the Look routine, its helpers, the verb dispatch, Get, Drop, Move and Use. Re-expressed, no code transcribed.
> This section overrides the older notes above where they disagree.

**The default command.** One command can be the default, shown by a line under its icon: a right-click
on an icon picks it, and so does Alt with its letter (A C T L G D M U R B, the ten icons in order: DOS
GAME.EXE's key switch passes 0x81..0x8A to the icon routine at image 0x5cad, which moves the line). A
right-click on the map then runs the default command on that tile. Rest and comBat have no map use, so
picking them leaves no default.

**The cursor.** T)alk, L)ook, G)et, D)rop, M)ove and U)se all arm one crosshair on the map, starting on the
leader. For T)alk, L)ook and D)rop (and A)ttack, C)ast) the arrows walk it anywhere in the map window; for G)et,
U)se and M)ove a direction key acts at once on the next tile that way (DOS GAME.EXE sets their cursor range to
0xFF at image 0x3e2a, and a key with that range acts on the neighbour at once, 0x6b38; seen in DOS U6 2026-11-07:
M, Left named the bread beside the Avatar and asked `To `). Enter or a click acts on the tile under the
crosshair, Esc cancels. The
prompt is the verb's name and a dash: `Look-`, `Get-`, `Move-`, `Use-`, `Talk-`; D)rop's second prompt is
`Location: `. Choosing nothing answers `nothing` after the dash (`Get-nothing`).

**Range.** Look and Talk reach anything in the window. Get, Move and Use take what is within one tile of the
leader, diagonals too, else a blank line and `Out of range!`. What the cursor names is echoed after the dash with
its count or article: `Get-a sword`, `Use-a door`.

**L)ook prints**, after `Thou dost see `:
- **A tile with no object, or only a hidden one:** the map tile's article and name and `.`, then within one tile
  of the leader a blank line and `Searching here, you find nothing.`
- **An actor, a shrine (393) or a statue (397-399):** its name, no period. A party member has no article. A
  creature's name is its current type's look name (a body under a Polymorph-like change reads as its true type).
  Then the portrait view, below. Nothing is searched.
- **Anything else:** the count, or the article, then the name, then
  1. `. It weighs N.M stones` (`. They weigh` for a stack); N.M is the weight in tenths, the object's own plus all
     it holds. A weight of 0 or of 25.5 stones or more is not printed.
  2. the damage if the object has one: `. It can do 8 points of damage` when no weight was printed, else `, can do …`
     when an absorb follows, else ` and can do …`.
  3. the armour value if it has one: ` and can absorb 3 points of damage`. Singular `point` for 1.
  4. then one of: a book's text (`:` and the text, see below); a spellbook's page (below); the time (`The time is
     8:00 A.M.` after a blank line, no period before it); or `.`
  5. then, unless a book's text was read, the search below.

The plural comes from the count: the name's `\s` part prints unless the count is 1, its `/` part only then. These
objects have a count and always print it, 1 included: gold (88), torches (90), 63, 77, 55, 56, reagents 65-72, 337,
128, 129, 83, 335, 89, 91, 92.

**Damage and armour** are the tables combat already uses (combat.md); object damage of an unlisted type is none.

**Books.** 151 book, 61 Book of Circles, 152 scroll, 270 balloon plans and 59 Codex read when carried or within one
tile; 332 and 333 signs, 143 decorative shield, 254 cross and 255 tombstone from any distance. All only if their
quality is not 0, which is the text's number (BOOK.DAT entry quality-1). Every such object on the tile reads in
turn. A readable object out of range is an ordinary look, and is then searched only if in range.

**The search** runs only within one tile of the leader, on a non-actor, unless tileflags2 bit 3 is set on its tile
(the same "Not possible" flag that stops G)et) or it is a spellbook. It prints a blank line and
`Searching here, you find `, then:
- secret door (334): on a hidden frame (even) `a secret door`, and the door shows; on a shown one `nothing`.
- a thing with tileflags3 bit 4 is searched through to the first object under it that lacks the bit.
- a closed chest (98), crate (192) or barrel (186), or a spellbook: `nothing`.
- anything else: each thing it holds, except charges (336) and effects (337), comes out onto its tile and is listed
  `a, b and c`, each with its count or article. Holding nothing: `nothing`. It ends in `.`

**The spellbook page.** L)ook at a spellbook holding spells, carried or within one tile, opens the spell page; a
key other than Enter closes it. Enter on a spell prints `Name-In Lor`, then `Ingredients:` and each reagent on its
own line with a leading space (`None` if it needs none). The words print with a capital and lower case letters per
syllable. The reagents read `mandrake root, nightshade, black pearl, blood moss, spider silk, garlic, ginseng,
sulfurous ash`.

**The portrait view** fills the panel above the scroll box, and the scroll stays visible:
- whose: a shrine or statue shows the portrait of its quality (the one it talks through). An actor below 224 shows
  its own. An actor of 224 or more shows one only if its type is 373 (portrait 192), 382 (193) or 363 (194).
- the caption, centred below it: the actor's conversation name if thy party has talk flags for it (any bit set), or
  if it is a party member; otherwise its look name. A shrine or statue's caption is its look name.
- anything readied, types 336 and 337 not counting: the status display's figure and things at (0xC0,0x28), the portrait
  at (0xF8,0x18). With no portrait the figure goes at (0xE0,0x28). Nothing readied: the portrait at (0xD8,0x18).
- no portrait and nothing readied: no view at all.
- a key closes it. Looking costs no time.

**Other verbs' words.** `Get-` prints what it took and nothing more. `Not possible` (tileflags2 bit 3 and the
Get-forbidden things) and `The total is too heavy.` are the refusals; `Drop-` echoes and asks `Location: `, then
`Not possible` where it cannot land.

## U)se as DOS has it (read in DOS GAME.EXE 4.5, 2026-11-05)
Where this section differs from the older sections above, this one is the rule. DOS has one switch of 85 types; a type that isn't in it is `Not usable`. Anything not carried must be within one tile (`Out of range!`).

**Doors (297-300), frames 0-3 open, 4-7 closed, 8-11 locked, 12-15 magically locked.** Plain U)se: open `closed!`; closed: its trap fires, else `opened!`; locked `locked`; magic `magically locked!`.

**Chests (98), frames 0 open, 1 closed, 2 locked, 3 magic.** Open `closed!`; closed: trap, else `opened!`, then it's searched; else `No effect`. **Barrel, crate (186, 192):** closed opens; open says `Not usable`; both then search. Neither closes again.

**Keys (64), lock picks (63).** U)se asks `On ` with the crosshair. Nothing there: `nothing`. Too far: `Out of range!`. Not a door or chest: `No effect`. A lock pick first has a d30 < (45-dexterity)/2 chance of `Key broke.`: sound 13, one gone. Then a key works only if it and the lock have the same non-zero quality, a pick only if the lock's quality is 0, else `No effect`.
- Door: open `Can't (Un)lock an opened door`; closed `locked!`; locked `unlocked!`; magic `magically locked!`.
- Chest: closed `locked!`; locked `unlocked!`; else `No effect`.

**Fire (122 candle, 145 candelabra, 164 fireplace, 206 brazier, 253 campfire):** `<word> is lit.` or `<word> is doused.`, with those words whatever the tile is called. Frame bit 0 is lit. A brazier with frame bit 1 is `Not usable`.

**Secret door (334):** toggles frame bit 0, says nothing. **Bell, pull chain (236, 419):** every bell in view swings through 4 frames over 16 steps, then one tone (speaker PWM of 16000 steps; ours is sound id 23, tuned by ear). **Rubber ducky:** sound_effects.md.

**Food:** 95, 96, 128-133, 135, 180, 184, 209, 210, 265: `You eat the food.`, one gone. Drink: 115-117: `You drink it.`, one gone, drunkenness +1.

**Well (233), cow (428), churn (181), beehive (182).** All look in the acting member's pack. No bucket (178): `You need a bucket.` Over the limit (strength x 20 < carried + 32, in 0.1 stones): `The bucket of water is too heavy for you.` or `...milk...`. Else bucket out, full one in (179, 180), `Done`. Churn: no milk `You need some milk.`, else milk out, butter (96) and bucket in, `Done`. Beehive: no jar (183) `You need a honey jar.`, else the jar becomes 184, `Done`.

**Fishing pole (264):** water on one of the 8 tiles around, else `You need to stand next to water.` One in 5, and once an hour: over the limit (carried + 5) `Got it, but can't carry it.`, else `Got it!` and a fish (265). Otherwise `Didn't get a fish.`

**Fountain (234):** `Make a wish?`, Y: `Wish for: ` (8 letters). Food, Mutton, Wine, Fruit or Meat (any case): one in 5 `You got food` and a portion of meat (129), or `You got food, but you can't carry it.` over the limit (carried + 2); otherwise `Failed`. Anything else: `No effect`. Another key ends it.

**Pick, shovel (103, 104):** must be readied, else `Not readied.` `Direction:`, a direction key, else `nowhere.` On the surface at (707,835), with no hole there: a hole (308) of quality 19, `You dig a hole.` On levels 1-4 on map tiles 108-111: one in 32 a fountain, `You find a water fountain.`; else one in 10 a gold nugget (89), `You find a gold nugget.`; else `Failed`. Anywhere else `No effect`.

**Balloon plans (270):** needs a silk bag (421), basket (422), cauldron (147) and rope (284) among the party's things; says `Missing a mammoth silk bag.`, `Missing a balloon basket.`, `Missing a cauldron.`, `Missing a rope.` for each missing. All there: they're used up, the balloon (420) lies at the party's feet, `Done!`.
**Balloon (420):** surface or gargoyle world, else `Not usable`. It inflates (423, frame 3) on the first free of the 8 tiles around, `Done!`; none free: it inflates underfoot, `No room!`.

**Gargish vocabulary (97):** `You study the scroll!`; the party knows Gargish from then on.

**Crystal ball (155):** d30 < (45-intelligence)/2 hurts the user d10 and ends it. Otherwise `Enter degrees followed by N, S, E or W.`, `At latitude=`, 3 characters, digits and N or S, in range 44N to 80S; then `longitude=` 37W to 88E, E or W. A bad answer resets the field to 0 and ends it. Then the Wizard Eye's view, at x = longitude x 8 + 304, y = latitude x 8 + 360, divided by 4 below the surface.

**Telescope (154):** from where it lies (or who holds it): `Direction-`, a direction key, else `nowhere.`; the view flies that way up to 40 tiles or until solid rock (map tile 252 on); `Done`.

**Silver horn (313):** a silver serpent about already: `Not now!`. Otherwise three tries of up to 8 random spots each on the edge of the view; a spot needs itself and the tiles W, S, SW, E and N free. Each gives one serpent (413), chaotic, assaulting. `Silver snakes are generated!`, or `None generated!`.

**Staff (78):** must be readied, else `Not readied.` No charge: `No effect`. `With <the spell's words>`, then the spell, without mana or reagents. **Vortex cube (62):** see Staff and cube; No effect: `No effect`. The Codex must be at (923,851), within 5 tiles.

**Horse (430):** by the acting member: Sherry `Sherry says: "Eeek!!! I'm afraid of horses!"`, Beh Lem `BehLem says: "Horses are for food!"`, already riding `You're already on a horse!`. A town's horse (actor under 224): `Not boardable!`.

**Lever:** closing onto something: `can't close the portcullis.`, and the rest of its gates stay as they are. A new portcullis has frame 3 where the doorway's frame has bit 1, else frame 1. **Passthrough:** only closing can be refused by an actor (`Not now!`). **Crank: which bridge** (seen in DOS 2026-11-05): DOS takes the first piece whose quality is the crank's among the objects it holds, the 2x2 superchunks round the party, so a crank always works its own bridge; at the far bridge the crank at (789,959) opened rows 960-961 (frames 3,4,5) and the end row 962 (0,1,2) and left the castle's alone. The piece found is the closed row's left end (frame 6) or the top open row's left end (frame 3); none in reach: `No correspondent drawbridge`; another frame: `Strange, nothing happened.` Hook PERSIST.REQ '8'. A closed bridge that can't move says `Can't close the drawbridge.` or `Can't open the drawbridge.`

**Food and drink in the pack** work the same as on the map. Things that also work where they lie: fan, pole, sextant, runes, orb, cube, balloon; readied-only ones still say `Not readied.` A readied thing is used by picking its slot on the status display.

### Other verbs
- **T)alk (order as DOS checks):** a party member off screen `Not on screen.`; solo `Not in solo mode.`; asleep, paralyzed, dead, evil, chaotic, or under worktypes 0x07, 0x12, 0x13, 0x9b `No response`; yourself `Talking to yourself?`; a temporary actor other than 373, a guard (382) or 363, which share scripts 102, 103 and 104, `Funny, no response.` DOS checks no line of sight here.
- **Ready:** `Ready-<name>`, then `Can't be readied!`, `No place to put!`, or `Too heavy!` when what's readied, this included, weighs over strength x 10 (0.1 stones). **Unready:** `Unready-<name>`.
- **L)ook** at a locked chest: `It's locked.` instead of the search.
- **D)rop** or **M)ove to a member,** of more than one of a stackable thing: `How many? `. Enter means all; 0, a non-number or too many means none (`How many? 0`). DOS asks before the location, we ask after it: the pack selection has no confirming key.
- **D)rop further than one tile** of 83, 92, 155, 123, 115-120, 183, 184, 275, 161, 417: sound 15, `It broke!`. A mirror (123) stays with frame 2, type 417 with frame 1, the rest are gone.
- **M)ove to a member** over their limit (strength x 20 < carried + the thing): `Can't carry!`.

### Also heard or seen
- **A blow that wounds a neutral person** (410, 384, 381, 387, 385, 376, 382, 383, 409, 386, 378, 380, 377, 379) struck by a party member: `%s yells for help!`, then the same alarm as a witnessed theft (5 karma, guards, others within 6).
- **Slime (375), wounded and alive:** up to 8 tries of a random spot within 1; on the first free one a copy with its hit points, `Slime divides!`.
- **Passage:** the invisible object 416 at (923,872) stops a party not on a quest: `"Thou art not upon a Sacred Quest!\nPassage denied!"`.
- **Shamino** (actor 3, in the party, alive, within 6 tiles of the Avatar): stepping onto a cave or hole of quality 1-20: `Shamino says, "This is the dungeon Deceit."`: names by quality: Deceit, Despise, Destard, Wrong, Covetous, Shame, Hythloth, GSA, Control, Passion, Diligence, Tomb of Kings, Ant Mound, Swamp Cave, Spider Cave, Cyclops Cave, Heftimus Cave, Heroes' Hole, Pirate Cave, Buccaneer's Cave; `dungeon ` before 1-7, `shrine of ` before 9-11. An evil or chaotic creature hatching: three chances in 4 of `Shamino says, "I hear something to the %s!"`, at most once a turn.

## Created and destroyed objects persist (2026-10-28)

> Provenance: READING ScummVM/Nuvie `actors/u6_actor.cpp` die + `u6_actor_types.h` (dead_obj_n /
> dead_frame_n), `actors/actor.cpp` all_items_to_container / inventory_drop_all / set_dead_flag,
> `actors/actor_manager.cpp` (objlist 0x800 status, position save), and the U6 spell scripts
> `web.lua`, `trap.lua`, `fire_field.lua`, `reappear.lua`, `replicate.lua`, `create_food.lua` — to
> understand what the original keeps; re-expressed. Mechanics in save_load.md §5.

What the game creates during play and KEEPS (a new save record, save_load.md §5 step 0b):
- **Barriers and bridge pieces** (levers / switches / cranks above).
- **Webs** laid by the Web spell (53, frame 0) and the **trap effect** (337) the Trap spell puts
  inside a door or chest (a contained record under the holder's record). Untrap deletes it.
- **A slain NPC's remains**: a real NPC (one with an objlist slot that is not a hatched or summoned
  temporary) dies into a body object at its tile, **quality = its actor number** (Seance / Resurrect
  find it by that), status OK-to-take (0x01). Which object, by the slain creature's type (the
  reference's actor-type table):

| creature | remains |
|---|---|
| townsfolk, guards, fighters, mages, jesters, children, Lord British, the Avatar, daemon, troll, gargoyle (363), skeleton, headless | **dead body 339**, frame by type: villager/merchant/LB 2, mage/woman 3, guard 4, swashbuckler/peasant 5, fighter 6, musician/Avatar 7, child/jester/farmer 8, skeleton 9, daemon/troll/gargoyle/headless 0 (headless 1) |
| winged gargoyle (362) | **dead gargoyle 341** frame 3 |
| cyclops (424) | **dead cyclops 340** frame 3 |
| horse (430) | **horse carcass 134** frame 1 |
| reaper, mouse, cat, tangle vine, dog, deer, wolf, drake, mongbat | **the creature's own object**, in the frame it died |
| giant bat, gremlin, gazer, bird, corpser, rabbit, rot worms, hydra, giant rat, sheep, snake, giant spider, giant scorpion, giant ant, cow, alligator, horse with rider | **blood 338**, a random frame 0..2 |
| insects, giant squid, ghost, acid slug, wisp, sea serpent, dragon, silver serpent, the playing musician, vehicles | nothing |

  The dead NPC's **inventory goes into the body** (its readied and carried records become the body's
  contents; a blood splat or "nothing" drops them on the tile instead). The actor itself is flagged
  **dead** (objlist 0x800 bit 0x10), HP 0, and parked at (0,0) level 0 like the reference; a load
  skips dead actors. Resurrect clears the flag (magic_skills.md §5f).
- A temporary actor (hatched / summoned) leaves the SAME remains, but temporary — gone at the next
  save, as the reference's TEMPORARY status does.

What stays TEMPORARY, as in the reference: spell fields (fire / poison / sleep / energy — the
reference flags them temporary), Reappear / Replicate copies (status 0x21), summoned and hatched
creatures, moongates, the campfire.

Hooks: PERSIST.REQ '3' (a body with contents, the dead flag, load-back, Resurrect), '5' (web +
trap records), below; ScummVM cross-load of the written save (tools/savecheck.sh).

### PERSIST.REQ (byte mode)
- `'1'` the castle lever (quality 1 at (303,383)) and its doorway (307,384): U)se → the portcullis
  at the doorway toggles (created with the doorway's frame, or destroyed), the lever's frame flips,
  the message; U)se again → back; the lever of quality 46 (level 4, moved to) → "strange, nothing
  happened"; a switch (quality 101, level 1) → an electric field appears on its doorway; S)ave →
  the written file holds the new barrier record at the doorway tile and the lever's frame.
- `'2'` the castle crank (303,384): U)se → the 8 closed pieces are gone and an open bridge of
  width 7 hangs from y=386 (frames 3/4/5 rows + 0/1/2 bottom), "Open the drawbridge.", the span is
  passable; U)se again → "Close the drawbridge.", the closed row is back (6 at 303, 7×6, 8 at 310);
  S)ave after opening → the file holds the open pieces and none of the closed row.
- `'3'` a real NPC near the start is slain by a spell: a body of the right type at its tile with
  quality = the actor, holding its former inventory (count); the actor gone; S)ave → the file: the
  body record with the inventory records as its contents, the actor's 0x800 bit 0x10 set, position
  (0,0,0); load the written save → the actor absent, the body present with its contents; Resurrect
  on the body → the actor stands, flag clear.
- `'4'` food: a loaf (qty 2) on the map: U)se twice → "You eat the food." / gone, the record's count
  then the record itself; ale ×4 → alcohol 4, 40 steps → at least one "Hic!"; an hour → 3; S)ave →
  byte 0x1c17 = 3.
- `'5'` Trap on a chest + Web beside the party → S)ave → the file holds a 337 child under the chest
  and 53 records at the web tiles; Untrap → the child record gone.

Status: VERIFIED 2026-10-28 — PERSIST.REQ 1-5 identical on emulated TT and the stock 4 MB Falcon:
'1' the castle lever (quality 1) removes the portcullis the data has on doorway (307,384) ("Switch the
lever, you hear a noise.", the tile unblocked, lever frame 1, the saved file holds 0 portcullis records
there and the lever's frame), the second pull recreates it with the doorway's frame (1 record saved);
lever 46 on level 4: "strange, nothing happened"; switch 101 on level 1 removes and restores the electric
field on doorway (195,222). '2' the castle crank: the 8 closed pieces go, 5 open rows (3/4/5) plus the
0/1/2 end hang from y=386, the span is passable, crank frame 3, "Open the drawbridge."; the saved file
holds the open pieces and none of the closed row; the second turn restores 6/7×6/8 at y=385 and the crank.
'3' the nearest NPC with inventory from the start is Lord British (4 records): "Killed Lord British!",
a dead body frame 2 at his tile with quality 5 holding the 4 records (live and in the template, which grew
by one record; his inventory chunk records gone), objlist bit 0x10 set, position (0,0,0), HP 0; the written
save loads back with him absent and the body present with its 4 contents; Resurrect: "Success", he stands,
the flag clears and the body's record is gone. '4' a loaf (128, stackable) set to 2: eaten twice ("You eat
the food.") — count 1 in the record, then no record; four ales → alcohol 4 ("You drink it."), an hour → 3,
three more → 6, 16 steps → 14 "Hic!" (12 real moves), saved byte 0x1c17 = 6. '5' the nearest untrapped
chest: Trap → a new 337 child (11 children saved under the chest, was 10), Untrap deletes it (10 again);
Web → 6 webs, each a saved record. Real ScummVM loads the written saves with the body (persist 3b), the
open bridge (2b) and the webs (5): "Game Loaded" (tools/savecheck.sh); savelinks.py: 0 bad links in all
three. Host suites + `make check` pass.
Notes: mode 4 walks ~7 game hours (turns are slow at dusk, item 17), so it needs VBLS 30000 on the TT and
60000 on the Falcon; the alcohol count fell to 0 during a 40-step walk before the hook was retuned — the
hourly decrement, not a bug.

## Other use handlers (TODO — same dispatcher, future slices)
- Magic lock/unlock spells. (Field / lever / crank / body / web / trap / food persistence: DONE 2026-10-28, above.)
- Readables (signs/books/tombstones) → show text — DONE (docs/formats/strings.md;
  L)ook / U)se on a readable pages its text). Clock/sundial time display — DONE (section above).
- Ladders/holes/stairs → change level. Needs the multi-level world model.

## Status: IN PROGRESS (doors + containers/stacks + keys/locks + LOCKPICKS + FOOD/HUNGER +
## readables + LEVERS/SWITCHES/CRANKS + DRAWBRIDGES + ladders/holes/caves (map_levels.md) +
## SECRET DOORS / PASSTHROUGHS / FIRE DEVICES / TORCHES / POWDER KEGS + small handlers +
## M)OVE pushing + STAFF / VORTEX CUBE / WHITE POTION / HOLY FLAMES / NPC TORCHES + CLOCK / SUNDIAL +
## REFERENCE LEVERS / CRANKS / FOOD + PERSISTENT CREATED OBJECTS done)

## Theft (read in DOS GAME.EXE 4.5, 2026-11-04; not run in DOS)
**What counts.** Bit 0 of an object's status byte means "free to take". After a successful G)et on the **surface**
(level 0), if the object or anything inside it has the bit clear:
1. The bit is set on the object and everything inside it, so the same thing is only ever stolen once.
2. Karma −1.
3. If any actor 1..223 is neutral (alignment bits 0), awake, within 4 tiles of the Avatar on both axes and in view,
   "Stop Thief!!!" is printed and the **alarm** runs. Otherwise "Stealing!!!" is printed.

An object dropped from the pack is free to take again, and so is what moves into a dead member's body.

**The alarm.** Karma −5 more. For each actor 0..255 that exists, stands on this level, is alive, not in the party and
neutral:
- A guard (type 382) closer than 32 tiles to the Avatar takes worktype **0x12**, arrest.
- Anyone else closer than 6 tiles who has a worktype other than 0 or 0x0E, isn't paralyzed or asleep, and is
  not flagged as a non-reacting type:
  - worktype below 0x80: **0x13**, runs from the party.
  - worktype 0x80..0x86: unchanged.
  - worktype above 0x86: **0x9B**, attacks the party (a type-392 actor first goes back to its normal shape).

**Arrest (0x12).** Each turn the guard walks toward the nearest party member within 50 tiles. Beside one:
- If Lord British's talk flag bit 0x80 is clear, the guard just stops (worktype 0x81): no scene.
- Else: `"Thou art under arrest!"`, `"Wilt thou come quietly?"`, Y or N. The guard stops (0x81) either way, first.
- **Y:** "Yes", "The guard strikes thee unconscious!", "Thou dost awaken to...". Off any vehicle, the party goes to
  **(231,186)** on the surface and the clock runs an hour at a time until **08:00**. Every member loses all
  **lockpicks** (63). A **key of quality 9** (64) goes into the container (177) at (235,183). The object 300 at
  (231,184) is set to frame 9. The NPCs are re-placed by schedule and the area's song is chosen again.
- **N:** "No", `"Then defend thyself, rogue!"`, the alarm runs again (karma −5), and this guard and every guard
  under 0x12 become worktype 0x08 and alignment 0x20 (hostile).

Our message box holds five lines, so we leave out the echoed Yes / No and the blank lines between these messages.

## M)ove and more, as DOS has them (read 2026-11-05; overrides the above)

**The crosshair finds nothing on a dark tile.** **M)ove on the map:** beside the leader or `Out of range!`; weight 0 or type 411 `Not possible`; an actor `Failed` by the strength test, if the leader or seated. 5 points. The first thing on the target, if a holder that takes it, has it inside: 98, 186, 192 at frame 0, 99, 188, 191, or the cube, taking anything but those three, under weight 255. Else it needs a tile it could stand on, or something with a top before a boundary; a diagonal needs one side clear too; otherwise `Blocked`, and the points are gone. A cannon turns whatever its way out. **How many? comes first** for a carried stack, then `Location: ` or `To `. Enter = all; 0, a bad number or too many stop it.

**Gem, potion, moonstone** work where they lie within one tile. **A horse** carries its rider; the others walk. Dismounting puts the horse on the first free tile around by column, else `No room!` and it's gone. **Every theft** sounds 13. A sleeper or a body reads as what it is, and **a book's text goes in the scroll**. Asleep, an actor lies down, in a bed as 146; under 0x91 it stays down till its schedule moves on; a blow wakes it. A musician at work (0x95) shows shape 392 and its instrument plays.

**D)rop on the map, as DOS has it (read 2026-11-05):** the target needs a tile the thing could stand on, or a thing with a top, or a first thing that is a holder that takes it; within 8 by the distance table of combat.md §6c, with a clear line; otherwise `Not possible`. 3 points. It flies there as a missile, whatever it is. Further than one tile, the breakables of the list above break. Otherwise a holder on the tile takes it, and anything else leaves it on top.

**A crank, as DOS has it (read 2026-11-05):** it doesn't change frame. Closing, anything but a bridge piece on a piece's tile says `Can't close the drawbridge.`. Opening, rows run from the start until the map tile under the column after the end post is 16, 28, 32, 44 or 214; anything on the rows above that says `Can't open the drawbridge.`.
