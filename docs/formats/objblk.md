# Format: Object placement (OBJBLK / LZOBJBLK)

Games: U6 / Savage / Martian — verify per game (U6 decoded)
Implemented by: /dataformats (u6obj.c), /tools (objrender, mkworld), /engine
Tracked in: /docs/INDEX.md  ·  Milestone: M6

> Provenance
> - Derived from : the user's DOS LZOBJBLK (decompresses to 72912 B) and the ST
>                  SAVEGAME/OBJBLKxx files; byte analysis + validation below.
> - References   : community knowledge that Nuvie/U6Edit read these (consulted
>                  for orientation; the layout here is from our own bytes).
> - Tools        : /tools/objrender (renders a region with objects).
> - Validated by  : 99.4% of decoded on-map objects fall inside their superchunk
>                  region (with the quad ordering below); rendering a dense
>                  region produces a correctly furnished U6 building.
> - Confidence   : confirmed (record layout + placement); probable (status bits
>                  beyond "contained").
> - Last updated : 2026-06-23

## 1. Container
Objects are grouped by SUPERCHUNK (128×128 tiles = 16×16 chunks). There are 69
blocks: 64 surface superchunks (8×8 grid) + 5 dungeon levels.
- DOS: **LZOBJBLK** = all 69 blocks concatenated, then LZW (see lzw.md).
- ST:  **SAVEGAME/OBJBLKxx** = the blocks as separate files (xx = AA, AB, …).

Each block: `[u16 count][count × 8-byte object record]`. count is little-endian.

### Superchunk storage order (2×2 "quad" order — NOT row-major)
Blocks are stored in 4×4 quads of 2×2 superchunks; within a quad the order is
(0,0),(1,0),(0,1),(1,1). For block index b (0..63):
```
quad = b/4;  within = b%4
scx = (quad%4)*2 + (within & 1)
scy = (quad/4)*2 + ((within>>1) & 1)
```
NOTE: object coords are WORLD-ABSOLUTE, so the engine doesn't need this to place
objects (just decode every block); it matters only for region validation and
for writing blocks back.
NOTE: this quad order is the **original DOS/ST OBJBLK** ordering. A **ScummVM save**
stores its 64 surface super-chunks **row-major** instead (slot `c` ↔ superchunk
`c%8`,`c/8`); see nuvie_save.md. Same record layout, different block order.

## 2. Object record (8 bytes, little-endian fields)
| Field    | Bits / bytes                         | Notes                         |
|----------|--------------------------------------|-------------------------------|
| status   | b0 (8 bits)                          | flags; bit 0x08 = contained/inventory (not on map) |
| x        | b1 | ((b2 & 0x03) << 8)              | world tile X (0..1023)        |
| y        | (b2 >> 2) | ((b3 & 0x0F) << 6)       | world tile Y (0..1023)        |
| z        | b3 >> 4                              | level (0 = surface)           |
| obj_n    | b4 | ((b5 & 0x03) << 8)              | object number (0..1023)       |
| frame    | b5 >> 2                              | frame / orientation           |
| quantity | b6                                   | stack count (low byte)        |
| quality  | b7                                   | per-object datum / stack high byte |

(**CORRECTED 2026-07-02**: quantity is byte 6 and quality byte 7 — earlier
versions of this spec had them swapped. Verified vs ScummVM/Nuvie loadObj
order and by the sample save: a bag holds reagents with b6=10 each, chests
hold gold with b6=5/50/240 — sensible counts; the swap read them as
quality=10 etc. and produced nonsense qty16 values like 38400.)

### Containment (VERIFIED 2026-07-02 vs ScummVM/Nuvie ObjManager)
`status & 0x18` is the object's LOCATION: 0x00 on-map, **0x08 in-container**,
0x10 in-inventory, 0x18 readied. For an **in-container** record the coordinate
bits do NOT hold a position — they hold the PARENT LINK:

    parent = ((y & 0x3f) << 10) | x        (16-bit record index)

i.e. the full 10-bit x field plus the LOW 6 bits of the y field give the
**0-based index of the container's record within the SAME superchunk's record
list** (load order, counting every record in that superchunk). The remaining
y bits and z are don't-care. Contents typically follow their container in the
stream, and nesting works by chaining (a bag inside a chest: the bag's parent
is the chest's index; the bag's contents point at the bag's index). The same
convention applies inside the save's ACTOR-INVENTORY chunk (see nuvie_save.md)
— there an in-INVENTORY record's x = owner actor, but an in-CONTAINER record's
x+y6 = parent record index in that chunk (e.g. gold inside a carried bag).
Verified on the sample save: 369 in-container records across the 64 surface
superchunks all resolve to sane on-map containers (chests/barrels/drawers/
bags with plausible contents — reagents qty 10 in a bag, gold in a chest).

### Stackables: 16-bit quantity
`quantity` is one byte, EXCEPT for stackable object types, where the real
count is 16-bit: **qty16 = (quality << 8) | quantity** (quality is consumed
as the high byte and is not otherwise meaningful for the stack). An object
TYPE is stackable iff the LOOK string of its base tile contains the plural
marker `\` (backslash splits singular\plural in LOOK.LZD — see look.md).
Examples: gold coins (obj 88), arrows, reagents. Non-stackables keep quality
as a per-object datum (e.g. a chest's quality = key id for its lock).

### Container object types (searchable; from Nuvie's object-type table)
chest 98, backpack 99, remains 168, drawer 176, desk 177, barrel 186, bag 188,
basket 191, crate 192, dead body 339 (+ dead animals / mousehole etc.).
Behavior (docs/rules/usecode_use.md): USE on chest/crate/barrel toggles the
open/close frame; SEARCH ("Searching here, you find …") moves every DIRECT
child out of the container onto the container's map tile.

## 3. From object to graphic
`tile = BASETILE[obj_n] + frame`, then draw tile from the combined tile set
(see tiles.md). BASETILE is 2048 bytes = 1024 × u16 (LE): obj_n → base tile.
Object tiles render with palette index 255 = transparent (terrain shows through).

## 4. Per-game deltas
Verify. Surface = z 0; dungeon levels z 1.. (their blocks are the 5 after the 64
surface ones). Object numbers/types differ per game.

## 5. Validation method
`make objrender` decodes all on-map objects and draws the densest region; the
result is a recognisable, furnished U6 building. `mkworld` bakes surface objects
into the world pack; the on-target game draws them over terrain.

## 6. Open questions
- [ ] Full status-bit meanings (temporary/egg/OK-to-take etc.); chest LOCKED
      state (quality = key id; how "locked" is flagged beyond quality!=0).
- [x] Container nesting + parent link — RESOLVED (§2 Containment).
- [ ] NPC/actor records + schedules (SCHEDULE file) — actors vs plain objects.
- [ ] Dungeon-level object coordinates (z>0) extent.

## 7. Status
VERIFIED (surface object placement + rendering; containment link + stackable
qty16 verified vs ScummVM/Nuvie source + the sample save's 369 contained
records). Locked chests / remaining status bits: TODO.
