# Format: Palette (U6PAL) + cutscene/convert palettes

Games: U6 / Savage / Martian — verify per game
Implemented by: /dataformats (u6pal.c/.h), /tools, /render
Tracked in: /docs/INDEX.md  ·  Milestone: M2 / M7

> Provenance
> - Derived from : reading the user's own DOS U6 file U6PAL (1024 B).
> - References   : community palette notes (read to understand; not transcribed).
> - Tools        : hexdump + value-range scan; /tools mapdump (renders with it).
> - Validated by  : entries 0–7 match the standard EGA order (black, blue,
>                  green, cyan, red, magenta, brown, light-grey) as 6-bit RGB;
>                  max component in the first 768 bytes is exactly 63 (6-bit);
>                  rendering the world map with it yields natural terrain colours.
> - Confidence   : confirmed (U6PAL main palette)
> - Last updated : 2026-06-23

## 1. Overview
`U6PAL` holds the in-game 256-colour palette used for tiles/world rendering. The
engine animates water/lava/etc. by colour-CYCLING ranges of this palette (see
palette_cycling.md, M7); the cycling metadata is separate from this layout.

## 2. Byte layout
- 256 colours × 3 bytes (R,G,B) = **768 bytes** of palette data.
- Each component is **6-bit VGA (0..63)**. Expand to 8-bit for modern
  rendering, e.g. `out = (v<<2) | (v>>4)`.
- The file is 1024 bytes: the **first 768 are the palette**; the trailing 256
  bytes are NOT palette data (max value 255 there) — purpose TBD, ignore for
  colour lookup.

| Offset      | Size | Type    | Field        | Meaning                  |
|-------------|------|---------|--------------|--------------------------|
| 0 + i*3     | 1    | uint8   | R[i] (0..63) | red,   colour i (6-bit)  |
| 1 + i*3     | 1    | uint8   | G[i] (0..63) | green, colour i          |
| 2 + i*3     | 1    | uint8   | B[i] (0..63) | blue,  colour i          |
| 768         | 256  | bytes   | (non-palette)| purpose unknown          |

## 3. Related palette files (not yet decoded)
- `PALETTES.INT` (4608 B) — cutscene palettes (first 4 bytes have the high bit
  set, so NOT an LZW container).
- `CONVERT.PAL` (1184 B) — used in conversion.
- `ENDPAL.LBM` — endgame palette (IFF/LBM).

## 4. Per-game deltas
Verify — assume each game ships its own U6PAL-equivalent.

## 5. Validation method
`make mapdump` renders the world with this palette; terrain colours look
natural and entries 0–7 equal the known EGA colours. (M7 will add side-by-side
quantisation QA.)

## 6. Open questions / unknown fields
- [ ] The trailing 256 bytes of U6PAL.
- [ ] Decode PALETTES.INT / CONVERT.PAL and the colour-cycling range metadata
      (palette_cycling.md).

## 7. Status
VERIFIED (U6PAL main 256-colour palette). Cutscene/convert palettes: TODO.
