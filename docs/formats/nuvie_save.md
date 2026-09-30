# Nuvie / ScummVM save format (U6) — our adopted save format

We adopt ScummVM's Nuvie save format as our OWN save/restore format (rather than
inventing an incompatible one), so saves interoperate with ScummVM and we can load
a ScummVM save for pixel-faithful comparison.

> Provenance
> - Derived from : READING ScummVM engines/ultima/nuvie/save/save_game.cpp
>   (SaveGame::load / check_version) + core/obj_manager.cpp (load_super_chunk /
>   loadObj); inspecting ~/.local/share/scummvm/saves/ultima6.000 (174214 bytes).
> - Tools        : our tools/nuviesave (decoder), hex inspection.
> - Validated by  : header version reads 256 (= 1.0); objlist actor 1 decodes to
>   obj_n 410 (the Avatar). Built a world pack from ScummVM's ultima6.000 and
>   rendered it on the TT target: the throne-room map (blue floor + braziers, red
>   carpet, NPC placement) and the 4-member party roster match ScummVM's screen
>   (refshots/u6_gameplay.png). Remaining deltas: party HP/stats (placeholder
>   numbers, not yet decoded from the objlist stat block) and a ~1-row viewport
>   offset (likely ScummVM half-tile smooth-scroll vs our whole-tile render).
> - Confidence   : confirmed (object + actor placement); stats arrays TODO
> - Last updated : 2026-06-25

## Endianness
ScummVM's NuvieIO reads **little-endian** (the DOS data lineage). So a ScummVM
save is LITTLE-endian throughout — including its embedded objlist (contrast the
Atari **ST** original SAVEGAME/OBJLIST, which is big-endian; see objlist.md).

## File layout (SaveGame::load — the native ScummVM `.NNN` save)
| bytes | field                                                        |
|-------|--------------------------------------------------------------|
| u16   | version (LE) = SAVE_VERSION = 1*256+0 = **256**               |
| u16   | game_tag (LE) = GAME_ID(gameType)                            |
| ...   | super-chunk: **actor inventories**                           |
| ...   | super-chunk: **eggs**                                        |
| ...   | **64** surface super-chunks (object placements)              |
| ...   | **5** dungeon super-chunks (levels 1..5)                     |
| u32   | objlist_size (LE)                                            |
| n     | **objlist** (objlist_size bytes) — the actor/party/clock table|

(No textual header — that's standalone Nuvie's `load_new`; ScummVM's `load()` is
leaner. A ScummVM save file may carry ScummVM metadata appended elsewhere, but the
engine payload begins at byte 0 as above.)

## Super-chunk (ObjManager::load_super_chunk)
| bytes | field                          |
|-------|--------------------------------|
| u16   | num_objs (LE)                  |
| 8×N   | num_objs object records        |

So a super-chunk is `2 + num_objs*8` bytes — walkable without parsing contents.

### Surface super-chunk ORDER (ScummVM save)  — differs from the original OBJBLK
The 64 surface super-chunks are written **row-major**: file slot `c` (0..63) holds
the 128×128-tile superchunk at column `c%8`, row `c/8`, so a map object at (x,y,z=0)
belongs in slot `(y>>7)*8 + (x>>7)`. (Verified: the throne room — avatar at 307,352
— is in slot 18 = `(352>>7)*8+(307>>7)`, which holds 801 objects.) This is NOT the
2×2 "quad" order the original DOS/ST OBJBLK uses (see objblk.md §1); ScummVM's
`save()` emits the superchunks in a plain index loop.

Observed deviations a writer must respect (derived via /tools/objprobe on the
sample save; ~97% of map objects follow the formula, the rest are intentional):
- **Contained objects** (status bits 1/2/3) follow their container as flat records
  in the **container's** superchunk; re-bucketing them by their own x,y is wrong.
- **Relocated/off-grid objects**: a few map objects sit in a superchunk that does
  not match their x,y (e.g. y=0 entries parked in slot 4). Nuvie keeps them in
  whatever list they were last in rather than re-bucketing on every move.

⇒ To reproduce a save **byte-for-byte**, treat superchunk membership + record order
as part of the model (what /tools/nuviewrite does). A from-scratch *functional*
writer (the engine, placing freshly-modelled objects) may instead bucket map
objects by the formula and emit contained objects after their container.

