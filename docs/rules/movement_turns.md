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
> - Last updated : 2026-11-04

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
same way; §3b has every other price. Rest keeps explicit ten-minute rest turns (rest.md).

### 3b. What DOS GAME.EXE 4.5 does (read 2026-11-04)
Every actor holds movement points, a signed byte. **Spending** (one routine for everybody): an actor of
type 431, the horse with a rider, pays half, rounded down; the minimum is 1.

**The next to act** is the actor with the highest points / dexterity, among those alive, on the party's
level, within 39 tiles of the leader, with points above 0 and able to move. A follower out of combat is not
picked: it walks when the leader does, and when any follower is at 0 or below the leader's points go to 0,
so **the slowest walker sets the pace**. An actor that is too far away loses its points.

**When nobody has points** the minute ends: an actor below 0 gets its dexterity added, any other is set to its
dexterity, so debts carry over and savings don't, and the clock moves one minute.

| Action | Points |
|---|---|
| A step | 5 + the impedance of the ground (below). The leader pays for the tile it enters, others for the tile it leaves. |
| A diagonal step by the leader | 5 more |
| Raft, balloon | 5 |
| Skiff, ship | by the angle to the current or the wind (wind.md) |
| A)ttack, by anyone | 10 |
| A foe with nothing to do | 5 |
| G)et, D)rop | 3 |
| M)ove, U)se | 5 (a refused use costs nothing) |
| C)ast | 10 + 3 × the spell's circle |
| L)ook, T)alk, readying | nothing |
| P)ass | all the leader has left |

**Impedance:** the high nibble of tileflags1, for the map tile and for each thing on it, skipped for an
impassable tile. So open ground costs 5, forest 12 and swamp 20.

**Ours.** A command's price goes through `turn_spend`: the clock moves by the points over the pace, and each
actor is paid those points × its dexterity / the pace, in sixteenths, keeps any debt, and acts while it is above
0, at most four times a command. This gives the same actions per minute as DOS but not its order within the
minute, and one effect timer tick per command. Actors beyond 39 tiles still walk (schedules depend on it).
A follower out of combat walks each command, whatever its points. A mounted Avatar with followers pays the
walkers' full price. Rest and the test hooks use the old way, everyone once. Timers keep their per-command tick.

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
