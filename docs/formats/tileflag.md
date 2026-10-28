# TILEFLAG — per-tile property flags + object weights (U6)

Games: Ultima VI (`TILEFLAG`, 7168 bytes, uncompressed). Savage/Martian have
their own equivalents (see Nuvie docs/martian/tileflags.txt).

> Provenance
> - Derived from : refdata/dos/u6/TILEFLAG (7168 bytes) inspected; layout +
>   bit meanings understood by READING Nuvie's tileflag notes + ObjManager.
> - References   : Nuvie docs/ultima6/tileflag.txt, ObjManager::load_weight_table
>   / can_get_obj, and MapWindow::drawTile/drawTopTile/drawObjs for the per-tile
>   toptile + multi-tile draw order (read for understanding, not copied).
> - Tools        : our tools/mkworld (reads it to precompute a gettable table).
> - Validated by  : Get verb on-target only succeeds for movable items (torch,
>   food, etc.) and fails on fixed scenery (walls, large furniture).
> - Confidence   : confirmed (weights + the bits we use); some bits still guessed.
> - Last updated : 2026-10-27

## Overall layout (4 blocks, total 0x1C00 = 7168 bytes)

| offset  | size | block        | index by   | contents                         |
|---------|------|--------------|------------|----------------------------------|
| 0x0000  | 2048 | tileflags1   | tile (0..2047) | bit0 Wet, bit1 Impassable, bit2 Wall, bit3 Damaging, bit4-7 directional/impedance |
| 0x0800  | 2048 | tileflags2   | tile       | bits0-1 light level, bit2 boundary, bit3 look-through boundary, bit4 **Foreground (toptile)**, bit5 no-shoot-through, bit6-7 size (double width/height) |
| 0x1000  | 1024 | **weights**  | obj_n (0..1023) | object weight, stored ×10 (byte/10 = stones) |
| 0x1400  | 2048 | tileflags3   | tile       | bit0 Warm, bit1 Support (can place on top), bit2 Breakthrough (force passable), bit3 **Vanish/Replicate-able** (the reference's "Ge": the Vanish, Reappear and Replicate spells act only on such objects — 177 tiles), bit4 Ignore, bit5 Background, bit6-7 article ("","a","an","the") |

(Endianness is per-byte; no multi-byte fields here.)

## Derived: can an object be picked up ("Get")?
Per ObjManager::can_get_obj:
- weight **0** → not gettable (fixed scenery — walls, terrain features).
- weight **255** (0xFF) → movable but not gettable (big furniture, etc.).
- object whose tile is a **toptile** (tileflags2 bit4) → not gettable.
- otherwise → gettable.

So: `gettable(obj_n) = w != 0 && w != 0xFF && !(tileflags2[basetile(obj_n)] & 0x10)`
where `w = weights[obj_n]`.

**Movable** (M)ove — rules/usecode_use.md): `w != 0`; weight 255 is movable but not gettable. u6e
ships the weight block verbatim in the world pack as the magic-guarded `U6WB` section
([4 "U6WB"][1024 weights], after U6W5); an engine without it falls back to "movable = gettable".

## Rendering flags (tileflags2) — decoded from real data

`tileflags2` (offset 0x800, one byte per **tile** 0..2047) drives sprite layering.

