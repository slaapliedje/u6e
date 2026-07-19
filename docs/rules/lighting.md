# Rules: Lighting + visibility (U6)

Games: U6 (default) — Savage/Martian likely share the engine path; verify.
Implemented by: /render (gamedemo compose) + /engine; data via TILEFLAG (tileflag.md).
Tracked in: /docs/INDEX.md  ·  Milestone: M6

> Provenance
> - Derived from : Nuvie MapWindow.cpp (updateBlacking / generateTmpMap / boundaryFill /
>   reshapeBoundary / createLightOverlay / updateLighting / floorTilesVisible / drawRoofs)
>   and GameClock — READ to understand, implemented from this spec. Light-emitter data
>   confirmed against real TILEFLAG via tools/tflagprobe. Symptom + behaviour verified by
>   rendering the user's save in ScummVM headless (xvfb + `scummvm -x N`) vs our engine.
> - Confidence : model confirmed; some numbers (min-brightness, radii) to tune vs ScummVM.
> - Last updated : 2026-06-26

## 1. The two independent effects (U6 combines both)
1. **Visibility (line-of-sight)** — you only see tiles reachable from the avatar without
   crossing a sight-blocker (wall). Everything else is BLACK. Nuvie: `generateTmpMap()`
   flood-fills (`boundaryFill()`) outward from the player through open tiles, stopping at
   boundary tiles; unreached tiles are blacked out. Boundary tiles themselves are drawn
   (the wall faces) but not flooded through.
2. **Lighting** — visible tiles are then DARKENED by an alpha overlay = `255 - light`,
   where `light = ambient + Σ light-globes`. Tiles whose total light is low render dark/
   black even though they're "visible". Nuvie: `createLightOverlay()` + `updateLighting()`.

The user's symptom (kitchen east of the throne room shown lit, "wall on fire") is effect
**2**: there is NO wall between the throne room and the kitchen (sight-block map verified),
so visibility doesn't hide it — it's hidden because, indoors, the ambient is low and no
light-globe reaches it. We currently render everything fully lit, so we show it.

## 2. Ambient light (global), 0..255
From the game hour (GameClock) — Nuvie `createLightOverlay()`:
- Day (hours 6–18): ambient = **255** (full).
- Night: ambient = **cur_min_brightness** (low; tune ~ a small value).
- Dawn (5) / dusk (19): smooth transition between min and 255.
- Eclipse / dungeon: forced to min. X-ray cheat: 255.

CRUCIAL: this full-daylight ambient applies **OUTDOORS only**. Indoors (under a roof) the
ambient is the low value even at day — that's why the 08:00 castle interior is dark away
from torches. So lighting REQUIRES indoor/outdoor detection (§4).

## 3. Light sources (globes), per tile/object/actor
`light = GET_TILE_LIGHT_LEVEL(tile)` = **tileflags2 bits0-1** (0..3). CONFIRMED via
tflagprobe — the level-1..3 tiles are exactly the emitters:
- **3**: lit torch(611), brazier(797), holy brazier(798), candelabra(683), fireplace(717),
  campfire(890), lightsource(1021), lamppost(1160), moongate(603), lava(221), force field,
  volcano(1140).
- **2**: candle(648), cookfire(1126), stove(703), oven(830), fire(788), heatsource(1022),
  fire/electric/protection field.
- **1**: magic items (helms/shields/swords/wands/gems/amulets/crystal ball/wisp).
Plus **actors**: `actor->get_light_level()` — the Avatar/party carry light (a torch/the
in-game light); this is what lights the room you stand in. (Value/radius: tune vs ScummVM.)

Each source draws a light **globe** (radius scales with level) into the alpha map
(`drawalphamap8globe`). We approximate per-TILE (radius in tiles ~ level: 1→~1, 2→~2,
3→~3, avatar ~3); refine to a smooth falloff later (LIGHTING_STYLE_SMOOTH) vs ORIGINAL.

