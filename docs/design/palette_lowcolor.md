# M7 — Low-colour palette enhancement (ST / STE 16-colour path)

> Provenance
> - Derived from : U6PAL + MAPTILES.VGA/TILEINDX.VGA (the user's own data) decoded host-side;
>                  the original ST port's palettes read from refdata/st/u6/U.PRG (see the
>                  st-palette-m7 memory) as the baseline to beat.
> - References   : tools/CLAUDE.md "Quantization strategy"; Atari ST/STE hardware palette
>                  (512 = 3 bits/ch, 4096 = 4 bits/ch). No reference SOURCE transcribed.
> - Tools        : tools/quant.h (shared median-cut core), tools/quantize.c (QA + PNG),
>                  tools/mkworld.c (ships the U6WW pack section), tools/png.c QA output.
> - Validated by : side-by-side QA PNG (build/quant_qa.png) + RMS error metric vs a naive
>                  EGA-16 baseline; the shipped U6WW section verified on-target (`make lowcol`
>                  → LOWCOL have=1 + the 16 palette regs match mkworld's host output byte-for-byte);
>                  the shifter backend rendered on an emulated plain ST (`make shot-st` →
>                  build/gamest_screen.png: throne room in the optimised 16 colours).
> - Confidence   : confirmed — quantiser core + shipped U6WW pack section + the rdr_st shifter
>                  backend all validated. Remaining work is footprint + per-scene palettes (below).
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

## Shipped pack section (U6WW) — the engine-facing artifact
`tools/mkworld.c` (`build_lowcolor`) ships a low-colour section the shifter backend consumes.
The core is shared with the QA tool via **`tools/quant.h`** (`q_build_hist` / `q_median_cut` /
`q_snap_pal` / `q_nearest`), so the pack palette and the QA PNG come from the SAME quantiser.

- **Weighting** = the tileset PIXEL histogram: decode all 2048 combined tiles, count each
  U6PAL index (transparent index 255 excluded). Colours covering more of the art get finer
  median-cut boxes. (First run: 245 used colours over 318 570 opaque tile pixels.)
- **Palette** = median-cut → 16, snapped to the ST 3-bit gamut (`q_snap_pal(...,3)`).
- **Section layout** (appended LAST in the pack, after the front-end art, self-guarded so older
  loaders that stop at EOF are unaffected):
  ```
  "U6WW"                     4-byte guard magic
  16 x { u8 r, u8 g, u8 b }  ST palette as raw 3-bit levels 0..7
  256 x u8                   lut[i] = the 16-colour index nearest to U6PAL colour i
  ```
  The palette is stored as raw 3-bit levels so `rdr_st` packs `r<<8 | g<<4 | b` straight into a
  shifter colour register (`$FF8240…`, `Setcolor`/`Setpalette`) with NO runtime maths. The ST
  does only: LUT lookup (256→16) per backbuffer pixel, then 4-plane c2p. TT/Falcon/vcard ignore
  the section entirely (they show all 256 VGA colours).
- **Quality** (build log): weighted RMS **25.19** vs the naive EGA-16's 33.5 — the tileset-usage
  weighting (+ excluding transparent 255) edges even the QA tool's unweighted 26.1.
- **First shipped 16** (rgb 3-bit): black · dark-red · dark-grey · tan · brown · mid-grey ·
  grass-green · muted-rose · deep-blue · water-blue · orange-red · gold · near-white · brown ·
  olive · purple — a genuine spread across the ST gamut, exactly what the fixed EGA-16 lacked.
- **Determinism**: byte-identical rebuild confirmed (`cmp` of two `mkworld` runs).
- **Scope note**: this is the single GLOBAL surface palette (the MVP the base shifter backend
  needs). The per-region / per-scene CLUTs (above) and band-palette rasters layer on top later;
  the section can grow more palettes without moving any existing offset.

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
- [x] Shared quantiser core `tools/quant.h`, used by BOTH the QA tool and mkworld.
- [x] Emit the low-colour pack section (**U6WW**: global ST-16 palette words + 256→16 LUT),
      usage-weighted by the tileset histogram; deterministic; verified on-target (`make lowcol`).
      TODO: the STE-16 variant + per-region CLUTs in the same section.
- [x] 4-plane shifter renderer backend `render/rdr_st.c` (256→16 LUT remap + chunky→4-plane c2p,
      Setscreen rez 0 = ST-low 320×200×16, Setpalette). 1:1 present (no TT doubling/letterbox).
      Pure-68000 `gamest.prg` (`make game-st`); validated on an emulated plain ST (`make shot-st`
      → the throne room renders in the optimised 16 — natural brick-red/stone-grey/parchment,
      not garish EGA). C c2p for now; a hand-asm span can replace it later (performance.md).
- [ ] **Footprint**: the current pack (~2.4 MB) + conv/portrait libs (~0.57 MB) + buffers overrun
      a real 4 MB ST's single ST-RAM pool (`plat_alloc_fast` falls back to chip there) → rdr_init
      fails "no renderer". Needs a slimmer ST pack (drop/shrink the front-end intro art, or a
      separate ST asset variant). `shot-st` uses 8 MB emulated to validate the render meanwhile.
- [ ] Per-scene low-colour palettes + colour-cycling on ST: `rdr_set_palette(256)` is a no-op on
      the shifter (16-colour hw can't take a 256 update), so cutscene palettes + CLUT cycling
      (water/fire) don't yet show. Reserve cycling slots (below) + ship per-scene 16s in U6WW.
- [ ] Reserve palette slots for colour-cycling ranges; flag over-budget scenes.
- [ ] Optional ordered dithering (per-asset, off for UI/tiles).
- [ ] Band-palette rasters (HBL/Timer-B) for >16 on-screen colours — after the base backend.

## Implemented / consumed by
- Host: `tools/quant.h` (core) + `tools/quantize.c` (`make quantize`, QA PNG) +
  `tools/mkworld.c` `build_lowcolor` (ships the U6WW section).
- Engine: `render/gamedemo.c` loads U6WW → `g_lc_pal[48]` / `g_lc_lut[256]` / `g_have_lowcolor`
  (`make lowcol` prints the load check), and hands it to the backend via `rdr_set_lowcolor`.
- Backend: `render/rdr_st.c` (16-colour shifter) consumes it — `make game-st` / `make shot-st`.
  `rdr_tt.c` / `rdr_falcon.c` implement `rdr_set_lowcolor` as a no-op (they show all 256).