### bit4 (0x10) — Foreground / toptile
The tile is drawn **on top of** movable objects and actors. A figure on the same
cell appears *behind* it. Used for the parts of furniture that should occlude a
seated/standing NPC (e.g. a throne's arms/front), table fronts, etc.

**The decision is PER TILE, not per object** (this is the crux — verified against
Nuvie's MapWindow::drawTile/drawTopTile, read for understanding only). Objects are
drawn in TWO passes around the actor layer:
1. LOWER pass — for every object, blit only its tiles whose toptile bit is **0**.
2. actors + avatar.
3. TOP pass — for every object, blit only its tiles whose toptile bit is **1**.

A multi-tile object therefore splits across passes by each tile's own flag: a
throne whose back/seat are non-toptile draws *behind* the seated NPC, while its
arms/front (toptile) draw *over* him — so he sits IN it. **No vertical pixel
offset is applied to seated sprites** (Nuvie applies none; correct layering alone
produces the seated look — do not "nudge" the sprite).

### bits6-7 (0xC0) — multi-tile object size
Empirically `(flags2>>6)&3`, cross-checked against LOOK names and tile bitmaps
(tools/tflagprobe):

| value | bit | meaning      | sample objects (names)            |
|-------|-----|--------------|-----------------------------------|
| 0     | —   | 1×1 single   | helms, most items                 |
| 1     | 0x40| double **height**| bookshelf, chest of drawers (tall) |
| 2     | 0x80| double **width** | throne, tapestry (wide)           |
| 3     | 0xC0| **2×2**      | bed, vat, wine cask, loom         |

So **bit6 (0x40) = double-HEIGHT, bit7 (0x80) = double-WIDTH.**

> CORRECTED 2026-06-28 — these were previously documented (and implemented)
> **swapped** (bit6=width / bit7=height), which drew every multi-tile object's
> extra cells one tile up-and-right of correct (LB's throne fell apart; the
> archway too). Verified against Nuvie `core/tile_manager.cpp`:
> `if (flags2 & 0x40) dbl_height = true;  if (flags2 & 0x80) dbl_width = true;`
> The DRAW offsets below (width→left, height→up) were already correct; only the
> bit→meaning mapping was wrong.

**Anchor + draw convention** (the flag lives on the ANCHOR tile T, which is the
tile placed at the object's map position; the extra tiles are the *previous*
tile numbers and the object extends UP and to the LEFT):
- double-width : T at (x,y) = right;  draw **T-1** at (x-1, y).
- double-height: T at (x,y) = bottom; draw **T-1** at (x, y-1).
- 2×2          : T at (x,y) = bottom-right; **T-1** = (x-1,y), **T-2** = (x,y-1),
  **T-3** = (x-1,y-1).

The extra tiles carry their own per-tile flags (e.g. a throne's upper-back tile
has size=0 but may itself be Foreground); the size bit only marks the anchor.
Confidence: bit→meaning **confirmed against Nuvie source** (tile_manager.cpp);
the T-1/T-2/T-3 ordering (width decrement first, then height, then corner) also
matches Nuvie drawTile; validated by the throne-room capture matching ScummVM.

Worked example — the throne (obj tiles 1200..1203). The save places two
double-WIDTH records: 1201 at (307,347) and 1203 at (308,348). Each draws its
anchor at (x,y) plus its previous tile one cell LEFT:
- 1201 (anchor, fg, double-WIDTH) + 1200 (its left part, fg) at (x-1,y)
- 1203 (anchor, double-WIDTH)     + 1202 (its left part)    at (x-1,y)
=> tiles land at (306,347)=1200,(307,347)=1201 and (307,348)=1202,(308,348)=1203,
forming the throne with LB seated at (307,348). (Drawing them as double-HEIGHT
instead put the left parts UP at (306,347)→(307,346) etc. — the old bug.)
A seated NPC at the throne cell is drawn, then the foreground throne parts draw
over the NPC's lower body while the back rises into the cell above — so the
figure sits *in* the throne instead of covering it.

## Use in the engine
- /tools (mkworld) reads TILEFLAG and precomputes a 1-byte-per-obj_n `gettable`
  table into the world pack (so the runtime needs no flag math). The engine's
  object store (engine/objects.c) consults it for the Get verb.
- /tools (mkworld) also ships the raw `tileflags2[2048]` block in the world pack
  (U6WF+); the renderer (render/gamedemo compose_map) uses bit4 (foreground pass)
  and bits6-7 (multi-tile draw) per the convention above.
- Passability for movement is derived separately today (from the map tiles'
  palette in mkworld); tileflags1 bit1/bit2 are the authentic source and can
  replace that heuristic later.

## Open questions
- Object blocking (2026-10-26): an object blocks movement iff its tile's tileflags1 bit1
  (Impassable) is set — the reference's `tile.passable`. The Wall bit alone does NOT block: of
  the 19 tiles that carry Wall without Impassable, all belong to doorways (301), arches (302/304)
  and the OPEN secret door frames (334 f1/f3), all walked through. Terrain is unaffected (every
  map tile with Wall also has Impassable). Forced passability (tileflags3 bit2: drawbridge spans)
  stays as before.
- bit3 of tileflags3 (2026-10-27): the Vanish / Replicate flag (rules/magic_skills.md §5f) — the whole
  block ships as the magic-guarded `U6W3` section ([4 "U6W3"][2048], after U6WB).
- bit1 of tileflags3 (2026-10-27): the Support / "can place on top" flag — 34 object tiles in U6 (tables,
  end tables, the cutting table, stone tables, book stands, altars, the CLOSED crate frame), no map tile;
  M)ove lets a pushed thing land on such an object (rules/usecode_use.md M)ove).
- Article bits (tileflags3 6-7): consumed by L)ook since 2026-10-27 — "Thou dost see " + article + name
  (rules/usecode_use.md "Clock and sundial"); the data's quirks ("a earth") are printed as they are.

## Status: VERIFIED (weights + gettable; some flag bits guessed)