## 4. Indoor / outdoor detection — IMPLEMENTED: Nuvie roof_map (per-tile bitmap)
Daylight (§2) only reaches OUTDOORS; indoors uses the low ambient. Nuvie keys this off a
per-tile **roof_map** (`map->get_roof_data` / `has_roof`).

DECISION (user, after comparing data): match ScummVM EXACTLY — use Nuvie's `roof_map_00.dat`.

KEY FINDINGS (clean-room research + tools/roofcmp):
- U6 itself has **no roof data**. Nuvie's roofs are a Nuvie ADD-ON loaded from
  `maps/<game>/roof_map_00.dat` (Map::loadRoofData: sparse RLE, records [u16 skip][u8 run]
  [run×u16 tile], LE; roof tile != 0 ⇒ roofed). It IS shipped with ScummVM, inside
  `ultima.dat` (a zip) at `ultima6/maps/u6/roof_map_00.dat`.
- The roof_map is **deliberately sparse — only 2433 tiles** (Lord British's castle + one
  other large structure). ScummVM does NOT darken ordinary town-shop interiors by day; only
  roofed structures go dark. (A floor-tile heuristic over-darkens ~4×: it marks all 10181
  interior-floor tiles, i.e. every shop — verified, tools/roofcmp distribution grid.)
- Pure wall-ENCLOSURE flood also fails (open castle courtyard connects interior to outside).

IMPLEMENTATION (mkworld build_indoor_rle, docs path in lighting.md):
- PRIMARY: read `roof_map_00.dat` (path: $ROOF_MAP, else <refdata>/roof_map_00.dat, else
  build/roof_map_00.dat — the Makefile `roof-extract` unzips it from $(ULTIMA_DAT)). indoor =
  roof tile != 0. This matches ScummVM tile-for-tile.
- FALLBACK (file absent): U6's own "floor"/"carpet" LOOK-named tiles + wall-dilate (self-
  contained; over-darkens but keeps the build working without ScummVM data).
- We do NOT ship roof_map_00.dat; we transcode it into our own bitmap: 1 bit/tile
  (idx=y*1024+x, 131072 B) **byte-RLE'd** (~1.5 KB). Engine looks up indoor[y*1024+x].
