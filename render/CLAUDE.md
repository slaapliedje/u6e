# CLAUDE.md — Renderer Subsystem

## Scope
Platform-independent renderer interface + per-target backends. The engine
talks ONLY to the renderer interface; all hardware divergence lives here.

## Key Decision: VDI/XBIOS for setup & present, NOT for per-tile blits
- VDI raster ops (vro_cpyfm/vrt_cpyfm) have too much per-call overhead for a
  tile engine on 68000. Use them only for capability discovery + presentation
  on graphics cards.
- Per-frame rendering goes into our own off-screen backbuffer, then a single
  present step copies/converts to the visible screen.

## Canonical internal format
8-bit indexed (256-color), because the richest source art is 256-color VGA.
- Shifter (ST/STE): build-time remap to 16-color asset variant.
- TT / graphics card 8-bit: use 256 directly.
- Falcon 16-bit truecolor: expand palette -> 16-bit at present time.

## Renderer interface (small surface)

## Verified Atari video APIs (Atari Compendium / TOS)
Detect hardware via the `_VDO` cookie (jar) to know which path to take.

### Mode setting (per target)
- **ST/STE/Mega**: `Getrez()` to read; `Setscreen(log, phys, mode)` —
  XBIOS opcode 5 — to set. ST/Mega ST require physical screen on a 256-byte
  boundary; all later machines need only WORD alignment.
- **TT030**: `EgetShift()` / `EsetShift()` to read/set TT video mode;
  `Setscreen()` also works using TT resolution codes. (`EsetGray()`,
  `EsetSmear()` configure gray/smear modes if needed.)
- **Falcon030**: `VsetMode()` to read/set VIDEL mode; `VgetMonitor()` to
  detect attached monitor (limits available modes). `VsetScreen()` is an
  alternate binding of `Setscreen()` (same opcode 5) for Falcon modes —
  Falcon modes via Setscreen use special resolution code 3 plus the extra
  mode param. `VgetSize(mode)` returns bytes to allocate for a given mode.

### Caveats baked in by these calls
- `Setscreen`/`VsetScreen`/`EsetShift`/`VsetMode` reinitialize the VDI/VT52
  but NOT the AES. Do not call AES while in a changed mode; restore the
  original mode before using GEM again. (As of TOS 4.0, Setscreen
  reinitializes the VDI screen driver — you must still call `vq_extnd()` to
  update open workstations.)
- For true screen independence, query actual resolution via VDI
  `v_opnvwk()`, not `Getrez()`. Use `Getrez()+2` only to pick the VDI
  physical device id for font selection.

### Palette
- ST: `Setcolor()` / `Setpalette()` — 16 entries, 512 colors (RGB 0–7 each).
- STE: 4096-color palette (extended RGB).
- TT: 256 of 4096 (TT palette regs).
- Falcon: VIDEL palette (8-bit-per-component style) / direct truecolor.
- NOTE: VDI color indices are re-mapped vs. hardware registers; build a
  remap table if mixing VDI and direct register access (see vr_trnfm /
  v_get_pixel approach).

## Presentation strategy per backend
- **st**: backbuffer (16-color, build-time remapped) -> Blitter copy (if
  present) or CPU copy to physical screen; page-flip via Physbase/Logbase
  + Setscreen for tear-free updates.
- **ste**: as ST, richer palette; hardware h/v fine-scroll regs optional.
- **tt**: 8-bit backbuffer -> direct copy to TT screen mem.
- **falcon**: 8-bit -> 16-bit expand at present (truecolor), or native
  8-bit mode; allocate screen with VgetSize().
- **vcard**: discover modes via the card's VDI driver (v_opnvwk /
  vq_extnd); present via VDI raster copy to the card's screen OR direct
  framebuffer if the card exposes a linear aperture. Still composite into
  our own backbuffer first.

## Reference implementations to study (not copy)
- Patrice Mandin's Atari Doom port demonstrates a working dual path:
  `-video xbios` (direct Falcon/TT) vs `-video vdi` (portable, all Ataris
  incl. clones/cards), plus 15- vs 16-bit truecolor handling and ST-RAM vs
  Fast-RAM buffer placement. Good model for our backend split.

## Rules
- State the exact XBIOS/VDI call (name + opcode) used in any code.
- Never route per-tile blits through VDI.
- Allocate screen buffers in ST-RAM where the shifter/VIDEL requires it;
  keep working buffers in Fast-RAM (TT/Falcon) where beneficial.
