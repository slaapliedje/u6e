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
4. **Well-known object table.** 🟡 STARTED (2026-07-29). `GameDef.wko` (`WellKnownObjs` in
   `engine/ruleset.h`) now holds the per-game obj_n of the objects SHARED systems reference by
   IDENTITY: `avatar, key, lockpick, spellbook, reagent_base`. U6 = {410,64,63,57,65}; SE =
   {310,0,…} (Avatar 310 from the captured objlist); MD = all 0. 0 = "no such object" (shared code
   treats it as no-match). `gamedemo.c` reads `g_wko` (set from the loaded GameDef) at: the view-centre
   Avatar draw (was hard-coded 410 → now correct for SE), the USE key/lockpick unlock, and the
   `magic_init` reagent/spellbook seeding. **Side-effect fix:** `magic_init` runs before the `is_core`
   gate, so SE/MD previously got a spurious U6 spellbook + 8 reagents in inventory; the sentinel guards
   now skip that for non-U6 games. U6 stays BYTE-IDENTICAL (wko == the old literals; enginetest ALL
   PASS, USEFX lockpick 9→5, TT render unchanged). Still to move (as their subsystems go cross-game,
   or behind an `on_use` hook): the deep container/door/actuator/field obj_n inside `use_object_inner`
   (U6 usecode), gold's converse-VM pool, LB/containers. SE/MD real obj_n await an SE/MD obj_n survey.
5. **Rule/skill subsystem behind RuleSet.** 🟡 SEAM STARTED (2026-07-29). `gamedemo.c` now holds
   `g_gd` — the loaded GameDef pointer, the authoritative game identity (id/geometry/wko/rules), set at
   pack-load — and per-game GAME-LAYER seams branch on it. First seam: **USE** — `use_object` dispatches
   on g_gd: U6 → `use_object_inner` (U6 usecode); **SE → `se_use_object` (real SE USE)**; MD → safe
   no-op until surveyed. This stopped SE/MD misfiring U6 semantics on renumbered objects (e.g. toggling
   passability on a non-door). **SE USE v1 DONE (2026-07-29):** containers (183/184/188/132/133) SEARCH
   /spill via the shared `spill_container`; food (103/110/197/114) EAT + feed — keyed on the surveyed SE
   obj_n (se_md_objn.md), validated by the `SEUSE.BIN` self-test (food 2→1→gone; container child spills).
   **SE DOORS DONE (2026-07-29):** since SE door frames are per-object with no uniform delta, the toggle is
   DATA-DRIVEN by passability — open=passable / closed=impassable tileflags1, now shipped to the core pack
   via a new **CTF1** section; USE flips to the nearest opposite-passability door-named tile. CTF1 also
   gives SE/MD **object passability in general** (walls/furniture/closed doors block — was entirely absent;
   Avatar keeps 7/8 walkable neighbours, not boxed in). Validated: `SEUSE.BIN` door 977(closed)→USE→976(open).
   NOTE ON ARCHITECTURE: use/talk logic is gamedemo-coupled
   (msgcat/blocking/UI), so those seams live in the game layer keyed on `g_gd`, NOT the engine `RuleSet`
   (which stays for engine-clean rules like `move_turn_cost`, wired via `world_init`→`w->rules`). Engine
   behavioural hooks (on_combat_hit, on_rest) are added when a game provides a real override — not
   speculatively (engine/CLAUDE.md). Still to do: SE doors (frame table); SE/MD magic-vs-skills gating.
6. **Savegame/OBJLIST layout** parameterized (the U6 `0x100/0x400/0x600` offsets differ per game).
7. **Start/clock policy** — ✅ ALREADY DATA. Start (`hdr[20/22]`) + game-time (`hdr[24]`, U6WH) come
   from the PACK HEADER (mkworld writes them per game: SE 380,555; MD 124,168), so this is per-game
   without engine changes. `GameDef.start_x/y` is a now-redundant fallback (the header is the source).
8. **Front-end/intro/string extraction** per-game via the manifest (lowest priority for "boots").

## Roadmap (phases)
- **P0 — Foundation (items 1-3).** GameDef{id, manifest, geometry, level-policy}; replace the pack
  magic; make world geometry + buffer sizes data-driven. Acceptance sub-goal: the U6 build still
  passes unchanged AND the same binary *accepts* a non-U6 pack header.
