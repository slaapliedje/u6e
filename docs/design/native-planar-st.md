# Native-planar ST renderer (eliminate per-frame c2p)

> Provenance
> - Derived from : our own perf profiling (docs/design/performance.md, `make bench-st`),
>                  Atari ST hardware refs (shifter planar layout, Blitter), our existing
>                  chunky renderer (render/rdr_st.c) + PREDIM lighting (render/gamedemo.c).
> - References   : Atari Compendium (Blitter register set, movep planar layout); Patrice
>                  Mandin's Atari Doom (dual chunky/planar model) — READ for understanding,
>                  not copied.
> - Tools        : make bench-st / bench / bench-falcon (our harness).
> - Validated by  : Phase-0 microbench (thesis, GO) + Phase-1 self-test (`rdr_planar_selftest`) +
>                  Phase-2 INV2 (`rdr_pverify`: planar composite == c2p(chunky), `diff=0` over a walk)
>                  and the measured frame (933→619 ms) — all via `make bench-st`.
> - Confidence   : design = confirmed; perf win = MEASURED (present 5.5× / frame 1.51× on plain ST);
>                  primitives + integrated compose = VERIFIED byte-identical to c2p.
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
- **Phase 1 (DONE):** `blit_planar_masked` (CPU) + the planar tile/mask cache + planar `dim_tile`. Unit
  self-test: blit == c2p(chunky) for the same tile (byte-identical planar output). → `diff=0`, see §8.
- **Phase 2 (DONE):** divert the ST `draw_cell`/`draw_multi` to planar; a planar static layer
  (`g_pdimmed`); drop the ST c2p present. Byte-verify the planar composite vs c2p(chunky) (INV2,
  in-memory). → `diff=0` over 41 frames; frame 933→619 ms (1.51×), present 444→81 ms. See §8.
- **Phase 2c (DONE):** wire it into the LIVE ST game (`present_map`), band-raster-safe map LUT,
  palette-change invalidation. Pixel A/B on a real ST: planar vs chunky identical bar the debug glyph.
  Live clean-walk 739 ms / 1.35 fps (present 5.5× cheaper). See §8.
- **Phase 3:** Blitter path for `blit_planar_masked` (STE/Mega/TT), runtime-detected.
- **Phase 4a (DONE):** planar mouse cursor (`rdr_pcursor_*`) + planar-aware `present_map`/`present_cursor`/
  `present_frame`, so native planar applies with the always-on cursor → LIVE ~1.35 fps. Pixel A/B identical.
- **Phase 4b (DONE):** drop the chunky composite on walk frames (`g_chunky_dyn=0`) + hybrid
  `present_frame` → **1.62 fps live**, pixel A/B identical. Caveat: PREDIM-only, ST defaults to SMOOTH.
- **Phase 4c:** planar dimming (→ ~2 fps); actors as standalone planar sprites; the full lit scene.

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

**Phase 1 DONE — primitives byte-verified (2026-07-19, `make bench-st`).** The three integration pieces
now exist and are proven byte-identical to c2p on real hardware (`PLANAR blit-vs-c2p diff=0`):
- `rdr_chunky_tile_to_planar(chunky, lut, out)` — one-time convert (256-B chunky → 128-B planar +
  32-B mask), remapping opaque indices through the same 256→16 lut as c2p4_lut; 255 → mask bit 0.
- `rdr_blit_planar_masked(dst, dstride_w, pt, x, y)` — the one hot primitive: per row+plane
  `d=(d&~m)|(s&m)`. Fast group-aligned path (sh==0, the compose case) + the 8-px-inset offset path
  (each plane spans groups gx, gx+1 via a 32-bit shift window). Exactly the validated Phase-0 case (c).
- `rdr_planar_get(key, chunky, lut)` — direct-mapped (tile,level) planar cache, 512×160 B = 80 KB,
  same Knuth hash as gamedemo's `g_dimcache`; the planar analogue of `dim_tile`.
- **Gate:** `rdr_planar_selftest()` proves the invariant `blit(planar(ct), x) over c2p(bg) ==
  c2p(bg⊕ct masked at x)` across 4 tile patterns (opaque / sparse-transparent / left+top-edge-
  transparent = the multi-tile bleed / a dim variant) × 2 offsets (aligned + inset), building the tile
  THROUGH the cache. Result: **0 mismatched plane-words**.

