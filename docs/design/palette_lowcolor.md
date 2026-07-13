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
>                  build/gamest_screen.png: throne room in the optimised 16 colours); the Timer-B
>                  BAND raster proven with an R↔B-swap band 1 (`make shot-st-raster`, crisp seam).
> - Confidence   : confirmed — quantiser core + U6WW pack + rdr_st shifter backend + U6WX band
>                  rasters all validated on the ST. Remaining work is footprint + per-scene palettes.
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

## Band rasters (U6WX) — the "cheat past 16 colours"
The ST shifter has 16 colour registers, but they can be RELOADED mid-frame by a timed interrupt,
so each horizontal band of the screen shows its own 16 → up to nb×16 colours per frame.

- **Mechanism** (`render/rdr_st.c`): the MFP Timer-B in EVENT-COUNT mode counts DE (display-enable)
  pulses = one per displayed scanline. The VBL routine loads band 0's palette during vblank and arms
  Timer-B for the first boundary; each Timer-B interrupt loads the next band's 16 colours straight
  into `$FF8240..` and re-arms (stop/reload/restart). A few interrupts/frame — negligible cost.
  `rdr_present` also picks each row's LUT from a per-row table so the c2p remaps to the right band.
- **Data** (`tools/mkworld.c` → U6WX, appended after U6WW): `[u8 nb][nb u8 y0][nb*48 pal][nb*256 lut]`,
  palette as raw 3-bit levels. Loaded into `g_lcx_*` and handed over via `rdr_set_lowcolor_bands`.
- **Layout** (2 bands, SCENE-NEUTRAL so it never hurts other maps): band 0 (rows 0..181 = map + panel
  + status) = the global U6WW 16; band 1 (rows 182..199 = command bar) = a 16 quantised from the
  command icon tiles (400..409) + the parchment under them. So the command bar renders in
  icon-optimised colours and band 0 stops spending slots on icon-only hues.
- **Validated**: `make shot-st-raster` installs a diagnostic band 1 with R↔B swapped — the command bar
  visibly inverts at a crisp row-182 seam (proof the Timer-B split fires, no flicker). The real 2-band
  render adds ~16 colours in the command band (frame total ~31 distinct > the 16 of one palette).
- **Honest scope**: U6's UI is a VERTICAL split (map left, panel right, SAME scanlines), so horizontal
  bands can't give the *map* its own palette — the big win needs the mid-line split (see status list).
  A supervisor note: the shifter registers ($FF8240) are supervisor-only; the interrupt handlers write
  them directly (they run supervisor), but the one user-mode load in `rdr_set_lowcolor_bands` must go
  through XBIOS `Setpalette` — a direct write there bus-errors.

## Mid-line split — investigated, ruled out (`make splitcheck`)
A mid-line (horizontal-position) raster would reload the palette at x=176 so the map (left) keeps a
terrain-16 while the panel (right) gets its own 16 on the SAME scanlines — the only way past U6's
vertical UI split. We measured whether it would actually help (`tools/splitcheck.c`):

| content (via ST 3-bit 16)        | through the map's terrain-16 | through its OWN 16 |
|----------------------------------|------------------------------|--------------------|
| MAP window                       | already optimal — band 0 IS the tileset palette | (same) |
| PARCHMENT (PAPER.BMP)            | RMS 17.74                    | RMS 8.25           |
| conversation PORTRAIT (item 0)   | RMS 35.09                    | RMS 17.49 (**101% worse via terrain**) |

So the **map has zero headroom** (its palette is built from exactly the tiles it shows), and the
real headroom is the **panel portrait** — the terrain-16 has no cool greys/whites, so a grey
portrait renders muddy brown (build/splitcheck.png: VGA | via terrain-16 | via portrait-16).

BUT a mid-line raster **cannot deliver** that win: a portrait needs a *full* different 16, and on an
8 MHz 68000 writing N palette registers takes ~0.75N µs → the beam crosses ~6N pixels mid-write,
far exceeding the ~16 px parchment gap between map and panel. Only ~2 registers swap cleanly; the
rest smear into the portrait. So the split can change a couple of colours at most — not a portrait.

**Conclusion**: the deliverable for the portrait/parchment headroom is a per-MODE palette swap
(conversation / dungeon / town → its own ST 16 via `Setpalette` + LUT, a cheap mode change, no
raster, full 16). The frozen map during a conversation would share that palette — acceptable since
the eye is on the portrait, and a map+portrait compromise 16 keeps both decent. Tracked above.

