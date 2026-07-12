# Renderer — M3 first backend (TT-low 256-color)

Date: 2026-06-23  ·  Milestone: M3  ·  Code: render/render.h, render/rdr_tt.c,
render/rdemo.c, tools/mkpack.c

Resolved/empirical notes for the first on-target renderer. The canonical
internal surface is a **chunky 8-bit backbuffer**; each backend converts it to
the screen's native layout at present time. The engine only ever touches the
backbuffer (per-tile blits there), never the screen directly.

## Mode + palette (TT-low, 320×480, 256 colors)
TOS calls (verified against headers + Hatari TT/EmuTOS/VGA):
- `EsetShift(TT_LOW)` (XBIOS 0x50, mode 0x0700) — set the 256-colour TT mode.
- `Setscreen(buf, buf, -1)` (XBIOS 5) — point log+phys video base at our buffer
  (rez = -1 keeps the mode set above). Base must be in **ST-RAM** (shifter
  requirement) — allocated via `plat_alloc_chip` (256-byte aligned).
- `EsetPalette(0, 256, ptr)` (XBIOS 0x52) — load the 256-entry CLUT.
- Restore on exit: `Setscreen(old_log, old_phys, -1)` then `EsetShift(old)`.

## Screen memory layout — 8 interleaved bitplanes
TT-low is **8 interleaved bitplanes** (like ST modes, but 8 planes). A row of
320 pixels is 320 bytes = 20 groups of 16 pixels; each group is 8 consecutive
16-bit words (plane 0..7); pixel p's colour bit b is bit (15-p) of word b.
Present = chunky → this (c2p): for each 16-pixel group, build 8 plane-words.
(Current c2p is straightforward C — correct first, optimised later; this is a
hot path and an obvious asm/blitter target.)

## Screen base MUST be 256-byte aligned (bug found on real TT)
The TT video base register is 256-byte-granular: if the physical screen base
isn't on a 256-byte boundary, the shifter mis-fetches and the image shears with
a garbage strip down the left edge. Our bump allocator originally aligned the
pool OFFSET but not the absolute address (the Mxalloc pool base is only
word/paragraph aligned), so the screen landed at e.g. 0x1e90e → strip. Fixed in
platmem.c bump_alloc to align the ABSOLUTE address; `plat_alloc_chip(...,256)`
now returns a 256-aligned screen (verified: Physbase 0x1e90e → 0x1ea00). The
standalone test programs didn't hit this because they hand-aligned their buffer.

## TOS version matters — test on real per-machine TOS
The ancient EmuTOS we first used mangled the TT 256-colour CLUT (the palette we
set via EsetPalette wasn't what reached the screen). Real **TOS 3.06** (the
TT's own ROM) renders the palette correctly. Lesson: test each machine on its
real ROM (ST 1.04 / STE 2.06 / TT 3.06 / Falcon 4.04); use a CURRENT EmuTOS
(1.4) for the portable dev path, not whatever the distro shipped. The Makefile
per-machine targets now default to the real ROMs.

## CLUT format — TT 256-colour uses STRAIGHT 4-bit nibbles
The TT 256-colour palette (EsetPalette) takes 0x0RGB words with **straight
linear 4-bit** channels. We verified this on-screen with a gray ramp: straight
nibbles → smooth ramp; the STE *bit-rotated* form (`((v&1)<<3)|(v>>1)`) → an
oscillating scramble. So `hw_nibble()` is just `v6 >> 2` (6-bit U6 → 4-bit).
(The 16-colour STE shifter DOES use the rotated form — that's the future `ste`
backend's concern, not the TT's.) NOTE: the colour scramble we initially chased
turned out to be mostly the unaligned-screen-base bug above plus the ancient
EmuTOS; the CLUT format itself is straight.

## Verification method
Two independent checks (host-side, no hand-waving):
1. **Composite correctness**: rdemo dumps the chunky backbuffer to `OUT.RAW`;
   host `raw2png` colours it with U6PAL and it is **byte-identical** to the
   golden region image from the same decoders (tools/mkpack). This proves the
   on-target load + tile composite exactly matches the host decode.
2. **On-screen correctness**: Hatari records an AVI of the emulated TT screen;
   a late (held) frame is extracted to confirm the real shifter output (mode +
   c2p + CLUT). rdemo holds the frame with a Vsync loop so the capture is
   stable. NOTE: under `--run-vbls` Hatari force-quits and the AVI has no index,
   so the frame is extracted by sequential decode (keep last), not `-sseof`.

## Asset pack (indexed256) — tools/mkpack
Big-endian (target byte order): magic, region WxH in tiles, tile count, then
palette (256×RGB6), tileset (256 × 16×16×8bpp), region tile-index grid. The ST
loads it and composites tiles into the backbuffer. All decode/quantisation is
host-side; the ST does only load + blit + c2p.

## Windowed present (perf)
`rdr_present_rect(x,y,w,h)` c2p's only a sub-rectangle (x,w must be ×16). The
game draws a small 11×11 map window + static panels and per move recomposes +
present_rect's ONLY the window (+ status strip) — present dropped 583→117 ms
(~5×). See /docs/design/performance.md.

## Open / next
- [ ] Further c2p wins: incremental window scroll (c2p only the new edge
      row/col on a 1-tile move) and/or asm c2p. `make bench` tracks it.
- [ ] Page-flip (two ST-RAM screen buffers) for tear-free animation (M5).
- [ ] Other backends: STE 16-colour (M7), Falcon truecolor + vcard (M8).
- [ ] A real input loop + clean mode restore on exit (M5) — rdemo currently
      holds the frame in a Vsync loop.
