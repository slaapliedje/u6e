# Rules: Combat (U6)

Games: U6 (default). Savage/Martian share the engine path with different weapon/actor
tables — map to RuleSet data, not code forks.
Implemented by: /engine (turn loop, actor HP/stats) + /render (gamedemo: Attack command,
enemy turns, messages). Data via OBJLIST actor stats (already shipped) + TILEFLAG.
Tracked in: docs/INDEX.md · Milestone: M6 (combat).

> Provenance
> - Derived from : Nuvie `data/scripts/u6/actor.lua` (the authentic U6 combat formulas live
>   in Lua: actor_combat_hit_check / actor_take_hit / actor_hit / actor_get_ac / weapon_dmg_tbl
>   / armour_tbl) + `actors/Actor.cpp` (attack flow) — READ to understand, written here in our
>   own words, implemented from THIS spec. Stat semantics cross-checked vs the Ultima Codex
>   wiki (Strength→melee damage, Dexterity→hit chance) and the "Data Driven Gamer" mechanics
>   writeup.
> - Tools        : curl of the public Nuvie scripts; our OBJLIST stat decode (statprobe).
> - Validated by  : NOT YET (engine attack vs an NPC; later vs ScummVM behaviour).
> - Confidence   : formulas confirmed (transcribed-as-spec from Nuvie's data scripts).
> - Last updated : 2026-06-27

## 1. Model
U6 combat is TURN-BASED and happens ON THE MAP (no separate combat screen). Each turn every
actor acts. The avatar/party attack via the **A)ttack** command; hostile actors move toward
and attack the party on their turn. We already ship per-actor **str/dex/int/hp** (OBJLIST,
M6 stats) — combat consumes those.

## 2. Hit check  (actor_combat_hit_check)
A single d30 roll decides hit vs miss.
```
attack_role = use_str ? str_adj(attacker) : dex_adj(attacker)
foe_dex     = dex_adj(defender)
roll        = rand(1..30)
MISS  if  floor((foe_dex + 30 - attack_role) / 2) >= roll
HIT   otherwise
```
- `use_str` = the weapon is strength-based: obj_n ∈ {4 spiked helm, 34 club, 39 mace,
  44 two-handed hammer}, or a monster flagged strength_based. Else dexterity.
- `str_adj` = str (if cursed: max(1, str-3)).
- `dex_adj` = dex (if cursed: max(1, dex-3); if asleep or time-stopped: 1).
- Special: the **glass sword** (obj_n 48) and attacks on non-actors always hit.
- Intuition: higher attacker stat → lower threshold → more hits; higher defender dex →
  higher threshold → more misses. With equal 15/15 the threshold is 15, so ~50% hit.

## 3. Damage  (actor_take_hit → actor_hit)
```
max = weapon_dmg(weapon)                 -- table below; monster = actor base dmg
if max == -1            : max = 1
elif max > 1 and != 255 : max = rand(1..max)        -- the damage roll
ac = armour_class(defender)                          -- sum of readied armour (+3 protected, -3 cursed)
if max != 255 and ac > 0 : max -= rand(1..ac)        -- armour soak
if max <= 0 : "<name> grazed."   (no damage)
else        : apply max to defender
```
Apply (actor_hit): if `max >= hp` the defender DIES (hp→0, removed; killer gains exp if the
victim was evil); else `hp -= max`. 255 = instant kill (glass sword, god-mode). Lord British
(obj_n 409) is immortal (hp clamped to 255).
- **Exp-award table (DONE)**: exp gained on a kill = `floor((str+dex+int+hp+dmg+exp)/4)` from the
  victim's stats (`kill_exp`: hp = the creature's max HP, dmg = the killing blow, exp = the
  victim's own experience value), min 1. Replaced the placeholder `2 + str/2`. (Confidence:
  per our spec — there is no on-screen ground-truth to verify the exact divisor/terms against
  without Nuvie source; the values it produces are sensible — slug ~10–20, gargoyle ~50.)