## Conversation-mode palette (U6WY) — capturing the portrait headroom
The split-check found the real low-colour headroom is the conversation PORTRAIT, and that a mid-line
raster can't reach it — but a per-MODE palette swap can. So while an NPC's portrait is up, the ST
swaps its whole 16 to a palette tuned for the conversation screen (no raster, full 16):

- **Data** (`tools/mkworld.c` → U6WY, after U6WX): one 16 + 256-LUT per portrait, indexed exactly as
  the engine loads portraits (idx<98 → PORTRAIT.A[idx], else PORTRAIT.B[idx-98]). Each is quantised
  from the PORTRAIT (up-weighted ~50% — it's the focus) + the TERRAIN histogram (~35%, so the frozen
  map stays recognisable) + PARCHMENT (~15%, the text panel). 194 slots, ~59 KB. `[4 "U6WY"][u16
  nport][nport × (48 pal + 256 lut)]`.
- **Engine** (`render/gamedemo.c`): `talk()` calls `conv_palette_enter(id-1)` → `rdr_set_lowcolor`
  (the portrait's 16 + LUT); `end_talk()` calls `conv_palette_leave()` → restores the banded game
  palette. The main loop's full `present_frame()` re-c2p's the whole backbuffer through the new LUT,
  so map + panel both re-map. No-op on TT/Falcon (`rdr_set_lowcolor` is a no-op — they show 256).
- **Validated** (`make shot-st-conv`): Lord British's portrait renders with smooth natural skin
  instead of the terrain-16's red-blotched face (build/conv_compare.png: left terrain-16 | right
  conversation palette; 93% of portrait pixels change), and the frozen map stays correct. Cool-toned
  portraits gain more (the split-check's 101%). Baseline toggle: CONVPAL.BIN byte1 bit0 disables the
  swap.

## Footprint — booting on a real 4 MB ST
Measured on a real 4 MB ST (`MEMINFO.BIN` diagnostic → the `MEM ...` console lines): the ST-RAM
pool is **2579 KB** (after the TPA — text 194 KB + BSS ~1019 KB — and GEMDOS headroom). The FULL
pack is 2473 KB (incl. 64 KB scratch), leaving only **106 KB** — so the conv/portrait libs (~583 KB)
fail to load and rdr_init dies "no renderer".

The dominant lever: the **front-end/intro art is 1383 KB** — over half the pack — and gameplay
never touches it (intro cutscene + gypsy chargen visuals). So the ST ships a **slim pack**:

- `tools/mkworld.c` with `MKWORLD_SLIM=1` writes a 4-byte **`NOFE`** marker in place of the whole
  front-end art block (U6WU..avatar-climax). Slim pack = **1025 KB** (vs 2409 KB full).
- The engine loader gates the front-end parse on that marker (`NOFE` → skip, pointers stay NULL);
  `run_menu`/`run_intro` already no-op on absent art, and the default boot goes straight to the
  shipped world. `-DPACK_NAME="U6ST.PAK"` selects the slim pack for the ST build.
- Result on a real 4 MB ST (`make shot-st`, `--memsize 4`): pack+scratch 1089 KB, libs load
  (**libs_ok=1**), **906 KB free** — the throne room renders and conversations work
  (`make shot-st-conv`). TT/Falcon are unaffected (they load the full U6.PAK).

The front-end on ST is then STREAMED (loaded, run, freed) from a separate U6FE.PAK — see "Front-end
streaming (DONE)" below. BSS is a further target (the full-world maps g_indoor/g_sightblk/g_blocked/
wf are 128 KB each) if more headroom is ever needed.

