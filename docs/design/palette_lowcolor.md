# M7 — Low-colour palette enhancement (ST / STE 16-colour path)

> Provenance
> - Derived from : U6PAL + MAPTILES.VGA/TILEINDX.VGA (the user's own data) decoded host-side;
>                  the original ST port's palettes read from refdata/st/u6/U.PRG (see the
>                  st-palette-m7 memory) as the baseline to beat.
> - References   : tools/CLAUDE.md "Quantization strategy"; Atari ST/STE hardware palette
>                  (512 = 3 bits/ch, 4096 = 4 bits/ch). No reference SOURCE transcribed.
> - Tools        : tools/quantize.c (host), tools/png.c QA output.
> - Validated by : side-by-side QA PNG (build/quant_qa.png) + RMS error metric vs a naive
>                  EGA-16 baseline — our optimised palette measurably + visibly better.
> - Confidence   : confirmed (quantiser core); the 4-plane shifter backend is still TODO.
> - Last updated : 2026-07-12

## Goal
Base ST/STE looks BETTER than the original "lazy" ST port, which reused a fixed EGA-ish
16-colour palette and ignored the ST's wider gamut. We start from the rich 256-colour VGA art
and reduce it to an OPTIMISED 16-colour palette chosen from the REAL hardware gamut.

## Hardware gamut
- **ST**  : 512 colours = **3 bits per channel** (0..7), value word `0x0RGB`.
- **STE** : 4096 colours = **4 bits per channel** (0..15). Straight 4-bit nibbles here (the
  STE register's rotated LSB form is applied at the present/`set_palette` layer, not in the
  asset). The original ST port already carried STE 4-bit palettes (U.PRG @0x17900) — so 4096
  is confirmed available; see the `st-palette-m7` memory.
- Snap 8-bit VGA channel `v` to a `b`-bit hardware value: `level = round(v/255 * (2^b-1))`,
  displayed back as `level * 255/(2^b-1)`.

## Quantiser (tools/quantize.c) — the method
1. **Source** = the 256-colour art rendered to RGB8 (currently the 512 map tiles @16×16 =
   512×256; the plan is per-scene/per-region, below). Build the unique-colour histogram
   weighted by pixel count.
2. **Median-cut** the used colours into 16 boxes: repeatedly split the box with the largest
   `weighted-range × total-weight` along its longest RGB axis, at the weighted median. Each
   box's centroid = its weighted average colour.
3. **Snap the 16 centroids to the hardware gamut BEFORE measuring error** (3-bit ST / 4-bit
   STE), so the optimiser accounts for hardware rounding — not an afterthought.
4. **Remap** each source pixel to the nearest of the 16 snapped colours by a luma-weighted
   squared distance (~3:6:1 R:G:B). Dithering is OFF for tiles/UI (muddies readability); it is
   a per-asset option for gradients (TODO).
5. **QA**: emit `build/quant_qa.png` — panels `VGA-256 | our ST-16 | our STE-16 | naive
   EGA-16`, 2×-upscaled — and print the per-target **RMS per-channel error**.

## Result (first run, 512 map tiles, 211 distinct source colours)
| palette            | gamut       | RMS error |
|--------------------|-------------|-----------|
| our ST-16          | 512 (3-bit) | **26.1**  |
| our STE-16         | 4096 (4-bit)| **25.4**  |
| naive EGA-16 (fixed)| —          | 33.5  (the "lazy port" baseline) |

Our optimised palette is ~22 % lower error than the naive EGA-16, and visibly faithful to the
VGA original (natural terrain greens/blues/browns) where EGA-16 is garish (oversaturated
primaries, browns→orange). STE's extra bit edges out ST as expected.

## Per-scene / per-region palettes
U6 swaps its palette by context, so a palette fit to each REGION beats one global 16 applied
everywhere. quantize.c picks the 4 most tile-varied, spread-out surface regions (32×32 tiles),
gives each its own STE-16, and compares against one global STE-16 built from all four
(build/quant_regions.png — rows = regions, cols = VGA-256 | per-region-16 | global-16):

| region @ tile   | per-region RMS | global RMS | win |
|-----------------|----------------|------------|-----|
| (480,352)       | 15.2           | 18.1       | 16% |
| (672, 96)       | 22.9           | 23.6       |  3% |
| ( 96,512)       | 16.7           | 20.4       | 18% |
| (128,864)       | 15.3           | 16.6       |  8% |
| **total**       | **70.2**       | **78.6**   | **11%** |

Per-region is ~11 % lower error, most visible in region-specific highlights — e.g. white
snow/mountain that the global palette can't afford and shifts to a muddy lavender. NOTE the
U6 SURFACE is fairly uniform (grassland + water + roads + buildings), so the surface-to-surface
win is modest; the LARGER per-scene wins are between radically different palettes — dungeons
(dark), town interiors, and the vivid PALETTES.INT cutscenes — which quantise to their own
palettes for free by the same method (extending the tool to those scenes is TODO).

## Colour cycling (must survive the reduction)
U6 animates fire/water/etc. by rotating small U6PAL ranges (palette_cycling.md — fire 224–231
confirmed). For the 16-colour path we must RESERVE slots for any cycling range that maps into
the scene, locking those entries so the optimiser fills only the rest, and flag scenes needing
more cycling slots than fit. **TODO** — not yet in quantize.c.

## Status / remaining
- [x] Quantiser core: 256→16, ST/STE gamut snap (snap-before-error), remap, RMS metric, QA PNG,
      EGA-16 baseline. `make quantize`.
- [x] Per-REGION palettes vs global — demonstrated on 4 surface regions (per-region 11 % lower
      error; build/quant_regions.png). TODO: extend to non-surface SCENES (dungeon / town /
      PALETTES.INT cutscenes), where the per-scene win is much larger, + decide the emit
      granularity (per-superchunk region CLUT swap vs per-scene).
- [ ] Reserve palette slots for colour-cycling ranges; flag over-budget scenes.
- [ ] Optional ordered dithering (per-asset, off for UI/tiles).
- [ ] Emit the 16-colour packs (ST/STE 4-plane interleaved tiles + the palette words) — feeds
      the shifter renderer backend.
- [ ] 4-plane shifter renderer backend (Blitter present) — the on-target consumer.

## Implemented / consumed by
- Host: `tools/quantize.c` (`make quantize`).
- Engine (TODO): a `render/rdr_st.c` shifter backend; mkworld emits the lowcolor pack variant.
