# Game-screen background — PAPER.BMP (U6Bmp container)

The full-screen game background: the papyrus page with frayed/burnt edges that
frames the whole UI, plus the unfurled-scroll box (bottom-right) that holds the
conversation/message text. The map window, portraits, party roster, and text
are composited *over* this image at runtime.

> Provenance
> - Derived from : refdata/{dos,st}/u6/PAPER.BMP inspected; decompressed to 320×200 and rendered with U6PAL (papyrus frame + scroll box visible).
> - References   : Nuvie `files/U6Bmp.cpp` (container shape) and `Background.cpp` (which file, U6 border_width=158) READ to understand — not copied.
> - Tools        : tools/bmpdump.c (decode → PNG); tools/uipreview.c (used as the UI background); built on our u6lzw + u6pal.
> - Validated by  : decompresses to 64004 bytes → width=320, height=200, 64000 pixels; renders as the recognisable U6 papyrus border + scroll.
> - Confidence   : confirmed
> - Last updated : 2026-06-24

## Container: "U6Bmp"
A simple whole-file image used for full-screen backgrounds (distinct from `.shp`
shapes, which are a different container — see shapes.md when written).

- The **entire file is LZW-compressed** (see [lzw.md](lzw.md); 4-byte LE size
  header then the stream). Decompress the whole file first.
- Decompressed layout:

| Offset | Size      | Field   | Notes                              |
|--------|-----------|---------|------------------------------------|
| 0      | u16 LE    | width   | 320 for PAPER.BMP                  |
| 2      | u16 LE    | height  | 200 for PAPER.BMP                  |
| 4      | width×height | pixels | 8-bit palette indices, row-major, top-left origin |

- Pixels index the in-game palette **U6PAL** ([palette.md](palette.md)). No
  per-image palette.

Verified for PAPER.BMP: file 3881 bytes → unpacked 64004 = 4 + 320×200.

## How U6 uses it (composition)
PAPER.BMP is drawn as the screen background, then UI elements overlay it:
- **map window** 160×160 over the top-left papyrus (origin ~8,8);
- **portrait + name** over the top-right papyrus during conversation (the party
  roster occupies the same area otherwise);
- **conversation/message text** inside the unfurled-scroll box (bottom-right);
- **date / wind** status text over the bottom-left papyrus.

Nuvie notes a U6 `border_width` of 158 (the right-hand chrome width) and blits
the right border_width columns of the bmp as the right edge; for our engine we
simply use the whole 320×200 image as the static background and composite over
it, which is equivalent and simpler.

## Target presentation (resolved)
The UI is authored at native **320×200**. ST/Falcon display that natively; on
the **TT** (TT-low = 320×480) it is **letterboxed** — placed 1:1, vertically
centred, with black bands top/bottom (no scaling). See render notes / milestone.

## Open questions
- Other U6Bmp files (intro/menu/end backgrounds) — same container, different
  dimensions; confirm when those screens are implemented.
- Per-game: SE/MD ship their own PAPER.BMP equivalents (same container).
- The command-icon bar graphics (sword/etc. along the bottom) are a SEPARATE
  asset (icon tiles), not part of PAPER.BMP — decode when wiring the command bar.

## Status
VERIFIED (confirmed) — U6Bmp container + PAPER.BMP 320×200 decode correct; used
as the UI background in the host preview.

## Implemented by
- `tools/bmpdump.c` (decode/validate), `tools/uipreview.c` (UI background)
- engine runtime: load once at startup as the static background (TODO on-target)
