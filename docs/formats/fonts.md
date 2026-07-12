# Fonts — U6.CH (8×8 game font)

The in-game text font: the small fixed-width 8×8 typeface used for the message
scroll, party panel, status line, books/signs, and conversations.

> Provenance
> - Derived from : refdata/{dos,st}/u6/U6.CH inspected (both 2048 bytes, byte-identical layout); glyphs rendered at ASCII indices.
> - References   : Nuvie `fonts/U6Font.cpp` (glyph bit order) and `fonts/FontManager.cpp::initU6()` (which file, how split) READ to understand — not copied.
> - Tools        : host glyph dumper (render each glyph as text), tools/uipreview.c (renders real strings in this font to PNG).
> - Validated by  : rendered ASCII text ("Thou art in Britannia.", mixed case + digits) produces correct, recognisable U6 letterforms (build/uipreview.png).
> - Confidence   : confirmed
> - Last updated : 2026-06-24

## Which file (IMPORTANT)
The game font is **`U6.CH`**, NOT `U6.SET`. `U6.CH` is a raw 2048-byte file with
no compression and no size header. (`U6.SET` is a *different*, LZW-compressed
file and is **not** this 8×8 font — do not confuse them. An earlier decode pass
wasted effort assuming U6.SET was the font; it is out of scope here.)

All three games ship a `U6.CH` (U6, Savage Empire, Martian Dreams); the layout
below is assumed shared until a per-game decode proves otherwise.

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
- Per-game deltas: confirm SE/MD `U6.CH` are layout-identical (very likely).
- Runic/gargish exact code offsets when we render gargoyle dialogue (M6 talk).
- Is there a separate larger/title font (U6.SET? WOU font in Nuvie) needed for
  intro/menus? Out of scope until those screens are implemented.

## Status
VERIFIED (confirmed) — English 8×8 glyphs render correctly; runic range present
but not yet exercised on-screen.

## Implemented by
- `tools/mkworld.c` (ships U6.CH in the world pack)
- `render/gamedemo.c`, `tools/uipreview.c` (`ui_text` / `text` rendering)
