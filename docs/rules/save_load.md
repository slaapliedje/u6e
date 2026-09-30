# Save / Load: on-target restore of SAVED.000

Games: Ultima VI (SE/MD ship no saves yet).
Implemented by: /engine save.c (read-side offsets), /dataformats u6obj.c (record decode, now
compiled for target), /render gamedemo.c `load_game()`.

> Provenance
> - Derived from : our own format specs — nuvie_save.md (ScummVM save layout, super-chunks,
>   objlist endianness), objlist.md (actor arrays, player fields, party bit), objblk.md
>   (8-byte object records, container links). No new reference reading required.
> - References   : (see those specs)
> - Tools        : none
> - Validated by  : LOAD.REQ round-trip on emulated TT AND stock 4 MB Falcon (§4); readied-gear
>   fold parity host-vs-target (§3); §5 write-back proven by LOAD.REQ modes 2/3 (two save->load
>   cycles) and by loading the target-written file in real ScummVM (tools/savecheck.sh).
> - Confidence   : confirmed
> - Last updated : 2026-09-29

## 1. What loading restores (the exact mutable set the save carries)

`load_game()` re-reads `SAVED.000` (the engine's own written save — ScummVM format,
nuvie_save.md) and rebuilds the LIVE state:

- **World objects** of the Avatar's LEVEL (`level_decode`, map_levels.md §4b): the 64 surface
  super-chunks (or dungeon chunk z-1) are decoded as one concatenated OBJBLK stream
  (`u6obj_decode_world`), then converted with the SAME keep/parent rule mkworld uses when it
  bakes the pack (top-level records of that z kept; contained objects kept when their container
  is, with x/y zeroed, alive=0, parent re-indexed). The whole `g_wobj` array is replaced.
- **Actors** (objlist, little-endian): position (0x100+i*3), appearance → tile
  (0x400+i*2, basetile+frame), str/dex/int (0x900/0xa00/0xb00), exp (0xc00, u16),
  HP (0xe00); maxhp := HP and level := maxhp/30 — the SAME derivations the pack boot uses,
  so a load equals a boot from that save.
- **Avatar/world**: position + z-level → the camera; clock (0x1bf3 minute, 0x1bf4 hour)
  → `g_start_min` (turn clock rebased to 0); gender 0x1c71, karma 0x1bf9.
- **Party**: rebuilt from the objlist status bit 0x80 (names kept when already known,
  else resolved from CONVERSE).
- **Conversation state**: per-NPC talk flags (0x17f1), quest flag (0x1bf1), knows-Gargish
  (0x1c5f) → the conversation VM's game state (also absorbed from the template at boot).
- **Pack + equipment + gold**: from the actor-inventory super-chunk (offset 4) — carried
  (status 0x10) party items → the pooled pack (nested container contents flattened by
  walking the record-index chain, ≤8 hops); readied (0x18) items → the owner's slot set
  (equipment.md), re-folded to weapon/AC; coins (obj 88) summed into the gold pool.
  The inventory chunk's ON-MAP records (an item dropped from the pack, §5) join the world
  objects: every loader places a record by its own x/y, not by the chunk it sits in.
- **Vehicle state** (vehicles.md §6): the party saved on a boat's tile is aboard it again
  (skiff, or a ship via its centre part); saved on a horse's tile, it is mounted again.
- The loaded file becomes the SAVE TEMPLATE: the next S)ave patches this state, so nothing
  from an earlier session reverts (before 2026-09-29 do_save kept patching the boot-time
  SAVE.000 after a load — a save-after-load silently undid the previous session's changes).

All transient buffers (the file, the decode array, the remap table — ~415 KB for U6) come
from a chip mark/reset region: zero permanent cost, fits the stock 4 MB tier's free pool.

## 1b. Where the template comes from (2026-09-29)

`SAVE.000` is no longer a dev file copied from a ScummVM install: `make world` synthesizes
the PRISTINE new-game save from the player's own LZOBJBLK + LZDNGBLK (tools/mksave;
nuvie_save.md "Pristine template"), builds the pack from it, and the disk/card installs
ship it (142 KB, disk 3). So a fresh install can S)ave, "Journey Onward" resumes SAVED.000,
and the new-game party carries its real starting gear/gold/reagents (the old pristine pack
had none — mkworld only harvested inventories in save mode). The engine also writes the
player's name (0xf00), level (0xff1+actor) and magic points (0x13f1+actor) and reads them
back on load; the trailer is date-stamped for ScummVM's load menu.

