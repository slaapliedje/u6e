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
> - Last updated : 2026-11-07

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
| step onto another actor  | no move, as a blocked tile (DOS, seen 2026-11-07) |
| step onto a party member | the Avatar moves; the member steps aside (DOS)    |

**Someone in the way (DOS U6 4.5, observed 2026-11-07 in DOSBox).** The Avatar at (305,350) pressed Up
into Nystul's chair at (305,349): the Avatar stayed where he was. The same Avatar pressed down-right into
Dupre at (306,351): the Avatar took Dupre's tile and Dupre moved to (307,351), aside, not back onto the
Avatar's old tile. So only party members give way. The small creatures that anyone may share a tile with
(combat.md §7c) are let through for the Avatar as for everyone else. Until 2026-11-07 u6e let the Avatar walk onto
anyone: the bot found him standing on a giant ant in the Ant Mound, where the fight could no longer go on.

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

**Ours.** The same: `sched_run` picks as above and hands the keys back when the actor at them comes up (combat.md §8b). The clock moves in
`minute_end`. What the minute does (`minute_effects`), from the routine behind DOS's clock: the magic timers (Light 100, Great Light 255,
Infravision, Eclipse and Negate Magic 20, Time Stop 10 and it stops the clock too); torches and readied rings and cloak for the party; the
conditions' own rolls (magic_skills.md §5b); the wind; what lies underfoot. On the hour: schedules (§8b), the drunk sobers, each magic-user
gains mana equal to its level up to its maximum. **Nobody regains health by walking**, only by resting, potions, spells and the regeneration ring. R)est and the test hooks use `step_npcs`, a minute's worth for everyone at once.

### 3c. Followers (DOS, read 2026-11-05)
Out of combat the others take places behind the leader. Slot n (roster order, leader skipped) lies `back[n]` tiles behind it and
`across[n]` to the side, along the leader's facing: across -1, 1, 0, -2, 2, -1, 1, -3, 3, 0; back 1, 1, 2, 2, 2, 3, 3, 3, 3, 1.
Two passes after each step of the leader. In each pass, each follower: if it stands clear of those before it in the line and beside one of
them it stays, except that in the second pass it still moves if its place lies ahead along the facing. Moving, it scores all eight neighbours it
could enter: 0x100 (a damaging tile 0x80, or none at all if it was already beside someone); 0x100 more for ending beside one of those
before it, or 0 if it was beside one and wouldn't be; less the distance to its place. It takes the best above 0. Either way it
pays 5, each pass, so **10 points for each step of the leader**, and a follower at 0 or below ends the leader's points.

Every other walker refuses a damaging tile.

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
pack (U6WG). (See docs/formats/tileflag.md.) A big object (tileflags2 size bits) blocks on each of
its cells by that cell's own tile, not only on its anchor: a fountain blocks all four of its tiles
(seen in DOS U6 2026-11-07: the castle garden fountain at (304..305, 378..379)).

**Eight ways.** DOS GAME.EXE 4.5 reads Up, Down, Left, Right, Home, PgUp, End and PgDn as the eight
directions (its key switch at image 0x648b, extended codes 0x147..0x151, direction index 0..7 into the
dx/dy tables at DS 0x1a2/0x1aa); on a PC the keypad's 7 9 1 3 are those last four. u6e: the arrows,
the keypad digits (8 2 4 6 and 7 9 1 3) and Clr/Home for north-west.

**A diagonal step needs one side open.** If both tiles beside the diagonal (the one across and the one
down) are blocked, the step is refused, with no message and no time spent; with one of them open it
goes. Seen in DOS U6 (DOSBox-X, 2026-11-07): from (304,377) to (303,378) with a bush and the fountain
either side, refused both ways, the target reached the long way; from (303,379) to (304,380) with the
fountain on one side only, taken. M)ove has the same rule for a pushed thing.

**Alone, a member walks as its kind.** In solo mode the one who walks keeps its own walker rules: a mouse
(Sherry) goes through mouse holes (213, 282, 310) as combat.md §7c has it for every mouse; that is how the
Rune of Valor is reached. Until 2026-11-07 the leader always walked as the Avatar (found by the bot).

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
- [x] Diagonal movement + cost (§3 cost; §4 keys and the corner rule, 2026-11-07).
- [ ] Running; party-follow movement.

## Status
IN PROGRESS (movement mechanics confirmed; passability + exact costs pending).