**Phase 2 DONE — integrated, byte-verified, measured (2026-07-19, `make bench-st`).** The compose now
maintains a planar mirror and presents with no c2p; the shared chunky compose LOGIC is untouched — only
the per-cell OUTPUT diverges (§3.4 held). Pieces:
- **Incremental planar STATIC** (`g_pdimmed`, renderer-owned): full c2p on rebuild, planar block-shift
  on scroll (whole-group units → bit-exact), and per-dirty-cell c2p in lockstep with `dim_cell_into`
  (the same revealed-edge + light-changed cells). Mirrors gamedemo's `g_dimmed_map` maintenance.
- **Dynamic planar blits**: the ST `draw_cell`/`draw_multi` (fields, actors, avatar, foreground) and the
  roof loop blit the same cells into `g_ptmp` via the (tile,level) planar cache — the chunky copies are
  skipped on the planar-only path (`g_chunky_dyn=0`).
- **No-c2p present** (`rdr_ppresent`): ONE shifted planar block-copy of `g_ptmp`'s 160×160 span into the
  screen at the +8-px `MAP_INSET` (per-group `screen[g]=(src[g-1]_lo8<<8)|(src[g]_hi8)`; border cols
  preserved).
- **Gate (INV2, `rdr_pverify`)**: after every composed frame, `g_ptmp == c2p(tmp)` over the visible span.
  **Result: 0 mismatches over 41 frames** (a full rebuild + a two-directional walk).

Measured, PREDIM night throne room, plain ST 8 MHz (`make bench-st`, `LIGHTSTY.REQ=3`):

| path | compose | present | **frame** | fps |
|------|--------:|--------:|----------:|----:|
| baseline (PREDIM chunky + c2p) | 489 ms | **444 ms** | 933 ms | 1.07 |
| native planar-only            | 538 ms | **81 ms**  | **619 ms** | **1.62** |

The **present is 5.5× cheaper** (444→81 ms) and the **frame 1.51× faster** (1.07→1.62 fps), byte-identical.
The residual +49 ms compose is the planar STATIC c2p maintenance (per-dirty-cell, ~20 cells/frame on a
lit walk); dimming DIRECTLY in planar (a planar darklut, later phase) would remove even that c2p.

**Phase 2c DONE — wired into the LIVE ST game + pixel-validated (2026-07-19).** `compose_map` now
enables native planar every frame when eligible — ST backend + PREDIM + the map region is a single LUT
(`rdr_native_planar_ok`; a band raster only swaps hardware palette REGISTERS per row via Timer-B, which
is orthogonal to the framebuffer, so planar is fine as long as the map sits in one band — it does, band
0). `present_map`'s clean-walk branch (no cursor) presents via `rdr_ppresent`. Correctness details:
- Planar tiles are built with the map's actual row-LUT (`g_row_lut[MAP_INSET]`), so they match the
  chunky present exactly whether or not a band raster is active.
- The planar caches are invalidated on any LUT change: `rdr_set_lowcolor` (new scene palette) and
  `rdr_set_lowcolor_bands` mark dirty; `compose_map` forces a planar-static rebuild (`rdr_planar_take_dirty`
  → `g_pd_valid=0`). Colour CYCLING rotates only the registers (not the LUT) so it needs no rebuild.
- The live path keeps composing chunky too (`g_chunky_dyn=1`) so `g_back` stays valid for the cursor,
  the full present, and bare mouse-moves; `g_ptmp == c2p(the chunky map)` by INV2, so `rdr_ppresent` is
  byte-identical. (The bench measures the planar-ONLY ceiling by dropping the chunky composite.)

**Validation (real 8 MHz ST via Hatari, `AUTOWALK.REQ` walks 6 tiles then holds the frame):** the map
renders correctly (throne room, PREDIM-lit, papyrus border + panel intact), `native_planar=1`. A pixel
A/B of the SAME scene composed planar (`rdr_ppresent`) vs chunky (`PLANAR.REQ=0`, c2p) differs by **124
px in a 12×14 box** — exactly the `native_planar=1`/`=0` debug glyph; **the map, panel and border are
pixel-identical**. Live clean-walk frame ≈ 658 ms compose (both paths) + 81 ms present ≈ 739 ms / 1.35 fps
(present 5.5× cheaper); the bench planar-only ceiling is 619 ms / 1.62 fps.