- **Leveling + party leveling (DONE)**: an actor's max HP = **level × 30**; experience thresholds
  **double** (L2=100 … L8=6400; cap 8). Experience is **per-actor** — each member runs its own
  exp (loaded from objlist +0xc00, pack **U6WO**, + kills, capped) and its own level
  (`g_alevel[]`, init = maxHP/30). On a kill the **slayer** earns the exp (the Avatar via
  `do_attack`, the attacking companion via `party_attack`); kills do NOT auto-level.
- **Training gate (DONE)**: authentic U6 gates the HP gain behind **training** — Lord British
  (obj_n 409; trainer flag, pack **U6WP**) advances levels. T)alking to a trainer advances EVERY
  party member that has earned the next level (the whole party is blessed: +30 max HP each, heal
  to full) via `try_train`; otherwise his normal conversation runs. The status line shows the
  Avatar's "Lv N". Verified vs the save (avatar level 3/exp 370 → +exp doesn't level → at
  threshold → level 4/HP 120) and a companion advancing 3→4 on the same visit.
- **Stat trainers (DONE — MVP)**: when no level is earned, a trainer instead raises a STAT for
  **gold** — `train_stat` spends `STAT_COST` (100) gold to raise the Avatar's lowest of
  STR/DEX/INT by 1, capped at 30. Gold is the **conversation VM's pool** (`g_cg.gold`) — the same
  gold merchant/trainer dialogs read/mutate, so there is ONE gold source. **Gold is now REAL**:
  mkworld sums the party's coins (obj_n 88, `OBJ_U6_GOLD`, incl. coins in bags) and ships it as
  pack U6WR → `g_cg.gold`. On the sample save = **110 gold** (Shamino 95 + Iolo 15); one raise
  (110→10) is then affordable. MVP simplifications remaining: auto-picks the lowest stat (U6's
  per-trainer specialty needs the trainer→stat NPC list), and Lord British doubles as the
  stat-trainer.

## 4. Weapon damage table (weapon_dmg_tbl, obj_n → max dmg)
sling 33=6, club 34=8, main gauche 35=8, spear 36=10, throwing axe 37=10, dagger 38=6,
mace 39=15, morning star 40=15, bow 41=10, crossbow 42=12, sword 43=15, two-h hammer 44=20,
two-h axe 45=20, two-h sword 46=20, halberd 47=30, glass sword 48=255 (instakill),
boomerang 49=8, triple crossbow 50=12, magic bow 54=20, spiked helm 4=4, spiked shield 13=4,
staff 78=4, lightning wand 79=30, fire wand 80=20, flask of oil 83=4, cannon 221=90,
ship 412=30. Unarmed/monster = actor table base dmg (actor_tbl[obj_n][5]).

## 5. Armour class table (armour_tbl, obj_n → AC, summed over READIED items)
leather helm 1=1, chain coif 2=2, iron helm 3=3, spiked helm 4=3, winged helm 5=2,
brass helm 6=2, gargoyle helm 7=3, magic helm 8=5, wooden shield 9=2, curved heater 10=3,
winged shield 11=3, kite shield 12=3, spiked shield 13=2, black shield 14=2, door shield 15=4,
magic shield 16=5, cloth armour 17=1, leather armor 18=2, ring mail 19=3, scale mail 20=4,
chain mail 21=5, plate mail 22=7, magic armour 23=10, spiked collar 24=2, protection ring 256=5.

## 6. Range  (get_weapon_range)
Melee = adjacent (Chebyshev 1). Reach weapons morning star(40)/halberd(47) hit at the melee
gate too. Ranged: bow 41=5, crossbow 42=7, magic bow 54=7, sling/thrown shorter; a ranged
attack also needs a clear line (map_line_hit_check). MVP is MELEE only. The SHIP as a weapon
(412: range 5, damage 30, a cannonball missile) is docs/rules/cannons.md.

