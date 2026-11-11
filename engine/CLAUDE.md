# CLAUDE.md — Core Engine Subsystem

## Scope
The shared, hardware-independent game logic that drives all three titles:
Ultima VI, The Savage Empire, Martian Dreams. The engine talks ONLY to the
renderer interface (/render) and the data-format loaders (/dataformats).
NO direct hardware access here, NO XBIOS/VDI calls — those live in /platform
and /render. NO file-format byte-twiddling here — that lives in /dataformats.

## Architectural premise
All three games share one engine lineage (confirmed: Nuvie/ScummVM run all
three on one codebase). Differences are DATA + a handful of per-game rules,
not separate engines. Design for "U6 engine + per-game config/overrides,"
NOT three forks.

### Shared vs. per-game split
- SHARED (in /engine):
  - Main loop, turn/time scheduler, real-time clock vs. game-time.
  - Map/world model (8x8 chunk templates, toroidal wrap — same in all 3).
  - Tile/object model, animation timing, line-of-sight, lighting/day-night.
  - Pathfinding, party/movement, schedules (NPC daily routines).
  - Combat resolution, inventory, containers, weight/equipment.
  - Conversation VM (usecode interpreter) — see below.
  - Save/restore orchestration (format details in /dataformats).
  - UI/screen layout model (map window, command bar, message scroll,
    portraits, inventory) — drawn via /render, not directly.
- PER-GAME (in /games/{u6,savage,martian}):
  - Game id, asset manifest (which files, palettes, fonts).
  - Map level semantics (U6: nested small->large + 3D dungeons; Savage:
    uses 4 of 6 levels, level 6 = gargoyle-world variant; Martian: toroidal
    cylindrical/spherical world). "Level" meaning differs — keep behind an
    interface.
  - Rule deltas: combat tweaks, item/spell sets (U6 magic vs. Savage tribal
    skills vs. Martian era mechanics), status effects.
  - Intro/endgame/cutscene sequencing.
  - String tables / language data.

### Recommended interface seams (so per-game stays data-driven)
- GameDef: static descriptor (id, asset manifest, map-level layout policy,
  feature flags).
- RuleSet: virtual hooks the shared systems call (on_combat_hit,
  on_use_item, on_rest, on_enter_level, ...). Default impl = U6; Savage/
  Martian override only deltas.
- WorldModel: shared chunk/object representation; level-mapping is a policy
  object provided per game.
Resolved: C with structs of function pointers (see RESOLVED DECISIONS; the
game layer tables gl_u6/gl_se/gl_md in docs/design/game_layer.md).

## Golden rules (engine-specific)
- Big-endian 68000. Engine works on IN-MEMORY decoded structures only; all
  little-endian file parsing is done in /dataformats before data reaches us.
  Engine code must never assume a particular on-disk byte order.
- Clean-room: behavior may be matched to the originals/Nuvie by OBSERVATION
  and spec, not by transcribing source. Document non-obvious rules in
  /docs/rules/ with the reference used to derive them.
- No copyrighted assets shipped; engine loads user-provided data via the
  asset manifest only.
- Renderer-agnostic: never assume color depth or resolution. Ask /render for
  capabilities; lay out UI responsively where the original allowed it,
  otherwise target the canonical internal layout and let /render present.
- Memory-aware: must run on modest ST RAM. Keep the resident working set
  small; stream/page map and tile data. Resolved: 4 MB on every target
  (a stock TT/Falcon with no Fast RAM, and a 4 MB ST/STE on the 16-colour pack);
  full VGA assets need TT/Falcon/card. See docs/design/memory_streaming.md.

## Main loop (shared shape)
1. Poll input (via /platform input abstraction).
2. Advance game clock; run due scheduled events (NPC schedules, timed
   effects, animation frame ticks).
3. Resolve player action -> turn cost; advance world by elapsed turns.
4. Update visible region (LOS, lighting).
5. Build frame: compose map window + UI into renderer backbuffer.
6. rdr_present().
Keep simulation tick decoupled from present rate where feasible
(animations/lighting can update on game-time, not frame-time).

## Conversation / USECODE VM (HIGHEST-RISK SUBSYSTEM)
- The dialog "control bytes" are the documented wall that stalled prior
  recreations. Treat the conversation interpreter as its own milestone with
  its own /docs/formats spec and its own test corpus.
