# Native-planar ST renderer (eliminate per-frame c2p)

> Provenance
> - Derived from : our own perf profiling (docs/design/performance.md, `make bench-st`),
>                  Atari ST hardware refs (shifter planar layout, Blitter), our existing
>                  chunky renderer (render/rdr_st.c) + PREDIM lighting (render/gamedemo.c).
> - References   : Atari Compendium (Blitter register set, movep planar layout); Patrice
>                  Mandin's Atari Doom (dual chunky/planar model) — READ for understanding,
>                  not copied.
> - Tools        : make bench-st / bench / bench-falcon (our harness).
> - Validated by  : NOT YET — thesis to be confirmed by the Phase-0 microbench below.
> - Confidence   : design = probable; perf win = unproven until Phase 0 measures it.
> - Last updated : 2026-07-19

## 1. Why

The ST present is a chunky→planar conversion (c2p). Measured, PREDIM night throne room:

| machine | frame | fps | present |
|---------|------:|----:|--------:|
| plain ST 68000 @ 8 MHz | 934 ms | 1.07 | **444 ms c2p** |
| Falcon 68030 @ 16 MHz  | 198 ms | 5.06 | 91 ms (truecolor, no c2p) |
| TT030 68030 @ 32 MHz   | 88 ms  | 11.4 | 49 ms |

c2p is ~86 cyc/pixel on the 68000 (a `lsr.b`/`roxl.w` bit-serial chain, near-optimal for a LUT'd
4-plane convert) and is HALF the plain-ST frame. The Falcon shows the escape: presenting a native
pixel format with **no c2p** is the win. For the shifter (ST/STE) the native format is 4 interleaved
bitplanes — so the plain-ST equivalent of "no c2p" is to **composite directly in planar** and never
convert. This is also where the **Blitter** finally earns its keep: masked planar blits are exactly
what it does (unlike c2p, which it cannot accelerate).

Goal: take the plain ST from ~1 fps toward ~2 fps by deleting the 444 ms per-frame c2p. Falcon/TT
already exceed 2 fps and are unaffected (they keep the shared chunky path).

## 2. Thesis (Phase 0 must confirm before we build the rest)

A **masked planar tile blit copies pre-converted planar data**; it does NOT bit-scatter per pixel.
So per 16-px row it is ~4 plane word-moves + a mask combine, vs c2p's 16×(lsr+roxl). The per-frame
c2p is replaced by a **one-time** convert at cache-build (dim the chunky tile, c2p it to planar once,
cache it), amortised across every later frame that reuses that (tile, light-level) pair — exactly
like today's `dim_tile` cache, but the cached variant is planar.

If the microbench shows a masked planar blit of the visible view is materially cheaper than the c2p
of the same area **on the plain ST (no Blitter)**, native planar is validated for the whole line.
If it only wins with the Blitter, we restrict it to STE/Mega/TT and the plain ST keeps chunky+c2p.

