# Engine core loop & world model (M5)

Date: 2026-06-23 · Milestone: M5 · Code: engine/world.*, engine/rng.*,
engine/ruleset.h, platform/platinput.*, render/gamedemo.c, tools/mkworld.c

## Shape
The engine is portable C operating on three seams: the **world model**
(decoded surface + player + clock), the **renderer** interface (/render), and
**platform input/timing** (/platform). This keeps the simulation fully
headless-testable (tools/enginetest) and the on-target build a thin wiring layer
(render/gamedemo.c).

Main loop (gamedemo.c, the on-target wiring):
```
load world pack -> world_init
loop: plat_input_poll -> world_try_move(dx,dy)
      if moved: compose centred view + avatar -> rdr_present
```
Per engine/CLAUDE.md, simulation is decoupled from present: we only recompose +
present when the world changed (a move). Animation/timed events on the game
clock come later (M6).

## World model (engine/world.c)
- Surface via u6map (MAP+CHUNKS), toroidal 1024×1024 tiles.
- Player (px,py), turn counter, game clock, deterministic rng_t.
- `world_try_move`: steps only onto a passable tile, advancing turns + clock by
  the RuleSet turn cost. Passability via a 256-entry table (see
  movement_turns.md — colour heuristic placeholder pending TILEFLAG).

## RuleSet / GameDef (engine/ruleset.h) — C interface-struct pattern
`RuleSet` is a struct of function pointers; `ruleset_u6` is the default; a NULL
hook falls back to U6. M5 has one hook (`move_turn_cost`); more get added only
where Savage/Martian actually diverge. `GameDef` is the static per-game
descriptor (id, start pos, rules). This is the seam that keeps the three games
data-driven instead of forked.

## RNG seam (engine/rng.c)
xorshift32, seeded per game. The single source of randomness so golden/logic
tests are reproducible.

## View compose (gamedemo.c)
Centres a 20×30-tile window on the player, blits each tile from the pre-decoded
tileset into the chunky backbuffer, then overlays a simple avatar marker at the
centre. Present (c2p) puts it on the TT screen. The map scrolls as the player
moves. (A proper UI — map window + message/stat panels — and a real avatar
sprite are M6.)

## World pack (tools/mkworld.c)
Big-endian: MAP + CHUNKS (the ST runs u6map on them) + tileset (256×16×16×8bpp)
+ palette + passable[256]. All decode/quantise is host-side; the ST does load +
u6map lookups + blit + c2p.

## Verification
- Headless (the M5 gate): `make enginetest` — movement, water-block, toroidal
  wrap, turn/clock advance, RNG determinism. ALL PASS.
- On-target: `make run-game` (interactive: arrows/WASD walk, Q quits);
  `make m5` (headless auto-walk + screen capture showing the scrolled view).

## Next (M6)
Objects/NPCs (savegame object data), real UI panels + avatar sprite, the full
usecode VM (promote the M4 spike), combat/inventory, save/restore.