- <<FILL IN: confirmed list of target graphics cards + whether they expose a
  linear framebuffer aperture or are VDI-driver-only>>

## Hardware Detection (runtime — NO card list)
Most ST-line graphics cards are framebuffer cards that ship a VDI driver
(typically NVDI-based or fVDI). We do NOT enumerate cards or bang their
registers. We detect capabilities at runtime and adapt to the CURRENT mode.

### Detection sequence
1. Read `_VDO` cookie -> built-in video class (ST shifter / STE / TT / Falcon).
2. Check `EdDI` cookie -> a third-party / extended VDI driver is present.
   - The LONGword after the 'EdDI' ID is a dispatcher address; call it with
     function number 0 (Pure-C register conventions: params in d0-d2/a0-a1)
     to get the EdDI version in BCD (e.g. 0x0110 = v1.10).
   - EdDI >= 1.00 enables off-screen bitmaps (v_opnbm); >= 1.10 allows
     requesting a SPECIFIC pixel format/plane count for the bitmap.
3. graf_handle() (AES) -> physical workstation handle.
4. v_opnvwk() -> open our virtual workstation.
5. vq_extnd(handle, 1, work_out) -> EXTENDED capability inquiry (owflag=1).
   Key returned fields we use:
     - No. of planes              -> color depth (4=16c, 8=256c, 15/16=truecolor)
     - "1 if palette based, 0 if true colour" -> indexed vs truecolor path
     - controller/screen-buffer type
     - Size of off-screen backing buffer (LONG): if nonzero, the card has a
       buffer outside conventional memory usable by vro_cpyfm() -> prefer it.
   (vq_extnd with owflag=0 returns the same data as v_opnvwk open info.)
6. Branch on planes:
     - 4  planes -> shifter/16-color path (build-time remapped assets)
     - 8  planes -> 256-color: use our 8-bit indexed backbuffer directly
     - 15/16     -> truecolor: palette-expand at present
7. For raw screen base + direct framebuffer compositing on built-in video,
   use Logbase()/Physbase(); on cards, prefer the off-screen-bitmap +
   vro_cpyfm present path (see below) since a linear aperture isn't
   guaranteed/portable.

### vq_scrninfo (pixel format details)
When EdDI/extended VDI present, vq_scrninfo gives exact pixel format and bit
order (needed to interpret 8-bit chunky vs interleaved planes vs truecolor
layout). Use it before choosing the present conversion routine.

### Off-screen bitmap present path (cards & flicker-free general path)
- v_opnbm() (opcode 100, sub-op 1) creates an off-screen bitmap in the
  driver's DEVICE-SPECIFIC format -> fast copy to screen.
  - EdDI >= 1.10: request our format via work_in[15..19]:
    work_in[15..16]=colors, [17]=planes, [18]=pixel format, [19]=bit order.
    (e.g. 256 / 8 / 0=interleaved / 1=normal bit order)
  - NOTE: (work_in[11]+1) (width) should be divisible by 16 or the driver
    rounds up.
  - If MFDB fd_addr==0 the VDI allocates+clears the buffer.
- Present with vro_cpyfm (opaque) / vrt_cpyfm (mono->color expand).
  Use the OFF-SCREEN bitmap's handle when screen is destination so clipping
  is right; use SCREEN handle when bitmap is the source.
- v_clsbm() to release.

### Critical caveat (already noted globally)
- vq_extnd/v_opnbm describe the CURRENT mode. We do NOT switch a card's mode
  ourselves (user sets card resolution via its own setup/ACC). On built-in
  video we may switch via Setscreen/VsetMode/EsetShift, but those reinit the
  VDI (call vq_extnd again after) and do NOT reinit the AES.

## Reference (verified)
- vq_extnd extended-inquiry field layout (planes, palette-vs-truecolor,
  off-screen buffer size) per the VDI function-102 spec.
- EdDI/NVDI cookie + off-screen bitmap mechanism (v_opnbm/v_clsbm), incl.
  EdDI 1.1 format-request parameters.
- fVDI: open-source VDI replacement with chunky 8-bit and truecolor driver
  examples (ARAnyM/RageII drivers) — good clean-room study material for the
  present/conversion routines.

