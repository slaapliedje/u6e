## Golden Rules for Claude (revised)
- Target CPU is Motorola 68000 (big-endian). Never assume little-endian.
- CLEAN-ROOM DISCIPLINE: Nuvie/ScummVM and community tools may be read to
  *understand* a data format and to *write a spec* in /docs/formats/. The
  engine implementation MUST be written from our own spec, not transcribed
  from their source. Do not paste their code. When a format is understood,
  produce the markdown spec first; implement from the spec.
- Original game data files (from the user's own DOS or ST copy) are required
  at runtime, like other open-source engine remakes. We ship NO copyrighted
  assets. Asset paths are user-configured.
- For graphics: use VDI/XBIOS for capability discovery, mode/resolution
  setting, and palette setup ONLY. Do per-tile rendering into our own
  off-screen backbuffer; never route per-tile blits through VDI raster ops.
- State which TOS/XBIOS/VDI call (name + number) you use, for verification.

## Toolchain & Build (resolved)
- Cross-compiler: `m68k-atari-mint-gcc` (GCC 15.2.0), triple `m68k-atari-mint`,
  big-endian, 32-bit int/pointer, default arch 68000. Full details (include
  paths, multilib, CFLAGS, clangd setup): **docs/design/toolchain.md**.
- Target builds: a 68000 binary (whole line) + a `-m68020-60` binary
  (TT/Falcon); both from the same source + same runtime detection.
- Language: C11 (GNU dialect), with hand-written asm only for verified hot
  paths. Engine is C, NOT C++ (see engine/CLAUDE.md "RESOLVED DECISIONS").
- IDE: clangd (config in `.clangd` + `.vscode/`); see docs/design/toolchain.md.
- TOS: EmuTOS for dev; spot-check on real TOS versions for compatibility.

## Renderer Architecture (resolved approach)
Renderer interface (platform-agnostic), small surface:
  - rdr_query_capabilities()   -> available modes/depths (via VDI mode enum)
  - rdr_set_mode(mode)         -> via XBIOS/VDI (Setscreen/VsetMode/etc.)
  - rdr_set_palette(pal, n)    -> per-target palette set
  - rdr_get_backbuffer()       -> pointer to our off-screen surface
  - rdr_present()              -> backbuffer -> visible screen
Backends:
  - st       : 16-color shifter, CPU/Blitter present
  - ste      : the st backend; on STE hardware it loads 4-bit (4096-colour) palettes
  - tt       : 256/4096 TT shifter
  - falcon   : VIDEL, 256 or 16-bit truecolor present path
  - vcard    : present via the card's VDI driver; mode chosen from VDI enum
All backends composite into a backbuffer in a canonical internal pixel
format; per-target present step handles layout/palette conversion.
Canonical internal format (resolved): 8-bit indexed chunky backbuffer
(render/render.h); the source art is 256-colour VGA. The ST backend LUT-maps it to
16 colours and converts to planar at present; the others present the 256 colours directly.

## Asset Pipeline (host-side, runs on dev PC)
- Inputs: user-provided original data files (U6 / Savage / Martian).
- VGA art (256-color) -> per-target palette-remapped assets at build/convert
  time. Quantization for the 16-color shifter target happens here, NOT on ST.
- Output: target-specific asset packs the engine loads at runtime.

