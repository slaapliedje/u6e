# Rules: Movement, turn cost, time advance

Games: U6 / Savage / Martian — default = U6; verify deltas
Implemented by: /engine (world.c, ruleset.c) + RuleSet hook move_turn_cost
Tracked in: /docs/INDEX.md  ·  Milestone: M5

> Provenance
> - Derived from : our world model design + reading the U6 surface (u6map).
> - References   : general U6 knowledge (tile-based movement, time advances per
>                  step); §3 time per step from READING ScummVM/Nuvie `core/player.cpp`
>                  (the base step cost) and the U6 actor script `u6/actor.lua` (the turn
>                  loop: one game minute per turn, every actor's movement points refilled
>                  by its dexterity) — re-expressed, no code transcribed.
> - Tools        : /tools/enginetest (headless), /render game demo.
> - Validated by  : enginetest asserts passable steps move + advance the clock,
>                  water steps are blocked (no move/turn), toroidal wrap, and
>                  deterministic RNG. All pass.
> - Confidence   : confirmed (movement mechanics) / PLACEHOLDER (passability
>                  source) / guessed (exact turn-cost value).
> - Last updated : 2026-06-23

## 1. System
The player occupies one surface tile (0..1023 on each axis, toroidal wrap). A
direction input attempts a one-tile step; it succeeds only onto a passable tile,
and a successful step advances the game clock by the move's turn cost.

## 2. Observed / implemented behavior
| Input / situation        | Result                                            |
|--------------------------|---------------------------------------------------|
| step onto passable tile  | player moves; turns += 1; clock += turn cost      |
| step onto blocked tile   | no move; no turn; clock unchanged                 |
| step off an edge         | wraps to the opposite edge (toroidal, 1024 = 2^10)|

## 3. Turn cost and the clock (RuleSet hook)
`move_turn_cost(world, dx, dy)` → the step's **movement points**; U6 = **5** for a walking step.
The game clock counts **minutes**. In the reference one turn is one minute, and each minute every
actor's movement points are refilled up to its **dexterity**; a step spends 5. So the Avatar takes
about dexterity / 5 steps per game minute (dex 20 → 4 steps).

u6e keeps one world turn per step (NPCs and the party act once per step, which matches the
reference while their dexterities are close to the Avatar's) and charges the clock separately:
the step's points accumulate and a minute passes per *Avatar dexterity* points (`world_spend`).
A passed turn (space) ends the current minute. Vehicles spend their own points (wind.md §2) the
same way; a M)ove push spends 5. Rest keeps explicit ten-minute rest turns (rest.md).
Diagonal cost not yet distinguished; terrain impedance is not added yet.

Until 2026-10-30 a step cost a flat **10 minutes** (a placeholder): a day passed in ~144 steps,
about 40× too fast, so schedules, nightfall and torch fuel ran away while walking.

## 4. Passability — from TILEFLAG (real)
Derived from **TILEFLAG** tileflags1: a tile is impassable for walking if
**Impassable (bit1)** or **Wall (bit2)** is set (`flags1 & 0x06`). Water carries
Impassable (blocked); shore is "Wet" only (walkable); swamp is Damaging but
passable. /tools (mkworld, passability.h `compute_passable_tflag`) precomputes the
256-entry terrain table; this replaced the old water-colour heuristic and now
blocks mountains/walls/bushes too. (Confirmed against real tiles: tools/passprobe.)

**Object blocking:** impassable OBJECTS (wall/furniture/closed-door tiles, again
`flags1 & 0x06`) are tracked in a 1024×1024 bitmap the engine builds from the
surface objects (render/gamedemo build_blocked, rebuilt on door open/close);
`world_is_passable` checks the terrain table AND this bitmap, so the player and
NPCs no longer walk through wall/furniture objects. tileflags1 ships in the world
pack (U6WG). (See docs/formats/tileflag.md.)

## 5. Determinism / RNG
All randomness goes through the engine rng_t (xorshift32), seeded per game, so
logic tests reproduce exactly (enginetest verifies same-seed == same sequence).

## 6. Test hooks
- `make enginetest` — the M5 headless gate (movement, block, wrap, turn, RNG).

## 7. Open questions
- [x] Decode TILEFLAG for real passability (replaced the colour heuristic; terrain
      + object blocking via tileflags1).
- [ ] Directional/impedance flags (tileflags1 bits 4-7) for one-way ledges etc.
      (currently Impassable|Wall = fully blocked, ignoring direction).
- [ ] Confirm U6's real turn-cost / time-advance granularity by observation.
- [ ] Diagonal movement + cost; running; party-follow movement.

## Status
IN PROGRESS (movement mechanics confirmed; passability + exact costs pending).
