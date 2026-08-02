# Map levels (surface + dungeon levels) — the world's Z dimension

System: world geography levels. Games: U6 / Savage Empire / Martian Dreams (per-game level *policy*
differs; the storage layout is shared). Implemented by: `dataformats/u6map.c` (`u6map_dungeon_tile`),
`engine/world.c` (`world_t.level`, `world_tile`), `render/gamedemo.c` (compose reads `world_tile`).

> Provenance
> - Derived from : the MAP file layout (docs/formats/map_chunks.md) + rendering each candidate
>                  dungeon-level chunk ordering with `tools/mapdump` and comparing coherence.
> - References   : community "Ultima 6 map format" notes + Nuvie's map/level model — READ to
>                  understand the level concept; not transcribed.
> - Tools        : mapdump (dungeon1_sc/flat.png), the in-game DUNGEON.REQ shot hook.
> - Validated by : mapdump renders a coherent dungeon (intact maze + symmetric cross-room) only in the
>                  FLAT ordering; the engine renders a coherent dungeon ROOM in-game at level 1 (100,100)
>                  — stone walls, brick floor, corridor exits, correct line-of-sight.
> - Confidence   : geography = confirmed; transitions/lighting/objects = TODO (see below).
> - Last updated : 2026-08-01

## 1. Level model
The world has a Z dimension = the map **level**:
- **level 0 = surface**: 1024×1024 tiles, 128×128 chunks stored **superchunk-blocked** (8×8 superchunks
  of 16×16 chunks, map_chunks.md §3), toroidally wrapping on both axes.
- **levels 1..5 = dungeon levels**: each 256×256 tiles = 32×32 chunks, stored **FLAT 32×32 row-major**
  (`file_index = cy*32 + cx`) — NOT superchunk-blocked. (The superchunk scheme is a surface-only
  artifact of U6's map editor.) The 5 levels are packed after the surface in MAP, 1536 bytes each.

### Ordering — how we know
Rendered dungeon level 1 both ways (mapdump): the **flat** order produces a coherent dungeon (a maze
with continuous corridors + a symmetric cross-shaped room); the superchunk order fragments it into 2×2
quadrants and breaks the corridors. So dungeon levels are flat. (`u6map_dungeon_tile`.)

### Extent / wrap
Dungeon levels are bounded 256×256 with solid-rock borders (the level data fills its own edges with
rock), so bounded-vs-wrap is invisible in practice; our accessor wraps `&255` for safety. The engine
keeps a dungeon position in 0..255. (Surface stays 1024-wrapped.)

## 2. Engine representation
`world_t.level` (0 surface, 1..5 dungeon). `world_tile(w,x,y)` branches: level 0 → `u6map_surface_tile`,
else → `u6map_dungeon_tile(level-1)`. Since the whole compose/lighting/LOS path already reads
`world_tile`, setting `w.level` renders the level for free. `world_is_passable` uses the shared per-tile
`passable[]`; the surface object-blocking bitmap is gated to level 0 (dungeon objects are a follow-up).
`world_init` starts on the surface (level 0), so U6/SE surface behaviour is byte-identical.

## 3. Per-game deltas (level policy — GameDef, mostly TODO)
- **U6**: 5 dungeon levels forming a nested 3D structure — surface cave/hole/ladder entrances line up
  with ladders between adjacent levels (down at (x,y,z) ≈ up at (x,y,z+1)). GameDef `dungeon_levels=5`.
- **Savage Empire**: uses 4 of 6 levels; level 6 = a gargoyle-world variant. Level meaning differs; TBD.
- **Martian Dreams**: toroidal world; era mechanics; TBD.

## 4. NOT YET (follow-ups, tracked here)
- **Transitions**: stairs/ladders/holes between levels, and surface dungeon entrances. Needs the
  transition-object semantics (which obj_n move which direction, and how positions map between levels).
  Until then, levels are reachable only via the DUNGEON.REQ debug hook.
- **Dungeon lighting**: dungeons should be DARK (torch-lit) regardless of surface time; currently
  `compute_light` uses the surface day/night ambient (so a daytime level renders lit where visible).
- **Dungeon objects**: per-level object blocking + object rendering (the objblk/save objects carry z).
- **Save**: dungeon-level chunks are preserved byte-wise on save (save.c) but the player level isn't
  persisted yet.

## 5. Test hooks
- `tools/mapdump` → dungeon1_sc.png (== flat, coherent). `DUNGEON.REQ "<level> [x] [y]"` drops the
  in-game view onto a dungeon level for a shot (built dg_1_100_100.png = a coherent level-1 room).

## 6. Status
IN PROGRESS. Geography (decode + level model + in-game render) VERIFIED. Transitions, dungeon-dark
lighting, per-level objects, and level persistence are TODO.
