# Fonts — U6.CH (8×8 game font)

The in-game text font: the small fixed-width 8×8 typeface used for the message
scroll, party panel, status line, books/signs, and conversations.

> Provenance
> - Derived from : refdata/{dos,st}/u6/U6.CH inspected (both 2048 bytes, byte-identical layout); glyphs rendered at ASCII indices.
> - References   : Nuvie `fonts/U6Font.cpp` (glyph bit order) and `fonts/FontManager.cpp::initU6()` (which file, how split) READ to understand — not copied.
> - Tools        : host glyph dumper (render each glyph as text), tools/uipreview.c (renders real strings in this font to PNG).
> - Validated by  : rendered ASCII text ("Thou art in Britannia.", mixed case + digits) produces correct, recognisable U6 letterforms (build/uipreview.png).
> - Confidence   : confirmed
> - Last updated : 2026-08-15 (U6.SET front-end font section added)

## Which file (IMPORTANT)
The in-GAME font is **`U6.CH`**, NOT `U6.SET`. `U6.CH` is a raw 2048-byte file with
no compression and no size header. `U6.SET` is a *different* file — the LZW-compressed
**proportional front-end font** (intro narration, main menu text, character creation:
everything nuvie's cutscene `image_print` draws) — see "U6.SET" below. The two are
distinct typefaces (60 of the 94 printable glyphs differ), so front-end text set in
U6.CH — even trimmed to proportional widths — is NOT what the original shows.

**U6 only** ships `U6.CH`. Savage Empire and Martian Dreams instead use a
**proportional "WOU" font** (`SAVAGE.FNT` / `FONTS.LZC`) — a different format,
documented in its own section below. The 8×8 layout here is U6's.

## Container facts
- Size: exactly **2048 bytes**. Raw — no LZW, no 4-byte size header.
- Endianness: not applicable. Each glyph row is a single byte (a bitmask), so
  there is no multi-byte field to byte-swap. Safe to load verbatim on the
  big-endian 68000 target.
- Layout: **256 glyphs × 8 bytes** = 2048 bytes, glyphs stored back-to-back.

## Glyph format
- Each glyph is **8×8 pixels, 1 bit per pixel**.
- 8 bytes per glyph, one byte per row, **top row first**.
- Within a row byte, bit **0x80 is the LEFTMOST pixel** (MSB-left), 0x01 the
  rightmost. A set bit = foreground (drawn in the chosen colour); a clear bit =
  background (not drawn — transparent over whatever is behind).

```
glyph g, row r (0=top):   byte = U6CH[g*8 + r]
pixel (col c, 0=left):    set  = byte & (0x80 >> c)
```

## Character mapping
- **Glyph index == ASCII code.** Index 65 = 'A', 97 = 'a', 48 = '0', etc.
- Glyphs **0–127**: standard ASCII (upper + lower case, digits, punctuation).
- Glyphs **128–255**: the runic / gargoyle (gargish) alphabets, indexed by the
  same code minus 128 in Nuvie's split (it constructs a second font object over
  bytes [128*8 ..]). For our engine we treat U6.CH as one flat 256-glyph table
  and select the runic range by offsetting the code by 128 when rendering
  gargish text.

## Rendering (engine)
A glyph is drawn into the chunky 8-bit backbuffer pixel-by-pixel; set bits get
the foreground palette index, clear bits are left untouched (transparent). See
`render/gamedemo.c::ui_text` and `tools/uipreview.c::text` — both iterate rows
and test `bits & (0x80 >> col)`.

## U6.SET — the U6 front-end (cutscene / chargen) proportional font