- Build it as a small bytecode VM: opcode table + data stack + access to game
  state (flags, party, inventory, NPC vars). Parser/decoder lives in
  /dataformats; the VM (execution semantics) lives here in /engine.
- Validate against captured original dialog flows (input choices -> expected
  text/state changes) before trusting it. Add per-game opcode deltas behind
  the RuleSet if Savage/Martian diverge.
Resolved for U6 and SE: SE shares the opcode set with a few differences and has
its own container (converse.md §4, §6b-6d). Martian Dreams has not been read yet.

## Save / restore
- Engine orchestrates; /dataformats owns the byte layout of save files.
- Decision: native-original save compatibility, or our own save format?
  - Pros of original-compatible: interoperate with real game saves.
  - Pros of our own: cleaner, versioned, cross-target portable.
Resolved: original-compatible. The engine reads and writes the ScummVM/original
save layout (docs/rules/save_load.md, docs/formats/nuvie_save.md), and our saves
load in real ScummVM (tools/savecheck.sh).

## Testing
- Headless logic tests: drive the engine with scripted input, assert world
  state transitions (movement, turn cost, combat outcomes with fixed RNG).
- Deterministic RNG seam so tests are reproducible.
- Conversation VM: golden-transcript tests per NPC.
- Integration: run under Hatari (--run-vbls / state save-load) for full-stack
  smoke tests; compare rendered frames per target (golden images in /render
  tests).

## Decisions log (engine)
All four early questions are settled in RESOLVED DECISIONS below: C interface
structs, a 4 MB minimum with VGA assets on TT/Falcon/card only, original-compatible
saves, and integer/fixed-point math only.

## RESOLVED DECISIONS
- Language: C (NOT C++). Use the "interface struct" pattern (structs of
  function pointers) for GameDef/RuleSet per-game overrides. Rationale: tiny
  override surface, predictable code size on base ST, no C++ runtime cost,
  best fit with existing m68k GCC toolchain. (If C++ is ever revisited, it
  MUST build with -fno-exceptions -fno-rtti and avoid heap-heavy STL.)
- Math: integer / fixed-point ONLY. NO FPU dependence anywhere in shared
  code. Rationale: base ST has no FPU; TT often shipped without the optional
  68882; Falcon needs an FPU mod. Treat FPU as never-present.
- Save format: original-compatible on read AND write (revised from an early
  "own format" plan): saves use the ScummVM/original layout and load in real
  ScummVM. See docs/rules/save_load.md.
- VGA (256-color) assets require TT / Falcon / 256+ color video card. Base
  ST/STE run from down-converted low-color asset packs (see palette note).

## RuleSet / GameDef pattern (C)
typedef struct RuleSet {
    void (*on_combat_hit)(GameState*, Actor*, Actor*);
    bool (*on_use_item)(GameState*, Item*, Actor*);
    void (*on_enter_level)(GameState*, int level);
    /* add hooks ONLY where Savage/Martian actually diverge from U6 */
} RuleSet;
- ruleset_u6 = defaults; savage/martian set only the pointers that differ.
- NULL pointer => fall back to the U6 default for that hook.
- GameDef holds static descriptor + asset manifest + level-mapping policy.

## Palette / asset quality (DELIBERATE ENHANCEMENT)
KNOWN ISSUE with the original ST port: it appears to have reused the fixed
EGA 16-color palette, despite the ST shifter supporting 512 colors (3 bits/
channel) and the STE 4096 (4 bits/channel). The original ST release was
criticized as a lazy port partly for this.
OUR APPROACH (host-side /tools pipeline):
- For the 16-color (shifter) target: quantize the 256-color VGA source art
  to an OPTIMIZED 16-color palette chosen from the ST's real 512-color gamut
  (per-scene/per-asset palettes where the engine allows palette swaps),
  NOT the EGA palette.
- For STE: target the 4096-color gamut for a richer 16-color selection.
- Goal: 16-color output that visibly improves on the original ST port.
  This is a marketed feature of the project ("fixes the lazy ST palette").
- All quantization is BUILD-TIME (host PC), never on the ST at runtime.

