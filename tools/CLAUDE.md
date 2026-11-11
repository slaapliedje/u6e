# CLAUDE.md — Host-Side Asset Pipeline (Tools)

## Scope & golden rule
These tools run on the MODERN DEV MACHINE (host PC), NOT the Atari ST.
They convert user-provided original game data into target-specific asset
packs the engine loads at runtime. ALL expensive work (decompression, color
quantization, palette remapping, format conversion) happens here at build
time. The ST does NONE of it at runtime.

- Language for tools: host-native (C, or a scripting language is fine here
  since these don't run on target). Resolved: C, so the tools reuse the
  /dataformats parsers; Python only for the bot and video harnesses.
- Clean-room still applies to FORMAT KNOWLEDGE: tools may read original files
  the user owns, but ship NO copyrighted assets in the repo or output repo.
- Endianness: original files are little-endian; engine target is big-endian
  68000. Tools must emit asset packs in the target's expected byte order so
  the runtime loader does minimal/no swapping.

## Inputs
- User-provided original data for each game (U6 / Savage / Martian), located
  via config (never bundled).
- Shared building blocks from /dataformats specs: LZW decompressor, library
  reader, MAP/CHUNKS, tile files, palette files.

## Outputs
- Per-target asset packs:
  - lowcolor (16-color): for ST shifter + STE
  - indexed256: for TT / Falcon 8-bit / 256-color video cards
  - truecolor: for Falcon 16-bit (+ truecolor video cards)
- A manifest per game (consumed by /engine GameDef): lists assets, palettes,
  fonts, and which pack variant they belong to.

## Palette handling (verified context + the headline feature)

### Background (verified)
- U6-family art is 256-color VGA, using in-game palette(s): u6pal for tiles,
  palettes.int for cutscenes, convert.pal for conversion.
- The engine animates water / lava / force-fields / etc. via COLOR CYCLING:
  it rotates a small RANGE of palette entries each tick rather than swapping
  tile bitmaps. This is the classic palette-animation technique. The cycling
  RANGES must be preserved as metadata, NOT baked into static frames.
- ST shifter palette = 512 colors (3 bits/channel RGB). STE = 4096 (4 bits/
  channel). Both are a WIDER gamut than fixed EGA.

### KNOWN ISSUE we deliberately fix
The original ST port was criticized as lazy partly for apparently reusing the
fixed EGA 16-color palette, ignoring the ST's larger 512-color (STE: 4096)
gamut. Our pipeline produces a BETTER 16-color result.

### Quantization strategy (per target)
- indexed256 / truecolor targets: NO quantization needed (TT/Falcon/card can
  show all 256 VGA colors, or expand to truecolor). Just convert layout +
  carry the palette + cycling ranges.
- lowcolor (ST/STE) target: quantize 256 -> 16 using an OPTIMIZED palette
  chosen from the ST's REAL gamut (512 for ST, 4096 for STE), NOT EGA.
  - Quantize PER PALETTE-REGION / PER SCENE, not one global 16-color palette.
    U6 swaps/adjusts palettes by context; a single global 16 would look bad.
  - Snap chosen colors to the nearest representable ST/STE hardware value
    (round to 3-bit, or 4-bit STE, per channel) BEFORE evaluating error, so
    the optimizer accounts for hardware rounding.
  - Optional ordered dithering for gradients; keep it OFF for UI/text/tiles
    where it muddies readability — make it per-asset configurable.

### Color cycling preservation (all targets)
- Detect/record the cycling ranges from the source palette data and emit them
  as metadata in the asset pack. The runtime /render set_palette path rotates
  those ranges on the appropriate target hardware palette.
- For lowcolor: if a cycling range maps into the reduced 16-color palette,
  RESERVE those slots during quantization (use the "fixed/reserved palette
  entry" approach: some slots are locked for cycling, the optimizer fills the
  rest) so animation still works after reduction. If a game scene needs more
  cycling slots than fit in 16 colors, flag it for manual tuning rather than
  silently dropping animation.

## Tile / sprite conversion
- Decode tiles via /dataformats (tileindx.vga = STORED not compressed;
  maptiles.vga stored; masktype/objtiles LZW). Convert to the target pixel
  layout:
  - lowcolor: 4-plane interleaved (ST/STE Blitter-friendly layout).
  - indexed256: 8-bit chunky OR the format requested via EdDI/v_opnbm
    (match the screen's device format to make present a straight copy).
  - truecolor: 16-bit; expand at conversion or expand-at-present (decide;
    expand-at-present keeps packs smaller). Resolved: expand at present
    (rdr_falcon's RGB565 LUT, rdr_vcard's per-layout expand); packs stay 8-bit.
- Respect tile masks/transparency; emit mask data alongside where needed.

## Map / world conversion
- MAP/CHUNKS, OBJBLK, savegame-object data -> engine's in-memory world
  format, emitted big-endian. Preserve toroidal-wrap assumption.
- Per-game level semantics handled by emitting the level-mapping policy data
  the engine's GameDef expects (U6 nested 3D dungeons; Savage 4-of-6 +
  gargoyle-world level 6; Martian toroidal world).

## Text / fonts / conversation
- Extract string tables, books/signs, item/ground descriptions -> engine
  string format (target encoding/endianness).
- Fonts (u6.set: normal/runic/gargish) -> engine font format.
- Conversation/usecode: tools extract & repackage the dialog bytecode +
  string pool; SEMANTICS live in the /engine usecode VM. Tools must NOT
  reinterpret control bytes, only repackage faithfully (+ optional validation
  pass that the VM can load every script).

## Validation / testing
- Round-trip + golden output: dump decoded tiles->PNG, maps->image,
  palettes->swatch, and diff against known-good references.
- Quantization QA: emit side-by-side PNG (original VGA vs our 16-color ST
  result) per scene so the palette improvement is reviewable. Track a simple
  perceptual-error metric per scene to catch regressions.
- Determinism: same inputs -> byte-identical asset packs (so builds are
  reproducible and diffable).

## Decisions log (tools)
- Tool language: C (reuses /dataformats); Python for tools/bot and tools/video.
- Truecolor: expand at present; one 8-bit pack serves every 256-colour target.
- Dithering defaults per asset class.
- Where reference/known-good assets live for golden tests.

