# shapes.md — U6 SHAPE files (*.shp: menus, title, intro, gypsy, end)

Games: Ultima VI (`MAINMENU.SHP`, `TITLES.SHP`, `INTRO.SHP`, `INTRO_1/2/3.SHP`,
`GYPSY.SHP`, `END.SHP`, `MONTAGE.SHP`, …). Savage Empire / Martian Dreams use
the same shape container. (The Atari-ST port ships the same images as `*.NEW`
with BIG-ENDIAN words; we decode the DOS little-endian `*.SHP`, matching the
engine's DOS/VGA asset path — REFDATA = refdata/dos/u6.)

> Provenance
> - Derived from : refdata/dos/u6/*.SHP inspected + decompressed with our u6lzw;
>   the shape/pixel-block layout READ from u6tech.txt (nodling/oocities U6 tech
>   doc) and the container index worked out from the real files.
> - References   : u6tech.txt (shape header rightX/leftX/upperY/lowerY + pixel
>   block double_length/x/y); Nuvie U6Shape READ for understanding (not copied).
> - Tools        : tools/shapedump (LZW → container → shapes → PNG via U6PAL).
> - Validated by  : GOLDEN IMAGE — MAINMENU.SHP renders the U6 main-menu text
>   ("Introduction / Create a Character / Transfer a Character / Acknowledgements
>   / Journey Onward") and TITLES.SHP shape 0 renders the blue "Ultima VI" logo,
>   both correct in the U6PAL palette (build/menu_00.png, build/titles_00.png).
> - Confidence   : confirmed (container + shape header + RLE pixel blocks).
> - Last updated : 2026-07-08

## Container (whole-file LZW; then an offset table)

The whole file is the standard U6 **LZW** container (lzw.md — 4-byte LE size
header + bit stream). Decompress first. The decompressed data is:

| off | size        | field                                             |
|-----|-------------|---------------------------------------------------|
| 0   | u32 LE      | total decompressed size (bytes)                   |
| 4   | N × u32 LE  | shape offsets: offset[i] = byte position of shape i (from byte 0) |
| …   | —           | shape data                                        |

**N (number of shapes) = (offset[0] − 4) / 4.** (offset[0] is the byte where the
first shape starts; everything from byte 4 up to there is the offset table.)
Observed: MAINMENU 1 shape, TITLES 2, INTRO 13 frames, GYPSY 200.

## Shape

Each shape (at byte offset[i]) begins with a 4-word header (LE):

| off | field   | meaning                                        |
|-----|---------|------------------------------------------------|
| 0   | u16 rightX | pixels to the RIGHT of the hotspot           |
| 2   | u16 leftX  | pixels to the LEFT of the hotspot            |
| 4   | u16 upperY | pixels ABOVE the hotspot                     |
| 6   | u16 lowerY | pixels BELOW the hotspot                     |

- **Width = leftX + rightX + 1, Height = upperY + lowerY + 1.**
- The **hotspot** is at canvas pixel (leftX, upperY).

Then a stream of **pixel blocks**, until a terminator:

| off | field | meaning                                                     |
|-----|-------|-------------------------------------------------------------|
| 0   | u16 dl | `double_length`: `len = dl >> 1`, `flag = dl & 1`. dl==0 ENDS the shape. |
| 2   | s16 x  | block start X, relative to the hotspot                     |
| 4   | s16 y  | block start Y, relative to the hotspot                     |
| 6   | data   | `len` pixels laid RIGHTWARD from canvas (leftX+x, upperY+y) |

- **flag == 0 (plain):** the next `len` bytes are literal palette indices.
- **flag == 1 (RLE):** decode until `len` pixels are produced — read a control
  byte `b`, `count = b >> 1`; if `b & 1` (odd) the next ONE pixel byte repeats
  `count` times, else the next `count` bytes are literal pixels.

Pixels are **U6PAL** palette indices (palette.md). Index 0 = background/black for
these full images (transparency for sprites is per-tile — see masktype in
tiles.md; the menu/title/intro screens are opaque). Multi-shape files (INTRO,
GYPSY) are ANIMATION FRAMES / element sets shown in sequence.

## Validation method
tools/shapedump decompresses, walks the offset table, decodes each shape into a
W×H indexed canvas, and writes a PNG per shape via U6PAL. MAINMENU/TITLES match
the original screens (golden images above).

## Open questions
- INTRO/GYPSY frame TIMING + which frames compose vs animate (the front-end
  sequencer decides; screen positions for sub-shapes come from the intro logic,
  not the file).
- Exact background/transparent index convention per screen (0 works for the
  validated menus).

## Status
VERIFIED (container + shape header + plain/RLE pixel blocks; golden-validated on
MAINMENU + TITLES). Feeds the front-end (title/main-menu/intro) + tools/shapedump.
