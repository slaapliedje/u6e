# Format: Tile graphics (TILEINDX + MAPTILES + OBJTILES)

Games: U6 / Savage / Martian — verify per game
Implemented by: /dataformats (u6tile.c/.h), /tools (mapdump)
Tracked in: /docs/INDEX.md  ·  Milestone: M2

> Provenance
> - Derived from : reading the user's own DOS U6 files TILEINDX.VGA (4096 B),
>                  MAPTILES.VGA (LZW → 117408 B), MASKTYPE.VGA (LZW → 2048 B), U6PAL.
> - References   : Nuvie TileManager.cpp READ to understand the MASKTYPE-driven
>                  decode (plain vs pixel-block) — not copied; community notes.
> - Tools        : hexdump; /tools mapdump; /tools mkworld (decodes all 2048).
> - Validated by  : tiles 0..255 render as recognizable U6 terrain; the full
>                  0..511 sheet decodes cleanly once MASKTYPE pixel-block tiles
>                  are handled (previously garbage in ~256..511); border tiles
>                  432-439 render as the frayed-papyrus map frame.
> - Confidence   : confirmed (plain + transparent + pixel-block tiles)
> - Last updated : 2026-06-24

## 1. Overview
Tiles are **16×16 pixels, 8 bits/pixel** (palette indices into U6PAL). Map tile
bitmaps live in `MAPTILES.VGA` (LZW-wrapped — see lzw.md); object tile bitmaps
in `OBJTILES.VGA`. `TILEINDX.VGA` maps a tile number to its bitmap offset.

## 2. TILEINDX.VGA
- Stored (not compressed): an array of **2-byte little-endian** values, one per
  tile (4096 B → 2048 tiles).
- Each value is the tile's data offset in **units of 16 bytes** into the
  decompressed bitmap data: `byte_offset = TILEINDX[i] * 16`.
- A full 16×16×8bpp tile is 256 bytes (= 16 units). Tiles 0..7 sit at units
  0,16,32,… (sequential full tiles); some later tiles' offsets advance by only
  1 unit (16 bytes), i.e. compact/shared data (see open questions).

## 3. MAPTILES.VGA (decompressed)
- LZW container; decompresses to 117408 bytes of tile bitmap data.
- A tile's 16×16 pixels are read row-major from `byte_offset`:
  `pixel(i,x,y) = data[ TILEINDX[i]*16 + y*16 + x ]`.
- Map geography (CHUNKS) only references tile indices **0..255**, all of which
  resolve in MAPTILES — that subset is what the world render needs.

## 4. Per-game deltas
Verify per game (tile counts/contents differ; the container scheme should be
shared).

## 5. Validation method
`make mapdump`: render tiles 0..255 to build/tilesheet.png (16×16 grid) and the
full world to build/worldmap.png. Both are visually correct U6 graphics. (A
future pixel-diff against the shipped reference image refdata/st/u6/WORLDMAP.BMP
could make this an automated regression.)

## 5a. MASKTYPE.VGA — per-tile encoding (RESOLVED, was "compact/flat tiles")
`MASKTYPE.VGA` is an LZW container → **2048 bytes, one byte per tile** giving how
that tile's bitmap at `TILEINDX[i]*16` is encoded. Three values:

| Value | Name         | Meaning                                                    |
|-------|--------------|------------------------------------------------------------|
| 0x0   | U6TILE_PLAIN | plain 256-byte 16×16 bitmap                                 |
| 0x5   | U6TILE_TRANS | plain 256-byte bitmap; index **255 = transparent**         |
| 0xA   | U6TILE_PBLCK | **pixel-block RLE** (variable length) — NOT a 256-byte copy |

This is why some TILEINDX offsets advance by <16 units: PBLCK tiles are
variable-length, not full 256-byte windows. Treating every tile as 256 raw bytes
corrupts all PBLCK tiles (e.g. the map-border tiles 432-439 and many in 256..511).

### Pixel-block (0xA) decode
Output is a 256-byte (16×16) buffer pre-filled with **255** (transparent). The
encoded stream:
- byte 0: block count (unused by the decoder).
- then a sequence of runs, each: `[u16 LE disp][u8 len][len pixel bytes]`.
- a write cursor (offset into the 256-byte output, persisting across runs)
  advances by a transparent gap `x` then writes `len` opaque pixels:
  `x = disp % 160 + (disp >= 1760 ? 160 : 0)`; cursor += x; copy len; cursor += len.
- terminated when `len == 0`.

(The odd `%160 / 1760` gap formula is the original's; for 16×16 tiles the gaps
stay in range. Reproduced faithfully — see u6tile.c `decode_pblck`.) PBLCK tiles
are implicitly transparent.

Implemented: `u6tile_set_masktype()` + `u6tile_decode()` in dataformats/u6tile.c;
the host pipeline (mkworld) decodes all 2048 tiles to plain 256-byte blocks in the
world pack, so the engine never needs the MASKTYPE decoder at runtime.

## 6. Combined tile set (all 2048 tiles) — RESOLVED
TILEINDX is a UNIFIED offset table (16-byte units) into one logical blob =
**decompressed MAPTILES.VGA (tiles 0–511) ++ OBJTILES.VGA RAW (tiles 512–2047)**.
- TILEINDX[0..511] land within MAPTILES (117408 B); TILEINDX[512] = byte 117408
  = exactly the start of OBJTILES, which is **stored/raw (NOT LZW** — its
  "header" is not a valid size). So: `combined = unlzw(MAPTILES) ++ OBJTILES`,
  and `pixel(tile,x,y) = combined[TILEINDX[tile]*16 + y*16 + x]`.
- Object graphics: object number → tile via **BASETILE** (`tile = BASETILE[obj_n]
  + frame`); see objblk.md. Object tiles use palette index 255 = transparent.
- Verified: `make objrender` renders a furnished U6 building from object tiles.

## 7. Open questions / unknown fields
- [x] **Compact/flat tiles** — RESOLVED: these are MASKTYPE pixel-block (0xA)
      tiles (variable-length RLE), see §5a. Now decoded correctly.
- [ ] TILEFLAG / ANIMDATA (passability flags, animation) — TILEFLAG decode
      replaces the M5 colour-heuristic passability.
- [ ] ANIMDATA tile-animation source list (Nuvie's U6_ANIM_SRC_TILE) for
      water/flag cycling — when we add tile animation.

## 8. Status
VERIFIED — all 2048 tiles decode correctly across MASKTYPE plain/transparent/
pixel-block encodings (map terrain, object tiles, and the map-window border
tiles 432-439). Tile metadata (TILEFLAG/ANIMDATA) still IN PROGRESS.