- **P1 — SE data path.** ✅ DONE (2026-07-18). `.LZC` decoded → docs/formats/lzc.md (+ `u6lzc`,
  `make lzctest`: all SE+MD `.LZC` verified). `mkworld` grew `$MKWORLD_GAME` (u6|savage|martian) + a
  `GameProfile` + a self-contained **core-pack emitter** (MAP/CHUNKS/tileset/palette/passable/objects)
  reusing the U6 loaders; SE/MD wrap the compressible core files (MAPTILES/MASKTYPE/LZOBJBLK) in a
  single-item `.LZC` that `load_core_asset` unwraps. `make world-se`/`world-md` emit `SE.PAK`/`MD.PAK`
  (SE 4844 objs, MD 7600 objs, both 2048 tiles). Host-validated: `make coredump-se`/`coredump-md`
  render the densest object cluster — SE shows a recognizable tribal village (huts/campfire/pottery),
  MD the red Mars terrain + era objects. U6 full-pack path unchanged; enginetest ALL PASS.
  Remaining for P2: `.LZC` TALK/LOOK/FACES *content* (dialogue/look-names/portraits) into the pack.
- **P2 — SE vertical slice.** 🟡 STARTED (2026-07-18): **SE boots from the shared binary.**
  `gamedef_se`/`gamedef_md` registered (magics `SEWT`/`MDWT`); the pack loader detects a CORE pack
  structurally (nothing follows the objects) and stubs the U6-only tail (font/paper/portraits/actors/
  verbs/anim/lighting/save) so the real render pipeline runs on SE's terrain + objects. `make shot-se`
  boots the Falcon binary on `SE.PAK` and pixel-dumps the Kurak-valley village (huts/pottery/food in
  SEPAL) — verified. U6 full-pack path unchanged (throne-room boot re-verified; enginetest ALL PASS).
  **Object identity DONE**: SE/MD `LOOK.LZC` (same run-fill table as U6 `LOOK.LZD`, via `u6lzc`) +
  gettable fold into the core pack's optional magic-guarded **`CLOK`** section; the loader parses it
  (magic-based `is_core = gd != gamedef_u6`, so the core pack can grow). `tools/coredump` labels the
  rendered SE village with authentic names (atl atl / obsidian knife / totem of Aphazz / roast
  pteranodon). **SE font DONE**: cracked the WOU proportional format (`SAVAGE.FNT` = `.LZC`→LZW→
  `[height][transp][widths 256][split lo/hi offset table][row-major glyphs, ink = byte≠transp]`;
  docs/formats/fonts.md), converted to U6.CH 8×8 in a `CFNT` core-pack section → `g_font`; "The Savage
  Empire" renders on the Falcon panel in the game's own font. **FACES portraits DONE**: each
  `FACES.LZC` item is a plain U6 shape (79×85) — `u6shape_decode` handles it unchanged; `mkworld`
  ships every present face in a `CFAC` core-pack section and the panel draws one — the SE Avatar's
  portrait renders on the Falcon (docs/formats/portraits.md). **NPCs DONE (2026-07-19)**: SE ships no
  OBJLIST, so the 167-actor roster is CAPTURED from a ScummVM new-game save (nested-Xephyr harness →
  `tools/nuviesave`; see memory se-actor-capture) and shipped as a new optional **`CACT`** core-pack
  section. `make world-se-svm` extracts the objlist then `mkworld` emits CACT; `make shot-se-svm`
  boots the Kurak village **populated with tribespeople + the party** on the Falcon (A/B vs `shot-se`
  confirms the added actors; U6 unchanged — enginetest ALL PASS, U6 throne-room boot correct).
  **FACES/TALK→actor mapping DONE (2026-07-19)**: `face(actor N) = FACES slot N−1`; TALK.LZC items
  self-describe their actor in byte 1 (converse.md §6b). `convert_faces` is now SLOT-indexed (all 92
  slots padded to 79×86 → `CFAC`) and `convert_talk` ships per-actor name+look-description (`CTLK`).
  `gamedemo.c::se_talk` shows the NPC's own portrait + name + "You see …" on talk — verified on-target
  (actor 4 → Jimmy's fedora portrait + "a lanky, sharp-eyed man in his mid-20s"). A `TALKTEST.REQ`
  debug hook boots straight into a chosen NPC's panel. The usecode branching dialogue (after `0xF2`)
  is the next milestone. Remaining P2: SE RuleSet (4-of-6 levels + gargoyle variant, tribal-skill
  stub). Deltas → docs/rules.

  **`CACT` section format** (optional, magic-guarded, after CFAC):
  `[4 "CACT"][u16 nact][nact × u16 tile][nact × {u16 x,u16 y,u8 z}][nact × {u8 hp,str,dex,int}][u8 nparty][nparty × u8 idx]`.
  Appearance `obj_n+frame` is resolved to an ABSOLUTE tile at build time (the core loader stubs
  BASETILE). The avatar (actor 1) is planted at the demo start; other party members ship `z=0xff` so
  the engine spawns them beside the avatar (party-follow). Clean-room-pure follow-up: find the objlist
  init template in `GAME.EXE` so the ship pipeline needs no ScummVM.
- **P3 — MD data path + vertical slice.** Locate MD's game palette; MD manifest; era-mechanics
  RuleSet; MD boots + walks + objects.
- **P4 — Acceptance.** All three boot from ONE binary; a vertical slice of each runs; all per-game
  behaviour lives in RuleSet overrides / GameDef data, not engine branches.

## Per-game deltas (known so far)
- **Savage Empire:** Eodon (Amazon-valley) world; 4 of 6 levels, level 6 = gargoyle-world variant;
  tribal skills replace U6 magic; palette `SEPAL` (RGB6, verified compatible). `SE.1..SE.8` = save-slot
  LABEL files (not saves). **Initial actor table**: SE ships NO `savegame/objlist` (U6 does); the
  256-actor OBJLIST is **generated by `GAME.EXE` into `savegame\objlist` on new-game**. Hunt (RE
  workspace `se_actors.md`) ruled out a shipped file, a static `GAME.EXE` blob, the objblk (its 4844
  objects are all inanimate — pots/mats/baskets, zero actors; confirmed via `coredump`), and Nuvie's
  `ultima.dat`. Static SE data gives per-NPC POSITIONS (`SCHEDULE`) + faces (`FACES.LZC`) + dialogue
  (`TALK.LZC`) but NOT appearance (obj_n→tile) or stats — those live only in the generated objlist.
  **RESOLVED (2026-07-19): objlist CAPTURED.** New-game SE run under ScummVM (Ultima/Nuvie engine) in a
  nested Xephyr display, driven by xdotool; saved via the in-game Escape menu → `New Save`. `nuviesave`
  read the save (`thesavageempire_enh.001`, game_tag 21317) with no per-game change → `se.objlist`
  (7539 B) = **167 populated actors** (103 surface@z0): avatar=obj_n310@(522,184), tribespeople obj_n
  310–328 (317×35, 316×27, 313×24…) + animals 235/249/251. Feeds mkworld like the U6 `world-svm` path
  (a **CACT** core-pack section: obj_n + x,y,z + status per actor). Clean-room-pure follow-up: locate
  the objlist init template inside `GAME.EXE` (now that ground-truth bytes exist) so the ship pipeline
  needs no ScummVM. See `docs/formats/objlist.md` + RE `se_actors.md`.
- **Martian Dreams:** Mars; toroidal world; era/technology mechanics; far more `.LZC` scripted
  content (SCENE*/MIDGAME*/MARS); game palette = **`MDPAL`** (RGB6, first 768B; renders correctly).
- **Container wrapping (SE/MD):** compressible core files (MAPTILES.VGA, MASKTYPE.VGA, LZOBJBLK) are
  single-item `.LZC` wraps holding raw data, where U6 uses bare LZW; MAP/CHUNKS/TILEINDX/OBJTILES/
  BASETILE/TILEFLAG are raw in all three. SE/MD TALK/LOOK/FACES = multi-item `.LZC` (see lzc.md).

## Status
IN PROGRESS (2026-07-19). P0 (GameDef identity + magic→game registry) + P1 (SE/MD data path) DONE;
**P2 well advanced — SE boots POPULATED from the shared binary**: core-pack loader path +
`gamedef_se`/`gamedef_md`; world + objects (`CLOK`) + font (`CFNT`) + portrait (`CFAC`) + **NPCs/party
(`CACT`, captured objlist)** all render. `make shot-se-svm` shows the Kurak village with tribespeople;
U6 unchanged (enginetest ALL PASS, throne-room boot correct). Next P2: FACES/TALK→NPC mapping by actor
number, look/get/use, SE RuleSet. Then P3 (MD slice) / P4 (acceptance). Depends on M6/M8.

## Cross-links
- `engine/ruleset.h` (GameDef/RuleSet), `dataformats/u6map.h` (geometry #defines to promote),
  `render/gamedemo.c` (pack loader), `tools/mkworld.c` (host packer), `tools/CLAUDE.md` (manifest).
- `docs/formats/*` (shared formats — verify SE/MD deltas), `docs/rules/*` (per-game rule deltas).
