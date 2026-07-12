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
