# Rule: Object frame animation (cycling an object's frame_n)

System: rendering / world animation (U6; SE/MD need their own sets)
Implemented by: /tools (mkworld objanim table), /render (gamedemo obj_tile)
Tracked in: /docs/INDEX.md  ·  Milestone: M6

> Provenance
> - Derived from : rendering each candidate object's frames from a built pack
>   (tools/tilestrip) + OBSERVING ScummVM animate the throne-room braziers over
>   time (tools/animobserve frame diffs). Object names from the shipped LOOK table.
> - Tools        : tools/tilestrip (frame render), tools/animobserve (ScummVM diff).
> - Validated by  : on the TT, the throne-room braziers cycle (build/braz_diff.png
>   shows the two brazier tiles changing, nothing else); fireplaces no longer rotate.
> - Confidence   : confirmed (brazier animates); the *set* is deliberately minimal.
> - Last updated : 2026-06-25

## The key fact (why this is subtle)
Distinct from ANIMDATA (animdata.md), which fills/cycles blank TERRAIN tile slots,
some OBJECTS animate by cycling their own `frame_n` (tile = BASETILE[obj_n] + frame).

But **`frame_n` in U6 usually encodes STATE or ORIENTATION, not an animation
sequence**. Verified by rendering frames:
- **fireplace** (obj 164): its 4 frames face 4 different walls → cycling them makes
  it "rotate" (a real bug we hit).
- **campfire** (253), **torch** (90): frames are lit/unlit → cycling blinks on/off.
- **fire** (201): frame variants (size/type), not a clean loop.
- **brazier** (206): 4 frames are flame colours (gray/orange/blue/purple) — a genuine
  animation; ScummVM cycles the throne-room braziers.

⇒ A "fire/flame name" heuristic is WRONG (it flags fireplaces etc.). There is no
"animated" bit in TILEFLAG / ANIMMASK / ANIMDATA for this. The animated-object set
is **U6 game logic**, reconstructed by OBSERVATION, one object at a time.

(Aside: ANIMMASK.VGA is LZW → a 2048-byte per-tile mask of COLOUR-CYCLE groups —
a third, separate animation mechanism, not object-frame animation. See open
questions.)

## The set (U6) — extend only after verifying frames are an animation
| obj_n | name    | frames | animation                          |
|-------|---------|--------|------------------------------------|
| 206   | brazier | 4      | flame colour cycle (gray/orange/blue/purple) |

## Engine model
- mkworld ships `objanim[1024]` in the pack tail (U6WE): per-obj_n frame count,
  0 = static (only the verified set is nonzero).
- render: `obj_tile(o)` returns `BASETILE[obj_n] + ((anim_ctr + x + y) % frames)` for
  objanim objects (the `+x+y` desyncs neighbours so braziers flicker independently);
  all other objects render their stored tile unchanged.
- The animation timer (gamedemo) advances `anim_ctr` ~6 fps and recomposes the map.

## Per-game deltas
SE/MD have their own animated objects (different obj_n) — TODO when those games come.

## Open questions
- The full U6 animated-object set + correct frame RANGE per object (some may animate
  only a subset of frames, e.g. skip an "off"/orientation frame).
- ANIMMASK.VGA colour-cycle groups → palette-cycling (palette_cycling.md): would make
  fire/water/lava shimmer via the CLUT, likely the more authentic fire look.

## Status
IN PROGRESS — framework done + brazier verified; the animated-object set is minimal
on purpose (over-inclusion rotates/blinks the wrong objects). Color-cycling (ANIMMASK)
is the likely next mechanism for fire/water glow.