## 2. Entry points

- **Ctrl-R** in play: restore SAVED.000 ("Game restored." / "No saved game (SAVED.000).").
- **Journey Onward** at the main menu: resumes SAVED.000 when it exists; a character-creation
  run always starts fresh (g_cc_done wins).
- The original had no in-game verb for this (DOS U6 restored from its boot menu); Ctrl-R is
  our mapping, chosen not to collide with R)est.

## 3. Known limits (v1, symmetric with do_save)

- Every level persists (2026-09-30, map_levels.md §4b): the writer addresses records by level
  and load decodes the Avatar's level; changes on other levels were reconciled into the
  template when the party left them. Eggs are still opaque (carried byte-wise).
- Pack stack consumption, gold spent and received items DO persist since 2026-09-30: the file
  is written as a rebuild (§6). What the pooled model cannot say — which companion carries what
  — is written as "the Avatar carries the pack" (companions keep only their readied gear).
- Spellbook known-spells / reagent seeds keep their session state (reagent ITEMS in the
  pack reload fine and are what casting consumes).
- A bag carried in the pack is flattened on load (its contents become loose pack items) and
  written back loose (§6). A companion who JOINS after the load keeps their template records
  untouched (they are pooled at the next load); one who LEAVES leaves their items in the pool.

## 4. Test hook + validation

`LOAD.REQ` byte modes:
- `'1'` (or any other byte): walk 3 turns → do_save → wander off → load_game → print the
  round-trip (`LOAD restore ok/x match/y match/clock match/party/nobjs/gold/ninv/aN weapon/ac`).
  VERIFIED 2026-09-20 on emulated TT and on the stock 4 MB Falcon (identical output):
  position/clock restored exactly; 8354 objects rebuilt from the save; gold 110; pooled pack
  21 stacks; readied fold a1(weapon 43, ac 0) a2(weapon 43, ac 13) — byte-equal to mkworld's
  host-side fold of the same save (independent parser parity).
- `'2'`: PERSISTENCE round-trip (§5) — member 2 takes off head + body, the avatar readies the
  helm, the body armour is dropped beside the party, a REAL owned skiff of the save is boarded
  and sailed 3 tiles; then TWO save→load cycles, each printing `PERSIST load ok / aboard /
  avatar on boat / boat at new pos / boat gone from old / a1 head / a2 head / a2 body / body
  on ground / body in pack`. VERIFIED 2026-09-29 on emulated TT and the stock 4 MB Falcon:
  every line holds in both cycles (helm 3 on the avatar, plate 22 on the ground, skiff at the
  sailed position with the party aboard; the second cycle proves the loaded file became the
  template). Needs the save-derived pack (`make world-svm`) so the party has gear to change.
- `'4'`: inventory REBUILD round trip (§6): consume part of a stackable stack, spend gold,
  receive an item, save → load twice; prints `REBUILD stack qty / gold / dagger / ninv / spells
  match / a2 ac / nobjs`.
- `'3'`: as '2' without the boat, so the party stays put and the written SAVED.000 shows the
  drop when loaded elsewhere. `tools/savecheck.sh build/SAVED.000` (Xvfb; run with
  `env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11`) loaded that file in real ScummVM: "Game Loaded",
  full party, no errors — the relocated record in the inventory chunk is accepted (Nuvie's
  loader places every record by its own status + x/y whatever chunk it sits in). NOTE: the
  script now pins `--save-slot=0`; before that fix Nuvie loaded its `latest_save` slot
  instead of the swapped file, so the 2026-09-29 re-run (dropped plate mail visible on the
  throne-room carpet in ScummVM) is the first real proof.

