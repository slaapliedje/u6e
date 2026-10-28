# Format: Tile animation table (ANIMDATA)

Games: U6 / Savage / Martian — verify per game (U6 decoded here)
Implemented by: /tools (animprobe, mkworld), /render (gamedemo anim tick)
Tracked in: /docs/INDEX.md  ·  Milestone: M6

> Provenance
> - Derived from : refdata/dos/u6/ANIMDATA (194 bytes) — our own byte analysis
>                  (tools/animprobe) + rendering the referenced tiles from a built
>                  world pack (tools/tilestrip), + OBSERVING ScummVM animate the
>                  same scene over time (tools/animobserve, frame diffs).
> - References   : community knowledge that Nuvie animates these tiles (consulted
>                  for orientation only; the layout + semantics here are from our
>                  bytes and from what ScummVM visibly does).
> - Tools        : tools/animprobe (decode), tools/tilestrip (render frames),
>                  tools/animobserve (ScummVM ground-truth frame diffs).
> - Validated by  : the B[] source tiles render as coherent animation sequences —
>                  14 frames of water shimmer (448..461), a fountain, a sparkling
>                  wand, pulsing gems, rotating shields; ScummVM cycles the matching
>                  on-screen tiles (braziers/fire diff every frame).
> - Confidence   : confirmed (container + dest/src/frame-count); probable (D = speed)
> - Last updated : 2026-06-25

## 1. What it is
ANIMDATA lists the tiles that ANIMATE and where each one's frames come from. The
KEY fact: an animated tile's own slot in the static tile set is **blank** (all
pixels = transparent index 255). At runtime the engine fills that slot from a
sequence of SOURCE tiles, cycling once per animation tick. So an engine that does
not animate draws these tiles as **holes** (terrain shows through) — water,
fountains, fields, fire, etc. go missing.

## 2. Container (194 bytes, little-endian)
Fixed 32-entry parallel arrays; `count` says how many are active.

| Offset | Size      | Field                                                      |
|--------|-----------|------------------------------------------------------------|
| 0x000  | u16       | **count** = number of active animated tiles (U6 = 29)      |
| 0x002  | 32 × u16  | **tile[]**  — the animated tile slot (blank in the tileset)|
| 0x042  | 32 × u16  | **src[]**   — base SOURCE tile of this slot's frame run     |
| 0x082  | 32 × u8   | **frames[]** — number of frames (source tiles src..src+f-1)|
| 0x0A2  | 32 × u8   | **speed[]**  — frame-advance rate / phase (probable)        |

Entries `count..31` are stale padding (ignore).

## 3. Animation rule
For active entry i, the on-map slot `tile[i]` displays, at animation step `k`:
```
shown_tile = src[i] + (k % frames[i])
```
i.e. the engine substitutes `tile[i]` → `src[i] + (k % frames[i])` wherever that
tile is drawn (terrain, object, anything). `k` advances on the game's animation
timer (a few fps). `speed[]` (D) most likely scales the per-tile advance / sets a
phase; we currently advance all entries uniformly and treat it as a refinement.

### Examples (U6, from our decode + render)
| slot tile[] | src[] | frames | what it is                 |
|-------------|-------|--------|----------------------------|
| 8..15       | 448 (+8/slot) | 14 | water shimmer (8 map water slots) |
| 1016..1019  | 336/338/340/342 | 2 each | a multi-tile object (2-frame each) |
| 1020        | 310   | 4      | (4-frame)                  |
| 1008..1011  | 272.. | 6/6/3/6| pulsing gems / rotating discs |

So a multi-tile object animates as several adjacent slots, each with its own
2-frame (etc.) source run — not one shared cycle.

## 3b. Time-of-day tile: the sundial (U6)

One substitution is driven by the game CLOCK, not the frame counter: the sundial's object tile
**861** is drawn as one of eight shadow tiles by the hour — 5-6 → 328, 7-8 → 329, 9-10 → 330,
11-12 → 331, 13-14 → 332, 15-16 → 333, 17-18 → 334, 19-20 → 335 — and as 861 itself (the dial
without a shadow) from 21:00 to 4:59. The reference re-points its tile index entry 861 whenever
the hour changes; we substitute at draw time in `obj_tile` (the object keeps tile 861 for its
flags and in the save). Tiles 328..335 carry no tile flags, 861 is Impassable only — so the
substitution never changes passability, foreground order or light. Rule in
docs/rules/usecode_use.md (Clock and sundial).

**Multi-tile objects whose anchor is an animated slot** (the clock: object 159 frame 1 = tile 699,
double-height, pendulum frames 298/299; slot 903, double-width): the SIZE flags and the extra cells'
tile numbers come from the object's own tile (699 → face 698 above), never from the resolved frame —
the frames carry no size flags. Until 2026-10-27 the engine read the size from the resolved frame and
drew the clock as a lone pendulum with no face (fixed in draw_multi). The foreground bucket index
(`anchor_family_fg`) had the same shape of bug: it tested a height-only object's top at anchor−2 instead of
anchor−1 (the decrements are cumulative: width first, then height, then the corner), so the foreground
face never joined the foreground pass either — fixed the same day. Verified against the ScummVM throne-room
screenshot: the clock is a gold face above a white-and-blue swinging pendulum.

## 4. Per-game deltas
Verify SE/MD (own ANIMDATA; different animated tiles).

## 5. Validation method
`tools/animprobe ANIMDATA` dumps the arrays; `tools/tilestrip` renders any
src run (frames are visually coherent sequences); `tools/animobserve` confirms
ScummVM cycles the matching on-screen tiles. On-target: mkworld ships the table,
the engine fills the slots (missing tiles return) and cycles them.

## 6. Open questions
- D (speed[]) exact semantics: ticks-per-frame vs starting phase vs per-tile rate.
- Whether animation advances on the world clock vs a fixed wall-clock fps in U6.
- Relationship to color-cycling (water also color-cycles in some U6 builds?) —
  here water is FRAME-animated (14 source tiles), so frame animation may subsume it.

## 7. Status
VERIFIED (container + dest/src/frame-count + the rule); D (speed) probable.
On-target: mkworld ships the table (U6WD pack tail); the engine substitutes
`tile[i] -> src[i] + (ctr % frames[i])` at every blit site and advances `ctr` on a
~6fps timer. Demonstrated on the TT in open water (`make m6-anim`): two frames
differ by ~75k pixels across the ocean (build/anim_engine.png), and the previously
BLANK water slots now render filled — fixing the "missing chunks". NOTE: this is the
ANIMDATA system (water/fountains/fields); some objects (e.g. braziers) instead
animate by cycling the OBJECT's own frame_n — a separate mechanism, still TODO.