## Object record (8 bytes, ObjManager::loadObj) — standard U6 packing
| byte | bits | field                                                      |
|------|------|------------------------------------------------------------|
| 0    | all  | status (location in bits 0-1: 0=map,1=container,2=inv,3=readied) |
| 1    | all  | x low 8                                                     |
| 2    | 0-1  | x high 2 → x = b1 \| ((b2&3)<<8)                            |
| 2    | 2-7  | y low 6                                                     |
| 3    | 0-3  | y high 4 → y = ((b2&0xfc)>>2) \| ((b3&0xf)<<6)              |
| 3    | 4-7  | z (level)                                                  |
| 4    | all  | obj_n low 8                                                |
| 5    | 0-1  | obj_n high 2 → obj_n = b4 \| ((b5&3)<<8)                    |
| 5    | 2-7  | frame_n = (b5&0xfc)>>2                                      |
| 6    | all  | qty (stackables: qty = (quality<<8)\|qty)                  |
| 7    | all  | quality                                                    |

Container/inventory objects appear as flat records in the same super-chunk
(status bits say where they live; inventory objects use `x` = owner actor number).
This is the same record as objblk.md, so a concatenation of the 64 surface
super-chunks == a decompressed OBJBLK stream (feeds our u6obj decoder directly).

## objlist
Same structure as objlist.md (positions 0x100, appearance 0x400, status 0x800,
str/dex/int 0x900/0xa00/0xb00, exp 0xc00, HP 0xe00), but **little-endian** here
(ScummVM/DOS). actor 1 = the Avatar (obj_n 410). NOTE the objlist region is
`objlist_size` bytes; a ScummVM `.000` then carries trailing ScummVM metadata
(thumbnail etc.) after it — a writer MUST preserve those trailing bytes.

## Writing a save (round-trip)
Strategy (host tool /tools/nuviewrite, and the model for the eventual on-target
writer): **preserve every region we don't model byte-for-byte, and re-serialize
from decoded values the two regions we DO model** — the 64 surface super-chunks
(every 8-byte object record) and the objlist actor arrays (position, appearance,
status, str/dex/int, HP) — back to little-endian. Encoding is the exact inverse of
the decode:
- object record (8 bytes): `b0=status`, position into b1..b3 and obj_n/frame into
  b4..b5 as below, `b6=qty`, `b7=quality`; preserve superchunk membership + order.
- position (3 bytes): `b0=x&0xff`, `b1=((x>>8)&3)|((y&0x3f)<<2)`, `b2=((y>>6)&0xf)|((z&0xf)<<4)`
- appearance (2 bytes): `b0=obj_n&0xff`, `b1=((obj_n>>8)&3)|((frame&0x3f)<<2)`
- 1-byte arrays (status/str/dex/int/HP) write verbatim.

Validated: a decode→encode round-trip of the user's `ultima6.000` — rebuilding ALL
8343 surface object records AND the objlist actor arrays from decoded values —
reproduces the file **byte-for-byte** (`make save-roundtrip`); a `--move-avatar`
patch changes only the avatar's packed position bytes; and ScummVM **loads the
written save** (with our fully-reconstructed surface chunks) and renders the world
+ moved avatar correctly (`make save-check`, build/svm_loaded_moved.png).

## Use in the engine
- /tools (nuviesave) extracts the 64 surface super-chunks (= OBJBLK stream) + the
  objlist from a ScummVM save; mkworld builds the world pack from THAT state
  (objects + actors consistent, matching ScummVM) instead of DOS LZOBJBLK + ST save.
- /tools (nuviewrite) writes a ScummVM-loadable save back out (host round-trip).
- **Pristine template** (tools/mksave, `make save-template`, run by `make world`): the
  shipped `SAVE.000` is SYNTHESIZED on the host from the player's own LZOBJBLK (64 surface
  blocks) + LZDNGBLK (5 dungeon blocks + the pristine OBJLIST) — the inputs Nuvie's own
  new-game path (`SaveGame::load_new`) reads — re-bucketed the way a save from a freshly
  started game is: carried/readied records → the inventory chunk, eggs (obj 335) → the
  eggs chunk, on-map records by `(y>>7)*8+(x>>7)` for z==0 or dungeon chunk z-1, each
  container's contents following it with chunk-local parent links re-indexed; objlist =
  the pristine one with Nuvie's new-game defaults (name @0xf00, str/dex/int 15, exp 0x172,
  level 3 @0xff2, magic 30 @0x13f2). PARITY CHECK: against the user's ScummVM save (itself
  a pristine start plus a little play) the synthesized file matches chunk-for-chunk —
  inventory 142, eggs 1978, all five dungeon chunks, 63 of 64 surface chunks (the throne
  room chunk differs by the 11 objects their game added), objlist 7283. The pack is then
  built from this file (`nuviesave` → mkworld save mode), so the world, the party's
  starting gear/gold/reagents and the save template are ONE state.
