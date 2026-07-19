# Portraits — PORTRAIT.A / PORTRAIT.B / PORTRAIT.Z

Character face portraits shown in conversations (and the avatar's own portrait).

> Provenance
> - Derived from : refdata/{dos,st}/u6/PORTRAIT.A/.B/.Z inspected (header offsets, item counts, decompressed sizes); 32 portraits decoded + rendered to a contact sheet (recognisable faces).
> - References   : Nuvie `portraits/PortraitU6.cpp` READ to understand which files, the 56×64 size, the A/B/Z split and index math — not copied.
> - Tools        : tools/portdump.c (contact sheet), tools/uipreview.c (portrait in the conversation UI); built on our u6lib + u6lzw + u6pal.
> - Validated by  : 32/32 test items decompress to exactly 3584 bytes and render as correct U6 faces under U6PAL; item counts match the A/B split (98+96 = 194 NPC portraits) Nuvie documents.
> - Confidence   : confirmed
> - Last updated : 2026-06-24

## Files & roles
| File        | Holds                                    | Items |
|-------------|------------------------------------------|-------|
| PORTRAIT.A  | NPC portraits, low half                  | 98    |
| PORTRAIT.B  | NPC portraits, high half                 | 96    |
| PORTRAIT.Z  | Avatar portrait variants                 | 12    |

194 NPC portraits total (A: 0–97, B: 98–193). `.Z` holds the selectable
avatar faces.

## Container
Each file is a **library** (see [library.md](library.md)) with **4-byte
little-endian offsets and NO size prefix**: the first offset equals the index
size, so `count = first_offset / 4`. Verified headers:

| File       | bytes  | first offset (LE) | count |
|------------|--------|-------------------|-------|
| PORTRAIT.A | 142495 | 392               | 98    |
| PORTRAIT.B | 139346 | 384               | 96    |
| PORTRAIT.Z | 20249  | 48                | 12    |

Open with `u6lib_open(.., offset_width=4, has_size_prefix=false)`.

## Item payload
Each library entry is an **LZW-compressed** block (see [lzw.md](lzw.md)) that
decompresses to exactly **3584 bytes = 56 × 64**, one **8-bit palette index per
pixel**, row-major, top-left origin. Dimensions are fixed (width 56, height 64).

Pixels index the standard in-game palette **U6PAL** (see [palette.md](palette.md));
no per-portrait palette. (Nuvie additionally *dithers* portraits for EGA/CGA
output — that is a low-colour fallback. Our VGA/256-colour path uses the raw
indices directly. The ST 16-colour target will dither at build time in /tools,
not at runtime.)

## Savage / Martian: `FACES.LZC` / `MDFACES.LZC` (portraits as U6 shapes)

SE/MD do **not** use the PORTRAIT.A/.B/.Z LZW-block scheme. Their portraits live
in `FACES.LZC` (SE) / `MDFACES.LZC` (MD): an [.LZC](lzc.md) container (SE items
flag 0x02 stored, MD 0xC1) whose **each present item IS a U6 shape file**
(see [shapes.md](shapes.md)) — byte 0 = total size, a 1-entry offset table, then
one shape with the rightX/leftX/upperY/lowerY header + plain/RLE pixel blocks.

- **SE faces are 79×85** (rightX=78, lowerY=84; leftX=upperY=0), SEPAL indices,
  index 0 = background. 83 present of 92 slots. VERIFIED: 24-face contact sheet
  renders recognisable SE characters (Jimmy Malone in his fedora, tribal warriors
  with face paint, a lizard-man, an Aztec totem, a parrot).
- The existing `u6shape_decode` handles them unchanged — `u6lzc_entry` → decode
  shape 0.
- Host pipeline: `tools/mkworld.c::convert_faces` decodes every present same-size
  face and ships them in the SE/MD CORE pack's optional **`CFAC`** section
  `[4 "CFAC"][u16 nface][u16 w][u16 h][nface × w*h indices]` (docs/design/
  multigame.md P2). The loader points `g_seface` at it; the panel draws one.
- Open: the FACES item→NPC mapping (needs the SE actor model); MD `MDFACES`
  dims (likely also 79×85) — confirm when MD boots (P3).

## Index mapping (NPC → portrait)
From the actor model: portrait index = **actor_num − 1**.
- index < 98 → PORTRAIT.A, item = index
- index ≥ 98 → PORTRAIT.B, item = index − 98
- the avatar uses PORTRAIT.Z, item = the chosen avatar-portrait number
  (stored in the savegame object list in the original; selectable at start).
A handful of high indices map to special/temporary portraits (gargoyle, guard,
wisp, Exodus) and some ranges (shrines/temple) have no portrait — wire these in
the actor layer when actors exist. NOT yet implemented here.

## Decode recipe (host or engine)
1. `u6lib_open(lib, file, len, 4, false)`
2. `blob = u6lib_entry(lib, item, &sz)`
3. assert `u6lzw_unpacked_size(blob, sz) == 3584`
4. `u6lzw_decompress(blob, sz, dst, 3584)` → 56×64 indexed pixels
5. draw with U6PAL (transparent index not used — portraits are full rectangles)

## Validation method
`make portdump` decodes 16 from .A + 8 from .B + 8 from .Z into a contact-sheet
PNG (build/portraits.png): all 32 decode and show correct faces. `make uipreview`
places a decoded portrait in the conversation panel of the authentic UI.

## Open questions
- Per-game deltas: SE/MD use the same container; confirm dimensions (likely
  identical) when those games are wired.
- Special/temporary portrait indices (gargoyle/guard/wisp/Exodus) and the
  no-portrait ranges — handle in the actor layer (M6 NPC work).
- Avatar portrait selection source in OUR save format (vs original objlist).

## Status
VERIFIED (confirmed) — container + 56×64 LZW payload decode correct; NPC→portrait
index mapping understood but pending the actor model.

## Implemented by
- `tools/portdump.c` (contact sheet), `tools/uipreview.c` (UI integration)
- engine runtime loader: TODO (load on demand from user's PORTRAIT.* at talk time)