## 5. Write side: what S)ave reconciles (do_save, engine/save.c)

The template save is patched IN PLACE; no record is ever added or removed, so every chunk
count and offset is preserved and every status bit we don't model survives untouched. Steps,
in this order (each later step looks for what an earlier one produced):

1. **Taken items** (world object dead + dirty): the on-map / in-container record becomes
   carried by the Avatar (loc 0x10, x = owner 1).
2. **Changed map objects** (dirty, alive): frame, position and — for boats — quality are
   written into the record found at its ORIGINAL position. Boats are the only objects that
   move: boarding registers each part's record position (a small table), the patch re-anchors
   it, and a load clears the table (positions match the save again).
3. **Readied gear**: every readied record (0x18) owned by a party actor is first demoted to
   carried (0x10); then, per party actor and slot, ONE carried record of that obj_n owned by
   any party actor (own records preferred, single items preferred, a bag's contents accepted)
   is marked readied by that actor (0x18, x = actor). Records readied in the save but not
   live end up carried; items readied from a GET this session are found via step 1.
4. **Dropped items** (`WOBJ_DIRTY_DROPPED`): every party-carried record of that obj_n +
   quality (readied ones excluded) is relocated onto the map at the drop tile (loc 0, x/y,
   z 0, frame); each keeps its own stack count — the pooled stack IS their union. The object
   is then an ordinary on-map record (dirty → STATE), so a later GET flips it like any other.
5. **Actors**: party positions (aboard a boat: all at the boat), stats, experience; the ridden
   horse (absorbed) or the last dismounted horse gets its position; clock; created-avatar id.

Region walked by steps 3-4: the actor-inventory chunk, the eggs chunk and the 64 surface
chunks (`[4, surface_end)`), ~10 k records; container links are chunk-local record indices.
Steps 1, 2 and 4 address a record by LEVEL (surface super-chunk by x/y, or dungeon chunk z-1;
a drop is relocated with the level's z). The same reconcile (`objects_reconcile`) runs before
every level switch, so the template always holds every level's current state.

## 6. The written file: a streaming rebuild (2026-09-30)

In-place patching (§5) keeps the in-memory template consistent as the object store of every
level (map_levels.md §4b), but it cannot add or remove records — so the FILE is written as a
rebuild (`sav_write_rebuilt`, engine/save.c), streamed through a 2 KB buffer with no allocation:

- header copied; then every chunk (inventory, eggs, 64 surface, 5 dungeon) copied record by
  record MINUS every record held by a POOLED actor (`g_pooled`: the party members whose
  inventories the live model absorbed — carried/readied by them, or contained in something
  they carry, chunk-local links followed), counts and links re-indexed;
- the actor-inventory chunk then gets the party's inventory FROM THE LIVE MODEL
  (`build_party_records`): each member's readied items (0x19, owner = member), the pooled pack
  carried by the Avatar (0x11; a stackable type — pack section U6W5, objblk.md §2 — as one
  qty16 record, a non-stackable stack as up to 8 single records), the gold as one coins record
  (obj 88, qty16), the spellbook (57) with one spell record (58, quality = spell number) per
  known spell as its contents;
- objlist copied; a ScummVM trailer copied with its header offset shifted by the growth.

The template itself is allocated with 16 KB of headroom so an in-game load can adopt a larger
rebuilt file as the new template. At boot the party's pack is likewise absorbed from the
template's inventory chunk (`pool_from_inventory`), so a fresh game carries its real starting
items (reagent/spellbook seeds only top up what is missing).

VERIFIED 2026-09-30: `LOAD.REQ '4'` (consume 3 of a 14-stack, spend 25 gold, receive a dagger,
save → load, twice) on emulated TT and the stock 4 MB Falcon: 11 / 85 / 1, known spells and
readied AC intact, 8343 surface objects; a rebuilt file (183 inventory records = 96 NPC
records kept + the party's) loads in real ScummVM with the earlier dropped plate mail still
on the carpet.

## Status
VERIFIED (v1 scope above; §5 write-back added 2026-09-29; §6 rebuild 2026-09-30).