Provenance: refdata/dos/u6/U6.SET decoded with our `u6lzw` + a host glyph dumper (2026-08-15);
nuvie `fonts/wou_font.cpp` READ to learn which header bytes mean what (`WOUFont` is the class
nuvie's `ScriptCutscene` uses for U6, initialised from `u6.set`); validated by rendering the
intro narration, the gypsy questions and the chargen vellum screens on the emulated Falcon —
letterforms + line breaks match the DOS captures. Confidence: **confirmed**.

- **Container**: bare U6 [LZW](lzw.md) (4-byte size header): 1168 B packed → **7004 B**.
- **Layout = the WOU layout below** (same family as SE/MD's fonts), with U6 values:
  | Offset | Size | Field | U6.SET value |
  |--------|------|-------|--------------|
  | 0 | 1 | glyph height | 8 |
  | 1 | 1 | — | 0 |
  | 2 | 1 | **ink pixel value** — a glyph byte equal to this is INK; anything else is transparent | 15 |
  | 3 | 1 | — | 0 |
  | 4..259 | 256 | width[ch] (advance = bitmap width) | 3..9 (`' '`=4, `'i'`=5, `'A'`=7, `'m'`/`'W'`=8) |
  | 260..515 | 256 | glyph offset LOW byte | |
  | 516..771 | 256 | glyph offset HIGH byte (`off = lo | hi<<8`, absolute) | |
  | off[ch] | w×8 | glyph pixels, row-major, 1 byte per pixel | |
- Every printable glyph has exactly **one blank leading column and no trailing column** (the
  advance IS the width; a 1-px inter-glyph gap is built into each glyph). Default text colour
  in nuvie for U6 = palette **0x48** (dark parchment ink); highlight 0x0c.
- Note the SE/MD convention below reads the *background* value at byte 3 (ink = anything
  else); for U6.SET use byte 2 as the ink value (glyphs contain only 0 and 15).

**Pack (`tools/mkworld.c`)**: shipped decoded in the front-end block after the vellum
section: `[u8 height (0 = absent)][256 × u8 width][256 × 8 × u16 row bits, big-endian, bit 15 =
leftmost column]` (4353 B). Engine: `g_fset` — `pglyph_w`/`ptext`/`ptextw`/`pwrap`/`fe_print`
draw with it whenever the pack has it (else the old U6.CH-trimmed fallback), so ALL front-end
text (narration pages, menu notes, gypsy questions, chargen screens) is set in the true font;
`ui_text` (in-game HUD/scroll) stays 8×8 U6.CH. On the ST the pointer is nulled with the rest of
the front-end when U6FE is freed.

## Savage / Martian: the WOU proportional font (`SAVAGE.FNT` / `FONTS.LZC`)

Games: Savage Empire (`SAVAGE.FNT`) / Martian Dreams (`FONTS.LZC`) — SE/MD only.
Confidence: **confirmed** (SE decoded + rendered on-target). Derived clean-room
from the user's own `SAVAGE.FNT` byte-by-byte (headers, widths, offset table,
glyph pixels); validated by converting to 8×8 and rendering "The quick SAVAGE
fox 0123456789" (host, legible) and "The Savage Empire" on the Falcon panel.

- **Container**: the file is an [.LZC](lzc.md) single item (SE flag 0x01 = LZW,
  MD 0x20 = LZW) — decompress with our `u6lzw` first. SE `SAVAGE.FNT` → 7188 B.
- **Decompressed layout**:
  | Offset | Size | Field | Meaning |
  |--------|------|-------|---------|
  | 0      | 1    | height | pixel height (8) |
  | 1..2   | 2    | —     | (0; unused) |
  | 3      | 1    | transp | the "background" pixel value (0x48 = 72) |
  | 4      | 256  | widths | per-char advance/bitmap width (chars 0..255; Σ = total columns) |
  | 260    | 256  | off_lo | glyph-offset LOW bytes |
  | 516    | 256  | off_hi | glyph-offset HIGH bytes → `off[ch] = off_lo[ch] | off_hi[ch]<<8` = absolute file offset of glyph ch |
  | off[ch]| w×8  | glyph | `width[ch]` columns × 8 rows, **row-major**, 1 byte/pixel |
- **Pixel**: a byte is INK iff it is **not** the transparent value (`!= transp`);
  else transparent. (SE glyphs use only 0 and 72; ink = the 0 bytes.)
  `off[ch] == 4 + 8·Σwidths[0..ch-1] + 768`, i.e. glyphs pack end-to-end from
  a base of 772; the offset table is redundant with the widths but authoritative.
- Widths are ≤ 9 (one glyph is 9); all others ≤ 8.

### Conversion for the engine
`tools/mkworld.c::convert_wou_font` folds the WOU font into the **U6.CH 8×8
1bpp** format (bit set where a pixel is ink, columns clipped to 8) and ships it
in the SE/MD CORE pack's optional **`CFNT`** section `[4 "CFNT"][2048 bytes]`
(docs/design/multigame.md P2). The loader points `g_font` at it, so the shared
`ui_text`/`ptext` renderer draws SE/MD text with no renderer change. (A future
enhancement could render the WOU font proportionally at its native widths.)

## Distribution (clean-room)
`U6.CH` is original game data: it is **loaded at runtime**, never committed. The
host asset pipeline (`tools/mkworld.c`) copies the user's `U6.CH` verbatim into
the world pack (section "font", 2048 bytes; pack magic bumped `U6W2`→`U6W3`).
The engine reads it from the pack; it is not embedded in source.

## Validation method
`tools/uipreview.c` renders real strings (mixed case, digits, punctuation) in
this font to `build/uipreview.png`; the letterforms match U6's on-screen font in
the reference screenshots. A per-glyph dump confirms 'A','V','a','6','!' etc.
render correctly at their ASCII indices.

## Open questions
- Runic/gargish exact code offsets when we render gargoyle dialogue (M6 talk).
- MD `FONTS.LZC`: confirm the WOU layout is byte-identical to SE (very likely;
  same engine + `.LZC` compressed item) — decode when MD boots (P3).
- WOU: render proportionally at native widths (nicer than the 8×8 clip) once
  SE/MD get their own HUD text surfaces.

## Status
VERIFIED (confirmed) — English 8×8 glyphs render correctly; runic range present
but not yet exercised on-screen.

## Implemented by
- `tools/mkworld.c` (ships U6.CH in the world pack)
- `render/gamedemo.c`, `tools/uipreview.c` (`ui_text` / `text` rendering)