- **ScummVM extended-save trailer** (engines/metaengine.cpp v4): `"SVMCR\0"`, u8 version 4,
  u32 LE date `(day<<24)|(month<<16)|year`, u16 LE time `(hour<<8)|minute`, u32 LE playtime,
  pascal description, u8 isAutosave, a THMB block (`"THMB"`, u32 BE size, u8 version 2,
  u16 BE w,h, 9 PixelFormat bytes = bpp 2 + RGB565 losses/shifts, w*h u16 BE pixels), then
  u32 LE = the header's file offset as the LAST four bytes. ScummVM's save list reads it
  from the end; a file without it is not listed. mksave writes one with a 16x10 black
  thumbnail (~400 bytes; ScummVM's own 160x100 costs 32 KB of target RAM per template);
  the on-target writer date-stamps it via GEMDOS Tgetdate/Tgettime (`sav_set_trailer_date`).
- **Proving ScummVM loads a file** (tools/savecheck.sh): swap it into slot 0 and run
  `scummvm --save-slot=0` — Nuvie's "Journey Onward" otherwise loads the config's
  `latest_save` slot (3 on this machine), which silently voided the check for months.
  Verified 2026-09-29 with the slot pinned: the synthesized pristine save (name + moved
  avatar visible) AND a target-written save (dropped plate mail on the throne-room carpet).
- **Streaming rebuild** (`sav_write_rebuilt`, 2026-09-30): the file is written chunk by chunk
  from the template minus the pooled party's records, with the party's inventory regenerated
  from the live model (docs/rules/save_load.md §6); `sav_encode_record` is the record encoder.
  Chunk counts change, so the trailer's header offset is shifted by the growth.
- **On-target writer** (engine/save.c): the engine loads `SAVE.000` as a TEMPLATE at
  runtime (the synthesized pristine save above, or after an in-game load the loaded file),
  patches the regions it models with **byte-wise
  little-endian** encoding (correct on the big-endian 68000 with no swapping), and
  writes the save via the platform file layer. `sav_objlist_off()` walks the chunk
  list to find the objlist; `sav_set_actor_pos()` encodes a packed position.
  Validated on the TT: the engine walks then writes SAVED.000 and ScummVM loads it.
- **Object-world patches (in place, no rebuild).** U6's object DB is a flat fixed
  record set that loads by STATUS, so open/close/take change a record's frame/status
  without shifting counts or offsets: `sav_patch_obj_frame` (door/container new frame
  → byte 5); `sav_patch_obj_taken` (on-map item → status loc 0x00→0x10 in-inventory,
  x→owner); `sav_patch_contained_taken` (an item taken after spilling from a container:
  locate the container's on-map record for its index P, then flip the in-container child
  with parent-link==P and matching obj_n from 0x08→0x10). Validated: door + on-map take
  + crate/child take = 12-byte localized diff, ScummVM loads it (`make save-engine`).
  `sav_patch_obj_frame` grew into `sav_patch_obj_state` (position + frame + optional quality,
  located by the record's ORIGINAL position — sailed boats); `sav_unready_party` /
  `sav_ready_item` / `sav_drop_item` reconcile carried records across the inventory + eggs +
  surface chunks (docs/rules/save_load.md §5). A dropped item's record is relocated on-map
  INSIDE the inventory chunk: Nuvie's `load_super_chunk` places every record by its own
  status + x/y whatever chunk it came from, and our loader now decodes that chunk's on-map
  records too. Verified by loading such a file in ScummVM (tools/savecheck.sh).

## Status: VERIFIED. Read: objects + actor table (position/appearance/HP/str/dex/
## int/party) → world pack → engine render; map & roster match ScummVM. Write:
## byte-identical round-trip with BOTH the 64 surface super-chunks (all 8343 object
## records) and the objlist actor arrays re-serialized from decoded values +
## observable modification + ScummVM loads our reconstructed save.
## On-target: the engine (big-endian 68000) writes a ScummVM-loadable save by
## patching a template — proven for: party positions, clock, per-actor stats/exp,
## avatar identity, and object-world changes in place (door/container frames, on-map
## AND contained-item takes). ScummVM loads engine/SAVED.000 in every case.
## TODO: model + re-serialize the eggs / dungeon-level chunks (opaque today).