- Clean-room note: roof_map_00.dat is Nuvie/ScummVM GPL-authored geography, treated as a
  USER-PROVIDED input (from the user's ScummVM install), like the original U6 assets.
- Verified (mkworld report): throne(316,367)/kitchen(313,350) INDOOR; open world outdoor.

## 5. Implementation plan (stages)
- B: data — DONE. light emitters shipped (tileflags2); boundary/wall flags shipped
  (tileflags1/2). Indoor bitmap (§4, roof_map) shipped as pack magic **U6WI** (RLE ~1.5 KB);
  engine decodes to `g_indoor[]` with `tile_is_indoor(x,y)`.
- C1: DONE — VISIBILITY flood (boundaryFill) at compose; flood VTxVT from centre through
  non-boundary tiles (boundary && !look-through); unreached cells black, wall faces shown.
- C2: LIGHT overlay — per visible cell, light = ambient(§2, gated by indoor §4) + globes(§3
  from emitter tiles + the avatar); darken/blacken cells below a threshold at compose.
- D: validate each against the ScummVM headless harness (throne room interior AND an
  outdoor scene); tune cur_min_brightness, radii, avatar light, threshold.

## 6. Test hooks
ScummVM ground-truth: `xvfb-run`/Xvfb + `scummvm -x <slot> ultima6` + `import -window`,
crop the map window, compare to our `make m5-svm` / a positioned capture at matched scale.

## 7. Status
- Stage B DONE: indoor bitmap via Nuvie roof_map_00.dat (U6WI).
- Stage C2 DONE (working, tuned vs ScummVM): light overlay in compose_map. Per-view-cell
  light = ambient (LIGHT_INDOOR_AMB=24 indoors / 255 day / LIGHT_NIGHT_AMB=56 night, gated by
  tile_is_indoor) + additive globes (BASE[]/RAD[] by emitter level, octagonal falloff) +
  avatar globe (150,r2). darklut[light>>5] (U6WJ) remaps each cell's pixels. Throne room now
  matches ScummVM: lit centre (braziers/throne/avatar), black far corners.
  - SMOOTHING DONE: light is computed per-cell (0..255) then BILINEARLY interpolated to each
    pixel (cell centre = tile+8px) and remapped via darklut[light>>4] (NLEV=16). Smooth
    gradient, no block edges — matches ScummVM's falloff. Tuning consts at top of the
    lighting block in render/gamedemo.c.
  - Debug toggles: -DLIGHT_DEBUG_BINARY (indoor=black/outdoor=full), -DLIGHT_DEBUG_FIELD
    (black where lev==0) for diagnosing the field.
- Stage C1 DONE: visibility/LOS flood in compose_map (compute_visibility). 4-connected flood
  from the avatar (view centre) through non-sight-blocking cells; sight-blocker = boundary
  (tflag2 bit2) AND NOT look-through (bit3) — so walls/closed doors/doorways block, open doors
  + windowed-open frames see through; objects checked via their current tile. reshapeBoundary
  shows sight-blocker cells touching a visible cell (wall faces draw at the vision edge);
  everything else is black. Non-visible cells also force light=0 (visible edges darken by walls).
  Verified: open throne room = all visible (no false blackout); corridors/rooms correctly hide
  everything behind walls (tools/visprobe replicates the flood). -DLIGHT_DEBUG_VIS isolates it.
- Stage D DONE (behaviour validated): outdoor daytime = full bright (ambient 255, no
  darkening); night (forced via $START_MIN test hook) = dim ambient (NIGHT_AMB) + a light
  pool around the avatar; interior matches ScummVM (throne room). Captures:
  build/game_outday.png, game_outnight.png, game_screen.png (throne).
  - (was a limitation, now FIXED by Stage E) outside-of-buildings.
  - Outdoor/night were validated as engine BEHAVIOUR (sane + correct character), not pixel-
    vs-ScummVM (we have a ScummVM save only for the throne room). A rigorous outdoor/night
    match would need ScummVM saves at those spots/times (writable via tools/nuviewrite).
  - $START_MIN (minutes 0..1439) in mkworld overrides time-of-day for night testing.
- Stage E DONE: ROOF DRAWING (Nuvie drawRoofs). Pack U6WK adds the roof tileset (286 distinct
  16x16 tiles, 255=transparent, palette == U6's — extracted from ScummVM's roof_tiles.bmp in
  ultima.dat) + a sorted roof map [u32 pos, u16 idx] (transcoded from roof_map_00.dat). Both
  point into the loaded pak (no extra BSS). Engine: `inside = tile_is_indoor(avatar)`. When
  OUTSIDE, overdraw the roof tile over each roofed view cell (roof_at() binary-searches the
  map), mark roofed cells visible, and gate indoor-darkening OFF (compute_light(inside)); so
  buildings show daylit/night rooftops, not their hidden interiors. When INSIDE, no roofs —
  interior + lighting unchanged. Verified: castle shows its rooftop from outside (was black),
  throne room interior unchanged. roof_tiles.bmp is a Nuvie/ScummVM asset used as a USER-
  PROVIDED build input (extracted from ultima.dat by the Makefile roof-extract), not shipped.
- Stage F — LIGHTING STYLES (prototype, 2026-07-19). `g_light_style` selects how the per-cell
  light field is applied (render/gamedemo.c ph3), runtime-selectable via `LIGHTSTY.REQ` byte
  '0'/'1'/'2'; default SMOOTH so nothing regresses. Motivated by the original ST-port RE
  (`docs/design/original-st-renderer.md`): the original applies lighting as a per-CELL tile
  choice, not per-pixel — hence cheap.
  - **SMOOTH (0, default)**: the per-pixel bilinear darklut above. Fidelity path for 256-colour
    TT/Falcon.
  - **DITHER (1)**: per-CELL light + ordered 4×4 Bayer dither between the 16 darklut levels — no
    per-pixel bilinear. Spatially mixes adjacent darkness levels for a low-colour-friendly
    gradient (aimed at `rdr_st`). Runs on both the 256 (Falcon) and 16-colour (ST) paths.
  - **BINARY (2)**: the original ST port's model (lit cell → full tile, else black) — reference.
  - **PREDIM+DITHER (4)** — the recommended combination: the pre-dim variant is itself DITHERED, so a
    cell reads as a sub-tile-smooth blend of two darkness levels instead of one flat level (closes
    PREDIM's tile-granular gap). `dim_tile_d` dims each pixel at darklut level k or k+1 by the 4×4
    Bayer threshold vs the cell's fractional light; cache keyed by (tile, Lc>>2) = 64 sub-levels, the
    high key bit distinguishing it from flat variants. Cache hits are still plain block copies, so it
    is **~free over plain PREDIM** (ST 862 vs 840 ms, +2.6%; ~94% hit rate — more misses than flat's
    99% due to the finer variants, but each miss is a one-time dither amortized over the hits).
    Verified: visibly smoother than PREDIM, full composite with actors. **This is the ST target.**
  - **PREDIM (3) — DIM-AT-DRAW (full composite, actors survive)**: lighting = every tile/sprite draw
    sources its PRE-DIMMED variant for that cell's light level, so the whole composite (terrain +
    objects + actors) dims for ZERO per-pixel lighting work. Hook: `draw_cell` knows its own cell, so
    when `g_predim` is on it fetches `dim_tile(tile, light[cell]>>4)` instead of the base tile — the
    masked copy loop is unchanged, dimming is baked into the source (transparency preserved so masked
    sprites stay masked). compose_map for PREDIM: compute the light field FIRST, then build a full
    dimmed composite into `tmp` (terrain via `draw_cell`, then the existing object/actor draws — all
    dim automatically), then a plain `tmp→screen` copy (no per-pixel lighting pass). Shipping all
    16 × 2048 dimmed tiles = 8 MB is impossible on a 4 MB ST, so tiles are **dimmed on demand into a
    128 KB direct-mapped cache** (`DIMSLOTS=512`, Knuth-multiplicative hash on `(tile<<4)|level`; a
    pair is dimmed once, ~99% hit rate) — used for ACTORS (ph1 dim-at-draw, transparent sprites).
    Chunky proxy for the planar path (the Blitter would blit planar variants). Caveat: **tile-granular**
    (per-cell, no sub-tile gradient — combine with DITHER for smoothness); roofs (outside-only) not yet
    dimmed.
  - **PREDIM incremental scroll (2026-07-19)** — the static layer no longer full-rebuilds each frame.
    A persistent DIMMED composite cache `g_dimmed_map` (the undimmed `g_tmp_map` dimmed per-cell) is
    memmove-scrolled in lockstep with `g_tmp_map` on a 1-tile walk; only the freshly-revealed edge band
    plus the cells whose **light changed** frame-to-frame are re-dimmed, every other cell is reused. The
    naive "scroll the baked buffer" is wrong because the avatar's own light globe is VIEW-anchored (it
    would smear); the fix is that the WORLD-anchored dimming translates exactly with the shift and only
    the changed cells (the globe's moving arc + emitters crossing the edge) get re-dimmed — so the free
    block-copy present is kept AND the rebuild collapses to edge+globe work. `predim_compose_static` +
    `dim_cell_into` (`render/gamedemo.c`); last-light is cached in `g_dm_light` to find the changed
    cells. **Proven byte-identical to a full dim rebuild** in all 4 scroll directions, flat and dither
    (`gamedemo.c` mode==11 self-test: `dimF`/`dimD` diffs all 0). Full rebuild still runs on jumps /
    style switches / first frame (gated on `g_dm_valid && g_tmp_valid`).
  - **Measured (TT, `make bench`, night throne room)**: DITHER cuts compose **131→103 ms/frame
    (~22%)** vs SMOOTH by dropping the bilinear; a per-pixel darklut lookup REMAINS (that residual
    only vanishes with the future planar tile-variant path — lighting baked into tile selection,
    zero per-pixel work). Present/c2p unchanged (52 ms).
  - **Measured (ST 8 MHz 68000, `make bench-st`, throne room = indoor)** — the real target, and it
    is brutal. Indoor scenes are ALWAYS non-uniform (LOS zeroes wall-blocked cells) so the bilinear
    runs day or night:
    `make bench-st` reports the SCROLL compose (1-tile walk = the real walking cost) split into
    ph0 static / ph1 actors+fg / ph2 light+vis / ph3 composite; the FULL compose (forced rebuild each
    frame) is the jump cost. Walking-frame numbers below use SCROLL compose:
    | style | SCROLL compose | ph0 static | c2p/frame | frame | ~fps |
    |-------|---------------:|-----------:|----------:|------:|-----:|
    | SMOOTH (current) | ~3025 ms | 158 ms | 678 ms | ~3.7 s | 0.27 |
    | DITHER | ~1048 ms | — | 678 ms | ~1.7 s | 0.58 |
    | PREDIM full-rebuild (pre-incremental) | ~865 ms | 598 ms | 678 ms | ~1.5 s | 0.65 |
    | **PREDIM incremental (flat, style 3)** | **~548 ms** | **282 ms** | 678 ms | ~1.2 s | **0.82** |
    | **PREDIM+DITHER incremental (style 4)** | **~639 ms** | **377 ms** | 678 ms | ~1.3 s | **0.76** |
    DITHER is **2.9× on compose** (vs 1.3× on TT) — the 68000's ~70-cycle hardware multiply makes the
    bilinear's 3 mults/pixel especially ruinous, and dither removes them. The lighting-APPLY itself
    goes ~free under PREDIM (TT ph3 106→11 ms; ST dim-cache **~99% hit**). **Incremental scroll** then
    halved PREDIM's static rebuild (ph0 598→282 ms flat / 377 ms dither) by re-dimming only the edge +
    light-changed cells, taking the walking frame from PREDIM's 0.65 fps to **0.82 fps (flat) / 0.76 fps
    (dither)** — a **5.5× compose speedup over the SMOOTH baseline** (3025→548 ms). After that the
    **c2p (678 ms) is now the single dominant term** — next lever is Blitter c2p (the bench's 678 ms is
    a FULL rect present; real walking uses `rdr_scroll_left16` ≈ 1/20). Outdoor daytime (uniform) hits
    the cheap `lut[]` path regardless.
  - **Visual — with a correction.** A render-path bug (the `LIGHTSTY` hook ran AFTER `compose_map`,
    and only accepted '0'..'2') meant EARLIER style shots were all SMOOTH — do not trust any lighting
    A/B image dated before that fix. Fixed (hook runs before compose, accepts '0'..'3'); proved by
    diffing (styles now differ by tens of millions of px, not zero). Verified on the night throne room
    (Falcon 256-colour): **DITHER** genuinely dithers (stippled shadows), full composite. **PREDIM
    dim-at-draw** is now a **full composite with actors/avatar**, dimmed correctly; SMOOTH is
    byte-identical (no regression, enginetest ALL PASS). PREDIM's only visual gap vs SMOOTH is
    **tile-granularity** (per-cell, no sub-tile gradient) — close the gap by combining with DITHER
    (dither the pre-dim level selection, or ship dithered variants).
  - Repro: build a night pack (`START_MIN=0 make world-svm` / `gamedata-st`), stage `SHOT.REQ` +
    `LIGHTSTY.REQ`, boot gamef.prg (Falcon) or gamest.prg (ST), `shot2png` the dump.