### Phase-0 RESULT (2026-07-19, `make bench-st`, plain ST 8 MHz, no Blitter) — **GO**
| operation | ms (N=20) | vs c2p |
|-----------|----------:|-------:|
| c2p 176² (today's present) | 439 | 1.0× baseline |
| masked planar blit, group-ALIGNED (11×11 grid) | **57** | **7.7× cheaper** |
| masked planar blit at the 8-px inset (spans 2 groups/plane) | 158 | 2.8× cheaper |

Thesis confirmed decisively — a masked planar blit is **2.8–7.7× cheaper than c2p even with no
Blitter**. Two consequences for the design: (1) native planar is a GO for the whole ST line, not just
Blitter machines; (2) the 8-px inset roughly triples the per-tile blit (each plane spans two groups
when shifted 8 px), so **compose GROUP-ALIGNED into a 176-aligned planar backbuffer and apply the
inset as ONE shifted planar block-copy at present** — keeping per-tile blits at the fast 57 ms path
rather than 158 ms. (Microbench: `rdr_planar_microbench` in render/rdr_st.c, BENCH.)

## 3. Architecture

### 3.1 Planar tile + mask
Each tile becomes ST-low planar: 16 px/row = 4 plane words = 8 bytes/row × 16 = **128 bytes/tile**
(half the 256-byte chunky tile) + a **16-word mask** (1 bit/pixel, 1 = opaque) = 32 bytes. Built at
load time by c2p-ing the existing chunky `g_tileset` (the asset pipeline stays chunky-first; the ST
converts on load, so no new build-time asset variant and no shipped planar assets). Transparent
pixels (index 255) set mask=0.

### 3.2 Masked planar blit (the one hot primitive)
`blit_planar_masked(dst, dstride, src128, mask32, x, y)` writes a 16×16 planar tile at pixel (x,y):
per row, for each of 4 planes `d = (d & ~m) | (s & m)`. Two paths, runtime-selected:
- **CPU** (plain ST): word ops, ~17 ops/row. The Phase-0 primitive.
- **Blitter** (STE/Mega/TT): HOP=halftone/AND, LOP, endmask + skew for the x-offset; the mask drives
  the Blitter's smudge/endmask. One Blitter op per plane per tile. State the register set used.

x need not be a group boundary — the 8-px map inset (§5) means blits land at x=8, 24, … (mid-group).
The CPU path shifts src/mask by (x&15); the Blitter uses its skew field. This is the fiddly part.

### 3.3 PREDIM in planar (the lighting fit)
PREDIM already selects a pre-dimmed tile per cell. In planar, the `dim_tile` cache stores **planar
dimmed variants**: on miss, dim the chunky tile via the darklut (as now) THEN c2p to planar + build
its mask, cache the 128+32 bytes. Hit = ready-to-blit planar. So the per-frame c2p vanishes; the
convert happens once per (tile, level) and is reused. DITHER/SMOOTH (per-pixel apply) do NOT fit
native planar (they need chunky) — **native planar is PREDIM-only**, which is already the ST target
(docs/rules/lighting.md Stage F).

**Alignment (from Phase 0):** compose the map into a **176-aligned planar backbuffer** (tiles at
x=cell·16, the fast 57 ms path), and apply the 8-px `MAP_INSET` as ONE shifted planar block-copy when
copying the backbuffer to the screen — NOT per tile (which is 2.8× slower).

### 3.4 Compose divergence — localized to the per-cell blit
The compose LOGIC (tile/anim selection, `compute_light`/`compute_visibility`, the incremental scroll
+ dirty tracking, object/actor placement, multi-tile expansion) stays SHARED with TT/Falcon — it is
complex and must not fork. Only the per-cell OUTPUT diverges: `draw_cell`/`draw_multi` dispatch to a
**chunky** masked blit (TT/Falcon → chunky backbuffer, then their present) OR a **planar** masked
blit (ST → planar backbuffer, no present-time c2p). The incremental dimmed cache (`g_dimmed_map`)
becomes planar on the ST; its shift is a planar block-move; dirty cells re-blit planar variants. The
"present" for the ST collapses to a planar block-copy of the map region to the screen (or compose
straight to the screen) — no c2p.

### 3.5 No c2p present
`rdr_present_rect_doubled` on the ST becomes a planar block-copy (or a no-op if compose wrote the
screen directly). The screen is 4-plane; the map region is already planar. The border/papyrus ring
and the panel stay as they are (drawn once, planar).

## 4. Geometry (unchanged, but now in planar)
`MAPX=0, MAPY=0, MAPSZ=176, MAP_INSET=8, VT=11`. The 8-px inset offsets tiles from 16-px group
boundaries — the CPU blit handles it by shifting src/mask by `x&15` (here always 8), the Blitter by
skew. The visible map is the inner 10×10 cells; actors are planar sprites blitted at the same offset.

## 5. Blitter path + detection
Detect the Blitter via the `_SND`/cookie or the Blitter-config byte (state the exact probe). If
present, route `blit_planar_masked` through it; else CPU. The plain ST (no Blitter) validates the CPU
path; STE/Mega/TT get the Blitter speedup for free on top.

## 6. Phased plan
- **Phase 0 (NOW): microbench.** Convert one representative tile to planar+mask; masked-planar-blit
  the ~100-cell view N times (CPU); compare to the current c2p of the same area. Decide go/no-go for
  the plain ST from the real number. No integration yet.
- **Phase 1:** `blit_planar_masked` (CPU) + the planar tile/mask cache + planar `dim_tile`. Unit
  self-test: blit == c2p(chunky) for the same tile (byte-identical planar output).
- **Phase 2:** divert the ST `draw_cell`/`draw_multi` to planar; a planar `g_dimmed_map`; drop the
  ST c2p present. Byte-verify the planar screen vs the current chunky+c2p screen (in-memory diff, the
  same trick as the c2p selftest). Measure the frame.
- **Phase 3:** Blitter path for `blit_planar_masked` (STE/Mega/TT), runtime-detected.
- **Phase 4:** actors as planar sprites; roofs; fields; the full lit scene.

## 7. Expected perf + risks
Expected (plain ST, if the thesis holds): per-frame c2p (444 ms) → a warm-cache planar block-move +
dirty-cell planar blits (~tens of ms). Frame could approach compose-bound (~490 ms → less, since the
dimmed→tmp copy + ph3 also fold into the planar path). Plausibly ~2–3 fps. **Unproven until Phase 0.**

Risks: (a) CPU planar masked blit at an 8-px offset may be costlier than hoped (the shift/mask per
plane) — Phase 0 measures it. (b) The compose divergence must stay localized to the blit, or it
forks the hard-won shared logic. (c) Cache footprint: planar variants are 160 B each (128+32) vs the
chunky 256 — smaller, fine. (d) Band-raster (M7) per-row palette split interacts with planar (the
planar tile bits are palette-index bits, so band palettes still work — verify).

## 8. Status
**Phase 0 DONE — thesis VALIDATED (GO).** Planar blit 2.8–7.7× cheaper than c2p on the plain ST.
Next: Phase 1 (`blit_planar_masked` CPU + planar tile/mask + planar `dim_tile` cache, byte-verified).
No engine render path changed yet — only the BENCH microbench (`rdr_planar_microbench`) exists.
