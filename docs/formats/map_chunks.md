# Format: MAP + CHUNKS (world geography)

Games: U6 / Savage / Martian — shared layout (verified on U6)
Implemented by: /dataformats (u6map.c/.h), /tools (mapdump)
Tracked in: /docs/INDEX.md  ·  Milestone: M2

> Provenance
> - Derived from : reading the user's own DOS U6 files MAP (32256 B) and
>                  CHUNKS (65536 B) + the size arithmetic below.
> - References   : community "Ultima 6 map format" notes (read to understand;
>                  not transcribed).
> - Tools        : hexdump of /refdata; our /tools mapdump renderer.
> - Validated by  : mapdump decodes the surface and renders a RECOGNIZABLE
>                  Britannia (build/worldmap.png) in correct palette colours;
>                  every chunk index < 1024; one tile (ocean) covers ~31%.
> - Confidence   : confirmed
> - Last updated : 2026-06-23

## 1. Overview
The world is a grid of **chunks**; each chunk is an 8×8 block of tiles. CHUNKS
is the set of unique chunk templates; MAP is the arrangement of chunk indices
across the surface and dungeon levels. Surface = 1024×1024 tiles, wrapping
toroidally on both axes (Martian Dreams uses the wrap for its cylindrical
world).

## 2. CHUNKS file
- 1024 chunk templates × 64 bytes = 65536 bytes.
- Each template = 8×8 = 64 **8-bit tile indices**, row-major: `tile[wy*8 + wx]`.
- So map geography references **tile indices 0..255 only** (objects, which use
  the full tile range, live separately in OBJBLK/savegame data).
- (Observed: chunk 0 is all tile 8.)

## 3. MAP file — 12-bit packed chunk indices
Chunk indices are **12-bit, nibble-packed 2-per-3-bytes, little-endian order**:
for bytes B0,B1,B2 → `a = B0 | ((B1 & 0x0F) << 8)`, `b = (B1 >> 4) | (B2 << 4)`.

Layout (sizes confirmed by the exact 32256-byte total):
| Region            | Chunks            | Packed bytes |
|-------------------|-------------------|--------------|
| Surface           | 128×128 = 16384   | 24576        |
| Dungeon level 1–5 | 32×32 = 1024 each | 1536 each (7680) |
| **Total**         |                   | **32256** ✓  |

### Surface ordering — SUPERCHUNKS (the key detail)
The surface is NOT stored as a flat 128×128 row-major array. It is an **8×8 grid
of "superchunks", each 16×16 chunks** (8·16 = 128). MAP lists superchunks
row-major, and the chunks within each superchunk row-major. For world chunk
(cx,cy):
```
scx = cx/16, scy = cy/16      # superchunk (0..7)
icx = cx%16, icy = cy%16      # chunk within superchunk (0..15)
file_index = (scy*8 + scx)*256 + (icy*16 + icx)
```
(Reading it as plain row-major scrambles the map into 16×16-chunk blocks — that
was the first decode bug, caught by the rendered image.)

## 4. Tile lookup
```
surface_tile(x,y):  x,y &= 1023            # toroidal wrap
  cx=x/8, cy=y/8 ; wx=x%8, wy=y%8
  cidx = surface[ superchunk_index(cx,cy) ]
  return CHUNKS[ cidx*64 + wy*8 + wx ]
```

## 5. Per-game deltas
Shared layout (verified U6). Per-game LEVEL semantics (U6 nested 3D dungeons;
SE 4-of-6 + gargoyle level 6; MD toroidal) are a /docs/rules concern
(map_levels.md), not this byte layout.

### Dungeon levels — chunk ordering (RESOLVED)
The 5 dungeon levels (32×32 chunks = 256×256 tiles, 1536 packed bytes each) are **FLAT 32×32 row-major**
(`file_index = cy*32 + cx`), NOT superchunk-blocked like the surface. Verified by rendering level 1 both
ways (mapdump dungeon1_sc/flat): only the flat order gives a coherent dungeon (intact maze corridors +
a symmetric cross-room); superchunk fragments it into 2×2 quadrants. Level model + per-game policy:
docs/rules/map_levels.md. Accessor: `u6map_dungeon_tile`.

## 6. Open questions / unknown fields
- [ ] Confirm Savage/Martian use the identical dungeon-level layout (SE 4-of-6 + gargoyle level 6).

## 7. Status
VERIFIED — surface AND dungeon-level chunk ordering (both rendered coherently: worldmap.png, dungeon1).
