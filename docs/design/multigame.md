# M9 — Multi-game (Savage Empire + Martian Dreams) design & roadmap

Goal (milestone M9): the SAME engine binary runs U6, Savage Empire (SE) and Martian Dreams (MD)
from per-game data + small RuleSet deltas — NOT engine forks. This doc is the M9 plan; per-game
format/rule specs land in /docs/formats and /docs/rules as they're decoded.

## Kickoff evidence (2026-07-18) — the thesis holds
SE and MD are built on the U6 engine, so the CORE world formats are byte-identical in structure.
Proven with the EXISTING U6 decoders, zero engine change:
- `MAP` (32256 B) and `CHUNKS` (65536 B) are the same size in all three games → same 1024×1024
  toroidal surface, same 8×8-superchunk-of-16×16-chunk layout.
- `tools/mapdump` (palette filename parameterized) decoded **both SE and MD** worlds: "MAP+CHUNKS
  load (size layout matches)" PASS, "all surface chunk indices < 1024" PASS. The SE render is a
  recognizable Eodon valley (jungle + river + a central settlement); MD renders too.
- Shared verbatim: `MAP`, `CHUNKS`, `TILEINDX.VGA`, `MAPTILES.VGA`, `LZOBJBLK`, `TILEFLAG`,
  `BASETILE`, `ANIMDATA`, `MASKTYPE.VGA`, `SCHEDULE` (all present + same layout in SE/MD).

## Format landscape — shared vs delta
| Concern            | U6                       | SE / MD delta                                             | Work |
|--------------------|--------------------------|----------------------------------------------------------|------|
| World map/chunks   | `MAP`+`CHUNKS`           | identical (verified)                                     | none |
| Tile graphics      | `MAPTILES.VGA` (LZW)     | present, but **tile COUNT differs** (U6=2048 hardcoded → the mapdump size-check fails) | parameterize tile count |
| Tile index/flags   | `TILEINDX/TILEFLAG/BASETILE/ANIMDATA` | present, same layout                        | verify |
| Objects            | `LZOBJBLK`               | present, same container                                  | verify record layout |
| Palette            | `U6PAL` (256 RGB6)       | SE `SEPAL`/`SAVAGE.PAL` (same RGB6 triples, larger); **MD has no 768-B game palette** (title/scene PALs only — palette likely embedded / in an `.LZC`) | name it (SE); locate MD's |
| Conversation       | `CONVERSE.A/.B` (library)| `TALK.LZC`                                              | decode `.LZC` |
| Look/names, books  | `LOOK.LZD`, `BOOK.DAT`   | `LOOK.LZC`; text in `SCENETXT.LZC`/`MDTEXT.LZC`          | decode `.LZC` |
| Faces/portraits    | `PORTRAIT.A/B`           | `FACES.LZC`/`MDFACES.LZC`                                | decode `.LZC` |
| Fonts              | `U6.CH`                  | `SAVAGE.FNT` / `FONTS.LZC`                               | per-game font |
| Object meanings    | obj_n literals (gold 88, spellbook 57, key 64, …) | SE/MD renumber these                     | obj_n table |
| Rules              | magic (79 spells), reagents | SE tribal skills; MD era/tech mechanics             | RuleSet swap |
| Level policy       | surface + 5 dungeon levels | SE uses 4 of 6 (level 6 = gargoyle-world variant); MD toroidal | GameDef policy |

**`.LZC` = the main new format.** SE/MD replace U6's library+`.LZD` files with `.LZC` containers
(TALK/LOOK/FACES/MDTEXT/…). Headers differ from U6 LZW (`TALK.LZC` begins `10 74 02 00`, an
index/size prefix). Decode it once (index of per-item LZW blobs) and most SE/MD text/faces unlock.

## Engine parameterization (from the M9 hardcoding survey)
Today the engine is single-title U6: it loads ONE monolithic pack gated on magic `"U6WT"`
(`render/gamedemo.c:4327`), with a fixed inline section layout; `GameDef.id` is never read; map
geometry, tile/actor counts, and every obj_n meaning are compile-time constants. Highest-leverage
unblocks (most foundational first):

