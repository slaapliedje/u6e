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

- **World objects**: the 64 surface super-chunks are decoded as one concatenated OBJBLK
  stream (`u6obj_decode_world`), then converted with the SAME keep/parent rule mkworld uses
  when it bakes the pack (top-level z==0 kept; contained objects kept when their container
  is, with x/y zeroed, alive=0, parent re-indexed). The whole `g_wobj` array is replaced.
- **Actors** (objlist, little-endian): position (0x100+i*3), appearance → tile
  (0x400+i*2, basetile+frame), str/dex/int (0x900/0xa00/0xb00), exp (0xc00, u16),
  HP (0xe00); maxhp := HP and level := maxhp/30 — the SAME derivations the pack boot uses,
  so a load equals a boot from that save.
- **Avatar/world**: position + z-level → the camera; clock (0x1bf3 minute, 0x1bf4 hour)
  → `g_start_min` (turn clock rebased to 0); gender 0x1c71, karma 0x1bf9.
- **Party**: rebuilt from the objlist status bit 0x80 (names kept when already known,
  else resolved from CONVERSE).
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

- Surface-focused: the 5 dungeon super-chunks are not reloaded (do_save also only patches
  the surface region). Dungeon-level object persistence lands with the per-level-objects
  dungeon follow-up.
- Pack stack CONSUMPTION (torches burnt, food eaten, reagents cast) and gold SPENT are not
  written back: the save's carried records keep their counts. Bought/received items from
  conversations likewise (no record exists to patch). These need record insertion/removal,
  i.e. a real inventory-chunk rebuild — the next step for this writer.
- Spellbook known-spells / reagent seeds keep their session state (reagent ITEMS in the
  pack reload fine and are what casting consumes).
- A bag carried in the pack is flattened on load (its contents become loose pack items);
  dropping such a bag relocates it WITH its contents, so its former contents then exist
  both on the map (in the bag) and in the pack until the next load resolves it.

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

## Status
VERIFIED (v1 scope above; §5 write-back added 2026-09-29).
