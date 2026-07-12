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