## Front-end streaming (DONE) — the ST title/intro/chargen off the hard drive
The 1.35 MB front-end art can't coexist with the gameplay core+libs on a 4 MB ST, so the ST STREAMS
it — the period-correct pattern (load from HD, use, free; don't hold it resident). The foundation
is `plat_chip_mark()`/`plat_chip_reset()` (platform/platmem.h): a scoped (LIFO) region free on the
ST-RAM pool.

- `tools/mkworld.c` (`MKWORLD_SLIM`) writes the front-end art to a separate **U6FE.PAK** =
  `[4 "U6FE"][48 pal][256 lut][2048 font][front-end art]`; the core slim pak gets a `NOFE` marker.
  The palette/lut are the game 16 (U6WW), so the front-end can render before the core loads.
- `render/gamedemo.c` (`-DST_STREAM`) reorders the ST boot: `rdr_init` early → `plat_chip_mark` →
  load U6FE → `rdr_set_lowcolor(fe pal/lut)` + `parse_frontend(fe art)` → run the menu/intro/chargen
  → `plat_chip_reset` (frees the 1.35 MB) → then the core + libs load into the reclaimed space. The
  chargen result (name/stats) lives in globals, so it survives the free. `parse_frontend()` is the
  extracted loader block (also used inline by the full pack). All `#ifdef ST_STREAM` — TT/Falcon keep
  the inline path byte-for-byte.
- **Validated on a real 4 MB ST** (MEMINFO): U6FE resident = 1543 KB used; freed → core+libs load
  (1183 KB, 808 KB free, libs_ok=1); the "Ultima VI" title + main menu render in the optimised 16
  (`make`, MENU.REQ → build/gamest_menu.png); character creation runs from the streamed pack and
  survives the free (`CHARGEN.REQ=N` → "NEWGAME avatar: Milady"). TT boots unchanged.
- **Follow-ups**: per-SCENE low-colour palettes (the intro cutscene scenes carry PALETTES.INT
  palettes; right now they render through the single game 16 — recognisable but not tuned). BOOK.DAT
  text lived in the front-end block → on ST it's freed with U6FE, so in-game sign/book reading is
  nulled until BOOKS moves to the core (or streams on demand) — the next application of this same
  streaming mechanism.

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
- [x] **Footprint** — DONE: the ST now boots + plays on a REAL 4 MB ST (below). A slim pack
      (`MKWORLD_SLIM` → U6ST.PAK) omits the ~1.35 MB front-end/intro art gameplay never uses.
- [ ] Per-scene low-colour palettes + colour-cycling on ST: `rdr_set_palette(256)` is a no-op on
      the shifter (16-colour hw can't take a 256 update), so cutscene palettes + CLUT cycling
      (water/fire) don't yet show. Reserve cycling slots (below) + ship per-scene 16s in U6WW.
- [x] Band-palette rasters (Timer-B) for >16 on-screen colours — the "cheat past 16" (below). Ships
      the U6WX section (2 bands) + a Timer-B palette-swap chain in rdr_st; validated on the ST.
- [ ] Reserve palette slots for colour-cycling ranges; flag over-budget scenes.
- [ ] Optional ordered dithering (per-asset, off for UI/tiles).
- [~] MID-LINE split (map/panel, x=176) — INVESTIGATED, NOT the right tool (see below). The real
      headroom is the panel PORTRAIT, which a mid-line raster can't deliver; a per-mode palette can.
- [x] CONVERSATION-mode palette (U6WY) — DONE (below). Per-portrait ST 16 swapped in on talk().
- [ ] Extend per-mode palettes to dungeon / town scenes (same mechanism as the conversation swap).

## Implemented / consumed by
- Host: `tools/quant.h` (core) + `tools/quantize.c` (`make quantize`, QA PNG) +
  `tools/mkworld.c` `build_lowcolor` (ships the U6WW section).
- Engine: `render/gamedemo.c` loads U6WW → `g_lc_pal[48]` / `g_lc_lut[256]` / `g_have_lowcolor`
  (`make lowcol` prints the load check), and hands it to the backend via `rdr_set_lowcolor`.
- Backend: `render/rdr_st.c` (16-colour shifter) consumes it — `make game-st` / `make shot-st`.
  `rdr_tt.c` / `rdr_falcon.c` implement `rdr_set_lowcolor` as a no-op (they show all 256).
- Band rasters: `tools/mkworld.c` ships U6WX; `render/gamedemo.c` loads `g_lcx_*` + calls
  `rdr_set_lowcolor_bands`; `render/rdr_st.c` runs the Timer-B palette-swap chain (no-op on TT/Falcon).
  Mechanism check: `make shot-st-raster` (RASTER.BIN → R↔B-swapped command band).
- Conversation palettes: `tools/mkworld.c` ships U6WY (per-portrait); `render/gamedemo.c`
  `conv_palette_enter/leave` swap on `talk()`/`end_talk()`. Check: `make shot-st-conv`.
- Split-check (why mid-line was ruled out, where the headroom is): `tools/splitcheck.c`,
  `make splitcheck`.
