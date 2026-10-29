# U6MCGA.PTR — mouse cursors (U6) + the shape-frame format

Games: Ultima VI (`U6MCGA.PTR`, the VGA/MCGA pointers). Per-mode variants exist
(`CURSORS.EGA/.CGA/.HRC/.TGA`); Savage/Martian use `SECURSOR.PTR`/`MDCURSOR.PTR`.
The inner per-cursor encoding is the standard U6 **shape frame** (also used by
`*.SHP` images), documented here because the cursor is our first consumer.

> Provenance
> - Derived from : refdata/dos/u6/U6MCGA.PTR inspected; decode understood by
>   READING Nuvie Cursor::load_all + U6Shape::load + docs/ultima6/u6shape.txt.
> - References   : Nuvie Cursor.cpp, files/U6Shape.cpp (read, not copied).
> - Tools        : our u6lzw + u6lib (4-byte) in tools/mkworld; host decode→pixels.
> - Validated by  : the decoded arrow renders correctly as the in-game cursor.
> - Confidence   : confirmed
> - Last updated : 2026-06-24

## Container
- Whole-file **LZW** (standard U6 container, lzw.md). Decompress first.
- The decompressed data is a **4-byte-offset library** (library.md, offset
  size 4): item N = cursor N's shape frame. (Item 0 = the arrow pointer.)

## Shape frame (one library item; little-endian)
Header (4 × u16):

| field | meaning                                   |
|-------|-------------------------------------------|
| X1    | pixels to the RIGHT of the hot spot       |
| X2    | pixels to the LEFT of the hot spot        |
| Y1    | pixels ABOVE the hot spot                 |
| Y2    | pixels BELOW the hot spot                 |

Then: `width = X1 + X2 + 1`, `height = Y1 + Y2 + 1`, hot spot at `(X2, Y1)`.
Allocate width×height filled with 255 (transparent).

Pixel blocks follow until a `num == 0` terminator:
```
num  = u16          ; block size*2 + encoded-flag
if num == 0: stop
xpos = s16 ; ypos = s16          ; position RELATIVE to the hot spot
if (hotx+xpos >= width) or (hoty+ypos >= height): stop
dst = raw + (hotx+xpos) + (hoty+ypos)*width
n   = num >> 1
if (num & 1) == 0:               ; RAW block
    copy n bytes from stream to dst
else:                            ; ENCODED block: n pixels via sub-runs
    j = 0
    while j < n:
        s  = u8 ; r = s & 1 ; m = s >> 1
        if r: fill m copies of (next u8) at dst+j      ; run
        else: copy m bytes from stream to dst+j        ; literal
        j += m
```
Pixel value 255 = transparent (left as the background fill).

## Use in the engine
- /tools (mkworld) decompresses U6MCGA.PTR, opens it as a 4-byte library, decodes
  item 0 (the arrow) to a flat width×height 8-bit bitmap + hot spot, and ships
  `[u8 w][u8 h][u8 hotx][u8 hoty][w*h pixels]` in the world pack. The runtime
  blits it as the mouse cursor (transparent index 255). render/gamedemo.

## Status: VERIFIED (arrow cursor; same frame format serves the .SHP images)

Shipped twice since 2026-10-28: as the core pack's U6W9 block and as U6FE.PAK section FES_CURSOR
(fepack.h), so the main menu can draw it before the core pack loads. The pointer's POSITION comes from
the engine's own IKBD mousevec hook (platinput.c; design/milestone.md "Mouse support"), not line-A.