1. **GameDef = identity + asset manifest.** Replace the `"U6WT"` magic + `PACK_NAME` literal with a
   GameDef-driven id/pack; add an asset-manifest field so `mkworld` emits SE/MD packs and the engine
   accepts them. Literal blocker for "SE boots from the same binary."
2. **World geometry & level policy as DATA.** Promote `U6_SURFACE_TILES/CHUNKS`, `U6_DUNGEON_LEVELS`,
   the superchunk layout, and the wrap mask (`u6map.h`/`u6map.c`, mirrored in `world.c`) into runtime
   fields; add the missing **level-mapping policy** to GameDef (SE 4-of-6 + level-6 gargoyle variant).
3. **Buffer sizing off `2048`/`1024`/`256`.** The pack header already carries `ntiles/nobjs`; make
   the fixed `[2048]`/`[1024]`/`U6_MAX_ACTORS` arrays GameDef-/header-driven (or max-of-games).
4. **Well-known object table.** Move the scattered obj_n literals (gold 88, spellbook 57, spell 58,
   key 64, lockpick 63, reagents 65-72, LB 409, containers, doors) into a per-game table on
   GameDef/RuleSet. Unblocks get/use/save without a fork.
5. **Rule/skill subsystem behind RuleSet.** Add the missing hooks (`on_use_item`, `on_combat_hit`,
   `on_rest`, `area_group`, …) and gate U6 magic vs SE tribal-skills vs MD era-tech behind them.
6. **Savegame/OBJLIST layout** parameterized (the U6 `0x100/0x400/0x600` offsets differ per game).
7. **Start/clock policy** into GameDef (drop the baked `315,367` / `12*60` noon defaults).
8. **Front-end/intro/string extraction** per-game via the manifest (lowest priority for "boots").

## Roadmap (phases)
- **P0 — Foundation (items 1-3).** GameDef{id, manifest, geometry, level-policy}; replace the pack
  magic; make world geometry + buffer sizes data-driven. Acceptance sub-goal: the U6 build still
  passes unchanged AND the same binary *accepts* a non-U6 pack header.
- **P1 — SE data path.** `mkworld` grows a game arg + SE manifest (palette=SEPAL, tile-count fix,
  `.LZC` for talk/look/faces). Decode `.LZC` → docs/formats/lzc.md. Host-validate the SE world +
  tiles + objects render (mapdump/objrender parity, like U6's M2/M3 golden dumps).
- **P2 — SE vertical slice.** SE boots from the shared binary: walk its map, objects/NPCs draw,
  look/get/use work via the obj_n table. SE RuleSet: 4-of-6 levels + gargoyle-variant, tribal skills
  stub. Deltas → docs/rules.
- **P3 — MD data path + vertical slice.** Locate MD's game palette; MD manifest; era-mechanics
  RuleSet; MD boots + walks + objects.
- **P4 — Acceptance.** All three boot from ONE binary; a vertical slice of each runs; all per-game
  behaviour lives in RuleSet overrides / GameDef data, not engine branches.

## Per-game deltas (known so far)
- **Savage Empire:** Eodon (Amazon-valley) world; 4 of 6 levels, level 6 = gargoyle-world variant;
  tribal skills replace U6 magic; palette `SEPAL` (RGB6, verified compatible). `SE.1..SE.8` = saves.
- **Martian Dreams:** Mars; toroidal world; era/technology mechanics; far more `.LZC` scripted
  content (SCENE*/MIDGAME*/MARS); game palette location TBD (no `U6PAL`-equivalent file).

## Status
KICKED OFF (2026-07-18). Thesis validated: SE+MD world map/chunks decode with the unmodified U6
decoders (host, mapdump). Plan set; P0 (GameDef + manifest + geometry-as-data) is next. Depends on
M6/M8; per-game specs land in /docs/formats + /docs/rules as decoded (`.LZC` first).

## Cross-links
- `engine/ruleset.h` (GameDef/RuleSet), `dataformats/u6map.h` (geometry #defines to promote),
  `render/gamedemo.c` (pack loader), `tools/mkworld.c` (host packer), `tools/CLAUDE.md` (manifest).
- `docs/formats/*` (shared formats — verify SE/MD deltas), `docs/rules/*` (per-game rule deltas).