## 7. Enemy AI (worktype)
Hostile actors run worktype ASSAULT: each turn, path one step toward the nearest party member
(reuse our bounded-BFS pathfinder) and, if adjacent, attack. A badly-hurt hostile (HP ≤ 1/4
of its starting HP) switches to worktype FLEE and retreats away from the party (CB4). An actor
is hostile when it is alignment-hostile (pack U6WM, see §8 for the tuned rule) OR has been
struck by the player (authentic: striking a townsperson makes them fight back).

**Aggro-on-sight (DONE).** A hostile does not omnisciently pursue from across the map — it
LURKS until it PERCEIVES the party: a per-actor `g_alert` latch flips on once any living party
member is within `AGGRO_SEE` (5 tiles) **and** in line-of-sight. `has_los` ray-marches the
cells between them and blocks on a sight-blocker — either a base-map boundary (`blocks_sight`:
boundary tile that is not a look-through window) OR a sight-blocking OBJECT on the cell
(`g_sightblk`, a per-tile bitmap built in `build_blocked` alongside the impassable-object map,
so it rebuilds when a door opens/closes). The key object case is a CLOSED DOOR sitting on an
open floor tile: the base tile is walkable but the door blocks both sight and (when shut)
passage; an OPEN door is see-through (not a boundary) and a table is impassable but see-over —
so sight and passability are tracked separately. Once alerted a hostile stays alerted and runs
the ASSAULT/FLEE logic above; being STRUCK alerts it immediately (it knows where you are). This
is all a runtime latch + bitmap (no pack change). Note U6 hostility is per-ACTOR, not per-type
— the SAME gargoyle obj_n (363) is a peaceful WORK citizen in one slot and an ASSAULT attacker
in another — so a peaceful-worktype creature is NOT a latent enemy and never "wakes" as one;
only an already-hostile actor (combat worktype, §8) lurks-then-engages. Validated (COMBAT.BIN
byte 9): a hostile at d=8 stays unaware (alert=0, doesn't approach); brought to d=4 with clear
LOS it alerts and closes in; all 3 real throne gargoyles (in sight at d≤3) alert and engage.
And byte 10 (LOS objects): 379 sight-blocking objects found in the world; with one moved
between a hostile and the party, perception drops to 0 (was 1 over open carpet), and clears
again when removed. **Cowards** (per-actor
worktype WORKTYPE_U6_COMBAT_RETREAT = 0x07, read from the objlist at +0x11f1 and shipped as
pack U6WN) flee the party ON SIGHT (nearest member within ~5 tiles) and otherwise walk their
normal schedule; when STRUCK a coward turns hostile and switches to ASSAULT ("FLEE actors
that get hit switch to ASSAULT"). DONE — validated headlessly (COMBAT.BIN byte 5): a coward
fled the avatar (y 355→357), then after a hit became hostile and closed back in (moved 2
tiles toward the party).

## 8. Implementation plan (stages)
- **CB1 — player melee attack — DONE.** A)ttack (key 'A' / command-bar icon 0) → direction
  → if an actor is in that tile, §2 hit check (avatar dex vs target dex) + §3 damage (default
  sword max 15, AC 0), apply HP/death, print Missed/grazed/Hit/Killed. Math lives in
  engine/combat.c (combat_hit/combat_damage), unit-tested `make combattest` (ALL PASS:
  ~50% at equal stats, 100% strong-vs-weak, 3.3% weak-vs-agile, dmg 1..15, armour soak,
  255 instakill). On-target: `make build/game.prg` + COMBAT.BIN sentinel spawns an acid slug
  east of the avatar; the TT capture shows "Killed acid slug!" (build/game_combat.png).
- **CB2 — enemy turn — DONE.** A `g_hostile[]` flag (set when the player A)ttacks an actor)
  makes that actor ASSAULT: in step_npcs, a hostile non-party actor finds the nearest living
  party member (incl. the Avatar) and, if adjacent, attacks it (enemy_attack: §2 hit + §3
  damage, monster melee = max(2,str/2) until CB3 ships the table), else paths one step toward
  it (npc_pathstep). Damage reduces the member's HP (g_ahp) and the roster shows it
  (sync_party_hp). Validated on-target: a hostile acid slug closes and wounds Dupre 90->83
  ("acid slug misses." last shown), clock 8:00->10:00 (build/game_cb2.png, COMBAT.BIN byte 2).
