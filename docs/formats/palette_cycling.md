# Rule/Format: Palette colour cycling (CLUT rotation)

Games: U6 (SE/MD have their own ranges — verify)
Implemented by: /render (gamedemo cycle_palette + rdr_set_palette)
Tracked in: /docs/INDEX.md  ·  Milestone: M6/M7

> Provenance
> - Derived from : OBSERVING ScummVM render the throne room over time (tools/
>   animobserve) and mapping the pixels that change colour (but not shape) back to
>   u6pal indices — they rotate within a contiguous palette ramp. The u6pal ramps
>   were read from the pack palette.
> - Tools        : tools/animobserve (ScummVM frame capture), a host pixel→index
>   mapper (inline python), tools/cycleprobe (ruled OUT ANIMMASK as the source).
> - Validated by  : on the TT, rotating range 224–231 makes the brazier flames (the
>   only 224–231 pixels in the throne room) shimmer with NO tile redraw
>   (build/cyc2_diff.png) — confirmed even with frame-animation disabled.
> - Confidence   : confirmed (range 224–231 + the mechanism); other ranges probable.
> - Last updated : 2026-06-25

## Mechanism
U6 animates fire / glow / magic by **rotating contiguous palette-index ramps** in the
CLUT each tick. Any pixel whose index is in a cycling range changes colour as the
ramp rotates — **no framebuffer change**. On the TT this is just re-uploading the
palette registers (XBIOS EsetPalette via rdr_set_palette); the shifter shows the new
colours on the next scanline. It is therefore very cheap (no recompose, no c2p) —
the opposite end of the cost spectrum from tile/frame animation.

Rotation (confirmed direction): each tick, the colour shown at index `lo+k` advances
to the next ramp colour: `CLUT[lo+k] = u6pal[lo + ((k + phase) % len)]`, `phase++`.

## NOT the source: ANIMMASK
ANIMMASK.VGA is LZW → a 2048-byte per-TILE map (values 1–15 + a few outliers). It is
NOT a colour-cycle range table: per /tools/cycleprobe, a group's tiles span the whole
palette, not a tight ramp. Its real purpose is unconfirmed (probably "which tiles
contain animated pixels" — needed only by a redraw-based, non-CLUT renderer). The
cycling RANGES below are independent of it (they are CLUT index ranges, hardcoded
U6 game knowledge).

## Which ranges cycle — the test
A cycle range is a **narrow** contiguous ramp used by **few, special tiles** (fire,
magic, energy). A wide ramp used by *hundreds* of tiles is just general shading —
cycling it would shimmer the whole screen. Tile-usage counts (tiles with >8 px in the
range) settle each candidate:

| range    | u6pal colours          | #tiles | verdict |
|----------|------------------------|--------|---------|
| 224–231  | red→orange→yellow (8)  | 26     | **CYCLE — CONFIRMED** (throne-room braziers rotate in ScummVM, 82 transitions) |
| 232–239  | blue ramp (8)          | 37     | candidate, **UNCONFIRMED** — used by force field, moongate, red gate, lightning wand, moonstone, magic items (all glow in U6), but observation FAILED (see below). NOT shipped. |
| 240–247  | pink/brown (mixed)     | 7      | no — not a clean ramp |
| 32–47    | yellow→red (16)        | 251    | NO — general red/orange scenery (would corrupt) |
| 48–63    | white→yellow→red (16)  | 418    | NO — general |
| 116–127  | light→dark blue (12)   | 229    | NO — general blue (sky/water); surface water is FRAME-animated (animdata.md), not colour-cycled |
| 248–255  | green/white/black      | 1592   | NO — general |

So we cycle **224–231 only** (fire, confirmed). The wide ramps (32–47, 48–63,
116–127, 248–255) look like ramps but are general shading — rejected by the usage
test (and water is frame-animated). 232–239 is a strong candidate but is held back
until confirmed (see below). New ranges are enabled only after the usage test AND an
observation.

### Observation method (clean-room) + why 232–239 is unconfirmed
Confirmed 224–231 by writing a ScummVM save (tools/nuviewrite), loading it, capturing
frames over time (tools/cycleobserve), and mapping the pixels that change COLOUR (not
shape) back to u6pal indices — they rotate within 224–231 (and our own TT render
shows the braziers shimmer with frame-anim off).

To confirm 232–239 we tried to put a magic/energy object in view by save-editing
(nuviewrite `--set-obj-at`: relocate the avatar to water; inject a gem / magic shield
/ moongate into the throne room). **All failed to render observably**: ScummVM keeps
the avatar out of deep water, and it does NOT render hand-injected surface objects
(a moongate tile has 99 blue px in our tileset, yet 0 appear on the loaded screen —
ScummVM appears to validate/relocate the object list on load; note `--move-avatar`,
an *objlist* edit, DOES take effect). So 232–239 can only be confirmed by reaching a
NATURAL moongate / force-field / shrine during real gameplay — deferred to that.

## Relationship to the other animation systems
- ANIMDATA (animdata.md): swaps a blank tile slot through frame source tiles — water,
  fountains. (Tile data changes.)
- Object frame_n (object_anim.md): `frame_n` is STATE/ORIENTATION/type, NOT animation
  — we do NOT auto-cycle it. Fire objects (brazier/fireplace) glow via THIS colour
  cycling instead, which also fixed the "rotating fireplace" bug.
- Colour cycling (this doc): rotates CLUT ramps — fire/glow. Cheapest of the three.

## Per-game deltas
SE/MD: own palette + cycle ranges (verify).

## Status
VERIFIED + SHIPPED for range 224–231 (fire) on the TT. Mechanism (CLUT rotation, no
redraw) is the model. 232–239 (magic/energy blue) is a strong candidate but is NOT
shipped — it couldn't be confirmed by observation (ScummVM won't render save-injected
objects); enable it once seen cycling at a real moongate/force-field in gameplay. The
other gradient ramps are general shading (rejected by the usage test).
