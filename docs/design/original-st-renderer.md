# Original ST-port renderer — architecture, and what it tells our port

> Provenance
> - Derived from : reverse-engineering the user's own original **GAME.PRG** (the ST gameplay
>   engine), Pack-Ice–depacked with our own clean-room depacker; behavioural reading of the
>   disassembly. Front-end analysis from **U.PRG**.
> - References   : none copied. Cross-checked against our own measured `docs/design/performance.md`.
> - Tools        : external RE workspace `~/src/u6e-re` (our hand-rolled 68000 disassembler +
>   codemap/a4map/depack_ice; per-address symbol map in that workspace's `game_labels.csv`).
> - Validated by  : architecture converges with u6e's independently-measured perf work
>   (`performance.md`). NOT emulator-verified; a few pixel-scale details remain open (below).
> - Confidence   : probable (structure), guessed (exact on-screen tile pitch of the full-refresh view)
> - Last updated : 2026-07-18

## Why this doc exists
Not to introduce new techniques — `docs/design/performance.md` already has u6e doing windowed
present, incremental buffer-scroll, dirty-rect holds, spatial bucketing, and hand-asm c2p. This doc
records what the **original 1990 ST port** actually does, because it (a) **validates** that our
renderer converged on the same architecture the shipping original used, and (b) isolates the two
things the original left completely unused — the **Blitter** and **STe/Falcon hardware scroll** —
which are u6e's remaining perf headroom. Addresses and the disassembly live in the external RE
workspace; this is the behavioural spec.

## The original's rendering architecture (behavioural)

**Binary split.** The ST port is two programs: a front-end (`U.PRG` — intro/menu/character
creation) that hands off to the gameplay engine (`GAME.PRG`), which is Pack-Ice compressed and holds
the map renderer and all world data (`chunks`, `maptiles.ega`, `objtiles.ega`, `basetile`,
`tileindx`, `animmask.ega`, object blocks).

**Map data path (frugal, cached).**
- Superchunk blocks are paged in on demand from disk into a cache.
- Individual 8×8-tile **chunks (64 bytes, 1 byte/tile)** are held in a small **LRU cache** — loaded
  on demand, evicted least-recently-used; the engine never re-reads the whole map per frame.
- The visible area is materialised into a small **decoded map array** (a compact row-major grid of
  tile indices) that the renderer reads — it does not re-walk chunks during drawing.

**Redraw model — incremental LIGHTING/LOS, but FULL-VIEW pixel repaint.** Two distinct mechanisms,
and the distinction is the whole point of this doc:
- *Line-of-sight and lighting are incremental.* On a move, a visibility flood walks outward from the
  avatar; the cells it reaches are pushed onto a small **ring-buffer queue** (head/tail indices) and
  drained by a consumer that recomputes per-cell **light** (radial falloff from a distance table)
  and **visibility** bits into two per-cell state arrays. Only cells near the avatar (bounded by the
  light radius) are touched. This is the genuinely incremental part.
- *The tile PIXELS are repainted for the whole visible view.* The view redraw is a grid loop that
  draws **every cell** of the visible window each time — a visible cell as its tile, an out-of-sight
  cell as black — gated by the visibility bit but with **no skip for unchanged cells**. It runs per
  move (via the render+animate wrapper). A separate initial/new-area full-refresh path also exists.

So the original is incremental in **lighting/LOS**, but **not in pixels**: every move re-blits the
whole visible view with the CPU. This corrects an easy misread (the queue looks like a dirty-*pixel*
queue but drives lighting) — and it means our incremental buffer-scroll is an advance, not parity
(below).

**Per-cell draw = attribute/animation dispatch, then a tile blit.**
Each cell goes through a dispatcher that consults per-tile **attribute/animation tables** (flags for
passability, animation frame, transparency, and a dissolve/checkerboard reveal option) before the
pixels move. Animation frames are advanced by a wrapper that ticks a per-tile frame counter using
the draw routine's return value.

**Tile blit = planar → interleaved, double-buffered, alignment-sensitive.**
- Tiles are stored **planar** (four 1-bpp planes) and blitted into the **interleaved** ST screen —
  the same planar-source → interleaved-screen model as our `rdr_st`.
- The screen is **double-buffered** (two buffers, drawn to the hidden one).
- Destination is `screen + rowtable[Y] + (X&~15)/2` — a **row-address table** for the scanline, and
  horizontal placement rounded to a 16-pixel word.
- **Two copy paths by alignment:** when the tile lands on a 16-px word boundary, the fast path does
  **four `move.w` per row (the 4 planes) then advances one scanline** — pure word copy. When it
  lands on the odd 8-px half, a **byte-split path** writes each plane as two `move.b` into adjacent
  word groups — **~2× slower**. There is a separate general blit that handles arbitrary sub-pixel
  shift (slower still), used by the full-refresh path.

**Palettes.** Per-scene 4-bit STE palettes set via XBIOS, applied **VBL-deferred** (staged, then
loaded in the vertical-blank handler) — not mid-screen raster splits.

**Hardware surface (the key negative result).** The engine drives almost no hardware directly — it
uses XBIOS for video and touches only PSG (sound) and the keyboard ACIA. It **does not use**: the
**Blitter**, **STe hardware fine-scroll**, or **STe DMA sound**. It treats an STe as a plain ST (the
only STe feature used is the 4096-colour palette). This is the "lazy port" our M7 milestone set out
to beat, now confirmed at the hardware level.

## What this validates in u6e (cross-ref `performance.md`)
The original independently used the same wins we measured our way into — good evidence our
architecture is right, not just expedient:

| u6e (measured, `performance.md`) | Original ST port (this RE) |
|----------------------------------|----------------------------|
| Incremental buffer-scroll: on a 1-tile move, memmove the window + repaint only the new edge | **Full-view pixel repaint every move** — the original does NOT do this; it re-blits the whole visible view. **We are ahead here.** |
| Incremental lighting/visibility (light/LOS recomputed for the affected region) | Incremental lighting/LOS via a ring-buffer flood queue (only cells near the avatar) — same idea |
| 16-px c2p group granularity; keep tiles word-aligned | Fast tile path is 16-px-aligned word copy; unaligned falls to a ~2× byte path |
| Double-buffered planar backbuffer → interleaved/c2p present | Double-buffered; planar tile source → interleaved screen |
| Object **spatial bucket index** (visit only in-view buckets) | Chunk **LRU cache** + decoded view array (touch only the visible window) |
| Per-scene / band-raster palettes | Per-scene STE palettes (VBL-deferred; we go further with Timer-B band rasters) |

Takeaways: (1) our **incremental pixel scroll is a real advance over the original**, which repaints
the full view in software every move — do not regress it. (2) The original's *incremental* part is
lighting/LOS, and we already do the analogous thing. (3) Keep tiles word-aligned so the fast copy
path stays the common case.

## Lighting: BINARY per-cell visibility (the native-planar model)

The most decision-relevant finding for the ST/STe/Amiga ports (RE: `~/src/u6e-re/REVERSE.md`
§"Lighting APPLICATION"). The original computes a graded light field but **renders it binary**:

- **Compute** (`light_accumulate $7efe`): per cell, read a **radial-falloff table** (indexed by
  `|dx|,|dy|` from the light source), clamp `4 − falloff + base` to **0..4**, accumulate into a
  per-cell byte `g_light` (sums multiple sources), and if the contribution > 0 set bit 7 of the
  per-cell `g_vis_state`. Then flood to the 8 neighbours (a ring-buffer queue) — incremental, only
  cells near the avatar. (This half matches what u6e already does.)
- **Apply** (`terrain_grid $3069c`): reads **only the visible bit** — visible → the real tile;
  not-visible → **a solid black tile (index `$FF`)**. Gated by a "lighting-active" flag (off outdoors
  by day → every tile full-bright). **The 0..4 level never touches a pixel** — it only drives light
  propagation and the visible threshold. So on-screen lighting is **binary, at cell granularity**.

**Why this matters — the perf lesson.** u6e currently applies lighting as a **per-pixel bilinear
darkness LUT** (`compose_map` ph3). Benched on TT that phase is ≈ **72 ms/frame — the single biggest
cost, larger than c2p (50 ms)** (`performance.md`). The original spends ≈ 0 extra at draw time:
lighting is a **per-cell tile choice** (40×40 = 1,600 cells) that the planar tile blit already
carries — versus our **per-pixel pass** (160×160 = 25,600 px) layered on top of compositing.

**Native-planar recommendation (`rdr_st` / future Amiga).** Move lighting off the pixel path and onto
**tile selection**, computed per cell before the blit:
- level 0 → the black tile; max → the real tile (this alone = the original, and it's essentially free);
- for a gradient the original lacked, add a **small set of dither-masked or pre-dimmed tile variants**
  (e.g. 3–4 levels via 2×2/4×4 stipple masks AND-ed into the planes) — still per-cell, still absorbed
  by the blit, no per-pixel math. Build the dim/dither variants in the host asset pipeline.
- This removes ph3 entirely on those targets **and** eases c2p pressure (fewer distinct pixels). On
  TT/Falcon we can keep the smooth per-pixel LUT (bandwidth affords it) or offer the tile-level model
  as a fast mode. See `docs/rules/lighting.md` (LIGHTING_STYLE_ORIGINAL vs _SMOOTH) — this adds a
  third point, **_DITHER**, between them.

## The remaining headroom the original left on the table
These are the levers the shipping original never touched — the genuine performance ceiling for a
"better than the original" ST/STe port. They line up with the still-open items in `performance.md`.

1. **Blitter-offload the hot blit (STe always; Mega ST usually).** The per-cell tile copy is pure
   CPU `move.w`/`move.b`. Copying a planar tile into the interleaved screen at a fixed line stride is
   a textbook Blitter job (2–4× the 68000 on those machines) and is a drop-in at the blit leaf, not a
   rewrite. This is `performance.md`'s option 4 ("Blitter-assisted"), and the RE confirms the
   original gives it up entirely. Gate on the blitter-present capability `rdr_st` already detects.
2. **STe/Falcon hardware fine-scroll — the double win.** The ~2× byte-split path exists *only* to
   place tiles at sub-16-px horizontal offsets. Hardware fine-scroll keeps every tile word-aligned
   (fast path always) **and** turns a one-tile move into "shift + repaint one edge strip" in
   hardware. This matches `hw-scroll-per-target` (STe yes, TT-low no) and `performance.md`'s Falcon
   VIDEL note ("real HW scroll would make the pan HW-driven — the next 'take advantage of the
   Falcon' step").
3. **Hoist the per-cell dispatch.** The original re-derives attribute/animation/dissolve per cell
   before every blit. On mostly-static terrain, precompute a per-tile **draw descriptor** (blit
   variant + source pointer + shift) once when a tile enters view; the inner loop becomes "blit
   descriptor N at (x,y)." (u6e's static/dynamic layer split already does the analogous thing.)
4. **STe DMA sound** (audio, not framerate) — the original is PSG-only even on STe; our M10 DMA-sound
   backend is a "better than the original" axis.

## Recommendations for `rdr_st` (priority order)
1. Add a **Blitter path** for the tile copy + planar→interleaved present, selected by capability
   detection; keep the C/asm CPU path as spec + fallback (as we already do for c2p).
2. On **STe**, add hardware **fine-scroll** for the map window; on **Falcon**, VIDEL line-width
   scroll (`performance.md` open item). Both shrink the redraw set *and* remove the unaligned slow
   path.
3. Keep the map-window/tile geometry **16-px word-aligned** end to end so the fast copy path is the
   common case (already true via the 16-px c2p group).
4. Preserve our **incremental pixel scroll** — the original repaints the full view in software every
   move, so this is a lead we already hold; never regress to a full-viewport pixel repaint. (Its
   incremental machinery is lighting/LOS only, which we also do.)

## Open questions
- **Exact on-screen tile pitch of the view.** The per-move view redraw iterates a 40-wide grid with
  a small per-cell pixel step and a dual-layout row table that did not resolve cleanly under static
  analysis, so the precise on-screen tile size is unconfirmed. It does not change any recommendation
  (the cost is a full-view CPU repaint regardless). One Hatari breakpoint on the per-cell dispatcher
  during play settles it.
- **Confirmed via the queue consumer:** the flood queue drives **lighting/LOS**, not pixels, and the
  view redraw repaints **every visible cell each move** — so a single step's *pixel* cost is the
  whole view, which is exactly the full-view repaint the Blitter / HW fine-scroll levers target (and
  which our incremental scroll already avoids). An emulator counter on the flood queue would give the
  exact lit-cell count per step, but it does not gate the pixel cost.

## Cross-links
- `docs/design/performance.md` — u6e's measured optimisation roadmap (the numbers).
- `docs/design/renderer-unification.md` — the `rdr_*` backend architecture these land in.
- `docs/design/render-tt.md`, `hw-scroll-per-target` — per-target scroll capabilities.
- Detailed per-address symbol map: external RE workspace `~/src/u6e-re` (outside this repo).