- **CB3 — weapon/range/armour tables + RANGED — DONE.** engine/combat.c gains weapon_dmg(),
  weapon_range(), armour_ac() (§4-6 static tables, unit-tested). do_attack uses the avatar's
  weapon (g_avatar_weapon) for damage AND range: ranged weapons (bow 5 / crossbow 7 / …) hit
  the first actor along the direction within range, stopped by wall sight-blockers
  (sight_blocked_at); melee needs adjacency. Validated on-target: a bow kills a slug 2 tiles
  away (build/game_cb3.png).
- **CB3b — readied weapon/armour from OBJLIST inventory — DONE.** The save's actor-inventory
  chunk (objlist.md §3b: readied = status&0x18==0x18, owner = x field) is parsed host-side
  (mkworld build_actor_gear, reusing weapon_dmg/armour_ac) into per-actor {weapon obj_n, AC},
  shipped as pack U6WL (256×3 B). The engine loads g_aweapon[]/g_aac[]: the avatar attacks
  with its real readied weapon; armed actors use theirs; damage is reduced by the defender's
  armour class. Real gear decoded from the save: Avatar sword, Dupre sword + AC 13, Shamino
  AC 8, Iolo a CROSSBOW (ranged) + AC 3, Lord British AC 5. Validated on-target: the slug
  that wounded Dupre 90->83 at AC 0 now only "grazes" the armoured party (all stay 90) —
  build/game_cb3b.png. (Monsters have no readied weapon -> still str-derived melee, fine.)