**Phase 4a DONE — planar cursor, so native planar actually APPLIES in real play (2026-07-20).** Crucial
discovery: the game's mouse cursor (`g_cw`) is set once at init and never cleared, so `present_map` ALWAYS
took its cursor branch (chunky c2p) — the Phase-2c planar present only fired with the cursor off, i.e.
never in real play. Fix: a planar mouse cursor drawn straight into the 4-plane screen with save-under
(`rdr_pcursor_show`/`_hide`/`_invalidate`, per-pixel plane read/write, coloured via the cursor row's LUT).
All three present paths go planar when native planar is active:
- `present_map` (walk): `pcursor_hide` → `rdr_ppresent` → `pcursor_show`.
- `present_cursor` (bare mouse move): `pcursor_hide` → `pcursor_show` (map unchanged).
- `present_frame` (UI redraw): full 1:1 c2p of `g_back` (still valid, `g_chunky_dyn=1`) → `pcursor_show`.
Consistency: each path ends having saved-under the new cursor; the on-edge invalidates any stale save-under.

**Validation (real 8 MHz ST, `AUTOWALK` 6 tiles, cursor left ON over the map):** the white arrow renders
correctly on the planar map; a pixel A/B of the SAME scene planar vs chunky (`PLANAR.REQ=0`) differs by
136 px = the `native_planar=1`/`=0` debug glyph + a 12-px status-line capture-timing speck — **the map,
the cursor, the panel and the border are pixel-identical.** So the 5.5×-cheaper present (444→81 ms) now
applies to actual gameplay: **~1.35 fps live** (compose still both-paths for `g_back` validity).

**Phase 4b DONE — dropped the redundant chunky composite on walk frames → 1.62 fps live (2026-07-20).**
`g_chunky_dyn=0` in production now: the chunky DYNAMIC composite (actors/fg/roofs into `tmp`, the ph0
`tmp` copy, the ph3 `g_back` memcpy) is skipped, so `g_back`'s inner-map region (rows 8..167) goes stale —
but every gameplay present of it is planar. Audit of `g_back` inner-map readers: `present_map`/
`present_cursor` read the planar screen (fine); `present_frame` now overlays `rdr_ppresent` AFTER its
panel c2p (map from planar, panel from `g_back`); the modal views (`overview`/Peer/Wizard-Eye, `book_view`)
present via `rdr_present_doubled` and write their OWN `g_back` content (self-contained); `dump_shot`
(SHOT.REQ) is Falcon-only (native planar never activates there) + warns on the ST edge case. The chunky
STATIC layer (`g_dimmed_map`) is still built — it feeds `g_pdimmed`.

Result: **live walk = 538 ms compose + 81 ms present = 619 ms / 1.62 fps** (was 739 / 1.35), the bench
planar-only ceiling now realized in the game. Pixel A/B on a real ST (planar vs chunky, cursor on, incl.
the hybrid `present_frame`): 136 px = the debug glyph + a cross-run NPC/timing speck — **map, cursor,
panel, border pixel-identical**.

Caveat: native planar activates only under PREDIM lighting, and the ST currently defaults to SMOOTH — so
the whole milestone is opt-in via `LIGHTSTY.REQ=3`. Making PREDIM the ST default (the capstone that makes
native planar the shipping ST path) is a deliberate visual decision, pending.

**Phase 4c STARTED — planar dimming (toward ~2 fps).** The residual compose cost is the per-cell c2p
(`rdr_pstatic_cell`, ~88 ms/walk) that converts the chunky-dimmed static (`g_dimmed_map`) to planar,
plus the chunky edge recompose. The plan: **compose the STATIC layer directly from pre-dimmed planar
tiles** — drop `g_tmp_map`/`g_dimmed_map`/`dim_cell_into`/`rdr_pstatic_cell` on the ST and maintain only
`g_pdimmed`, incrementally (planar shift on scroll + per-dirty-cell planar recompose). This rests on ONE
identity — a per-pixel darklut COMMUTES with tile overlay:
`blit(dim_base(base)) then blit(dim_obj(obj)) == c2p(darklut[fill(base,255→0) ⊕ obj])`
— where the base dims after mapping transparent→0 (opaque, mask all-1) and the object preserves 255
(masked). **Proven byte-for-byte:** `rdr_planar_dim_selftest` (BENCH) = `dim-commute diff=0` across
opaque/holed-base/edge-transparent-object patterns. Projected: removes the ~88 ms c2p + much of the
~118 ms chunky edge recompose → compose ~400 ms → frame ≈ 480 ms / **~2 fps**.

Remaining for 4c (Phase-2-scale, byte-verified via a static INV): a `dim_tile_base` variant (255→0 then
darklut, cached), route `fill_tiles`/`draw_view_objects` to blit planar-dimmed tiles into `g_pdimmed`
(the static analogue of `draw_cell`'s dynamic planar path), and a planar incremental recompose replacing
`dim_cell_into`+`rdr_pstatic_cell`. Then drop the chunky static on the ST.

**Phase 3** — Blitter path for the masked blit (STE/Mega).