- **CB4 — alignment + combat mode + fleeing — DONE.** (1) **Alignment** (tuned, see below):
  per-actor hostility shipped as pack U6WM (256×u8). `actor_hostile(i)` = struck-flag (CB2)
  OR alignment-hostile. (2)
  **C)ombat mode** (key 'c'/'C', "Combat mode!"/"Peace mode."): party members
  auto-attack an adjacent hostile each turn (adjacent_hostile + party_attack). (3)
  **Assault**: hostile non-party actors path to the nearest party member and strike
  (hostile_step/enemy_attack, the CB2 path). (4) **Fleeing**: a hostile whose HP falls
  to ≤ 1/4 of its starting HP (snapshot g_amaxhp at load) enters worktype FLEE — on
  its turn it retreats DIRECTLY AWAY from the nearest party member instead of attacking
  (check_flee sets g_aflee; hostile_step heads to a goal away from the party; best-effort
  — a cornered actor stays). Message "<foe> wounded. Flees!" / "Hit <foe>! It flees!".
  (5) **Exp**: a kill grants the avatar `2 + str/2` experience (g_exp), shown as "+Nexp".
  Validated on-target (Falcon, COMBAT.BIN byte 4, build/cb4.png): a wounded acid slug
  fled south (y 354→357, away from the avatar) while an alignment-hostile gargoyle
  assaulted the party (Avatar 90→70).
  COWARD worktype — DONE (see §7): per-actor WORKTYPE_U6_COMBAT_RETREAT (objlist +0x11f1,
  pack U6WN) flees on sight, switches to ASSAULT when struck.
  EXP-BASED LEVELING — DONE (see §3): maxHP=level×30, doubling thresholds, per-actor exp from
  objlist +0xc00 (pack U6WO), "Lv N" on the status line. TRAINING GATE — DONE (see §3): kills
  only accrue exp; Lord British (trainer flag, pack U6WP) advances thy level on T)alk when
  earned (+30 HP).
  ALIGNMENT — DONE, AUTHENTIC (reworked from ScummVM/Nuvie source). Hostility is the actor's
  real U6 **alignment**, read from the objlist **status byte (0x800), bits 0x60** (Nuvie
  `ActorManager::loadActors`): `align = ((status&0x60)>>5)+1` → 1 NEUTRAL, 2 EVIL, 3 GOOD, 4
  CHAOTIC; **enemy ⟺ EVIL or CHAOTIC ⟺ `status & 0x20` set**. mkworld ships this as pack U6WM;
  `actor_hostile(i)` = struck-flag (CB2) OR alignment-enemy. This SUPERSEDES the earlier
  obj_n+worktype heuristic (a proxy) — the party reads GOOD, townsfolk/LB/guards NEUTRAL, the
  three throne gargoyles EVIL. Result unchanged (**3 hostiles**, byte 8: total=3, near=3,
  party-flagged=0) but now from the authentic per-actor source, so it is correct for ALL scenes
  (dormant EVIL monsters, etc.), not just this one.
  GARGOYLE PEACE — DONE, and it needs NO special flag: peace is **per-actor alignment**. A
  gargoyle that has made peace carries NEUTRAL/GOOD alignment in the save; a hostile one carries
  EVIL. Verified on the sample save (byte 13): the three throne gargoyles (EVIL) are hostile, a
  **citizen gargoyle elsewhere (obj_n 363, NEUTRAL) is NOT**, the party/LB are not — and a struck
  citizen gargoyle defends (g_hostile). The old `g_peace` toggle + separate gargoyle flag were
  removed as redundant.
  AGGRO-ON-SIGHT — DONE (see §7): hostiles lurk until they perceive the party (range + LOS,
  including sight-blocking OBJECTS like closed doors), then engage; struck = instant alert.
  EXP-AWARD TABLE / PARTY LEVELING / STAT TRAINERS — DONE (see §3).
  DEFERRED (remaining):
  - **Training in Lord British's full plot conversation**: the conversation VM (cvm_game_t) has
    gold/karma/worktype but no level/exp/stat fields, and U6's combat/exp/advance logic lives in
    Nuvie's **Lua data scripts** (not the C++ engine we read), so folding advancement into LB's
    script needs VM level/stat ops + those scripts. The current model (T)alk-to-LB advances the
    party + trains, speaking as LB) is functionally complete.
  - **Dedicated stat-trainer NPCs**: U6 trainers are conversation-driven (each teaches one stat
    via usecode); needs the trainer-NPC list + a VM stat op. We keep auto-lowest via LB.
  - **exp-award formula** (`kill_exp`) is our spec — U6's real per-kill exp is in the Lua scripts
    (not in the C++ tree), so the divisor/terms are unverified (values are sensible).

## 9. Test hooks
Headless `make` capture: avatar A)ttacks an adjacent actor; message scroll shows the result;
HP change verified. Later: a hostile NPC trades blows over N turns. Compare behaviour (not
RNG) vs ScummVM where feasible. Deterministic RNG seam (engine/rng.c) for unit tests.

## 10. Status
SPEC complete. **Combat milestone (M6) DONE** + validated on-target: CB1 (player melee),
CB2 (enemy AI assault), CB3 (weapon/range/armour tables + ranged), CB3b (readied weapon/
armour from OBJLIST inventory -> real per-actor gear, armour reduces damage), CB4 (per-obj_n
alignment [U6WM] so monsters are hostile by default + C)ombat-mode toggle + party auto-fight
+ wounded-FLEE + per-kill exp). Follow-ons: coward worktype, exp-based leveling, alignment
tuning (see CB4 above).
Captures: game_combat.png (melee kill), game_cb2.png (enemy wounds unarmoured party),
game_cb3.png (bow kill at range), game_cb3b.png (armoured party shrugs off the slug),
cb4.png (wounded slug flees south while a gargoyle assaults the party).
Unit test: `make combattest` (hit/damage distributions + weapon/range/armour tables).
