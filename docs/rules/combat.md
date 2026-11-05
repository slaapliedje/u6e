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
> - Last updated : 2026-10-31

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
  44 two-handed hammer}, or the attacker fights unarmed (a creature's own blow, or a person with
  nothing readied) and its type is flagged strength-based. Else dexterity. The flagged types (the
  reference creature table's "uses strength" column, read 2026-10-31): 98 mimic, 355 gazer,
  362 winged gargoyle, 363 gargoyle, 365 TV flower, 366 tangle vine, 367 daemon, 368 skeleton,
  370 headless, 371 troll, 373 wisp, 374 hydra, 376 fighter, 377 swashbuckler, 378 mage,
  379 villager, 380 merchant, 381 child, 382 guard, 383 jester, 384 peasant, 385 farmer,
  386 musician, 387 woman, 409 Lord British, 410 the Avatar, 424 cyclops, 427 giant ant, 428 cow,
  429 alligator, 430 horse, 431 horse with rider, 347 reaper.
- `str_adj` = str (if cursed: max(1, str-3)).
- `dex_adj` = dex (if cursed: max(1, dex-3); if asleep or time-stopped: 1).
- Special: the **glass sword** (obj_n 48) and attacks on non-actors always hit.
- Until 2026-10-31 u6e used dexterity for every blow, took 6 (not 3) off a cursed attacker, and
  ignored the defender's curse, sleep and Time Stop (found reading the reference while checking a
  bot fight where the Avatar missed a fleeing winged gargoyle 40 times running — legitimately: with
  dexterity 48 against 18 it cannot be hit).
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
- **Experience for a kill (REFERENCE-FAITHFUL 2026-10-28)**: only an **evil or chaotic** victim (its
  LIVE alignment) is worth anything; the award is `floor((str + dex + int + hp + dmg + xp) / 4)` from
  the CREATURE TABLE's base values for the victim's type (monsters.c `monster_base`: the table now
  carries the reference's per-type **xp** column — gargoyle 10, winged gargoyle 5, daemon 10, dragon
  12, silver serpent 15, cyclops 4, troll 4, mongbat 4, drake 4, headless 2, sea serpent 2, snake 1,
  scorpion / ant 3, alligator 6, reaper / tangle 4, Lord British 30, vehicles 10-30, everything else
  0). The live stats and the killing blow play no part. Examples: a gargoyle (363) is worth
  (25+19+8+30+15+10)/4 = 26, a giant rat (342) (5+13+4+8+6+0)/4 = 9, a deer or a townsperson 0.
  Previously we used the victim's live stats, the blow and its own exp value, for any alignment.
- **Levels (REFERENCE-FAITHFUL 2026-10-28)**: an actor's max HP = **level × 30** (capped 255). Experience
  accrues per actor (objlist +0xc00); kills never level anyone. Levels are gained ONLY at the
  **eight shrines** — their conversation scripts (entries 192-199, reached by T)alking to the shrine
  object, usecode_vm.md §6c) raise a meditating member one level when exp ≥ the script's own table
  `0, 100, 200, 400, 800, 1600, 3200, 6400` indexed by the current level (cap 8), and add that shrine's
  stat gift: Honesty int +3, Compassion dex +3, Valor str +3, Justice int +1 dex +1, Sacrifice str +1
  dex +1, Honor str +1 int +1, Spirituality +1 to all three, Humility level only. Lord British does NOT
  train or level anyone (our earlier "training gate" and gold-for-stat trainer were inventions and are
  removed); his script heals, as before. Our old doubling thresholds matched the scripts' table.
- **Resurrection costs experience** (the reference's actor_resurrect): a resurrected actor keeps
  `exp × karma / 100` when karma < 98 (Resurrect spell and a healer's RESURRECT alike).
- A few NPC scripts read a member's level or stats (Xiao, Nicodemus, Horance, Andrea's arm-wrestling
  reads the Avatar's strength) and one grants dex +1; they work through the same VM ops.

## 3b. Attacks on OBJECTS (actor_hit's object branch; 2026-10-27)
When the blow lands on a tile with no actor, the TOP object there is the defender (what the
cursor shows). A tangle vine (366) is "killed" (a message). Otherwise only a **non-stackable
object with a nonzero count** can be hurt — the count is its strength (262 of the surface's 357
doors carry one; chests, barrels, crates, chairs, kegs do too) — never a magically locked door
(297-300, frame ≥ 12) or an open chest (98 frame 3). The weapon's MAXIMUM damage (not a roll)
comes off the count; when the count is at most the damage the object is **broken**:
`<name> broken!`, its trap (337) fires on the attacker, its contents spill onto its tile, a
mirror (123) cracks (frame 2) and costs 10 karma instead of vanishing, anything else is gone.
u6e persists a break at once: the contents' records go on the map (sav_spill_k), a fired
trap's record and then the holder's own record are deleted (objblk.md order holds: the holder
is childless by then).

## 3c. Remains (2026-10-28)

A slain creature leaves what the reference's actor-type table says — a dead body, a blood splat, its
own object lying there, a carcass, or nothing — with the dead NPC's inventory inside (or on the tile
for blood / nothing); the table, the dead flag and what is saved are in usecode_use.md "Created
and destroyed objects persist". Hatched and summoned creatures leave the same remains, temporary,
holding the kit they hatched with (eggs.md §3c). This holds whoever strikes the last blow
(CORRECTED 2026-11-01: a foe the Avatar slew in melee used to vanish with no remains and no dead
flag; only companions' blows and spells left them).

## 3d. Death of a party member and of the Avatar (2026-10-31)

> Derived from READING the reference's `actor.lua` (actor_dead, actor_avatar_death, actor_resurrect,
> get_LB_to_throne), `init.lua` (party_heal) and `party.cpp` (resurrect_dead_members), re-expressed.
> Found missing by the bot harness: melee and foe spells took the Avatar and companions to 0 HP and
> play went on.

- **A companion at 0 HP dies** (any cause: a blow, a foe's spell, a field, poison). Its remains
  (§3c; a person leaves a dead body, quality = the actor) lie where it stood and it leaves the map,
  but it stays in the party, shown in the roster with 0 HP. It neither follows nor fights.
  Its readied equipment stays with it (u6e pools the party's pack, so nothing is moved into the
  body; the reference drops the companion's inventory into the body — deviation).
  It comes back with the Resurrect spell cast on the body, a healer paid to raise the body the party
  carries (usecode_vm.md §6f), or the Avatar's death below. A dead companion's body is temporary:
  after a save and a load the companion is still dead (0 HP) with no body, and a healer raises it as
  if the party carried one (§6f), or the Avatar's death revives it.
- **Striking a companion** (from READING the reference's `actor.lua` actor_attack / actor_hit; found by
  the bot harness): A)ttack may rest on a party member. The blow rolls to hit and for damage as against
  anyone ("Hit Iolo!", "Iolo grazed.", "Missed Iolo."), but the companion never turns hostile, never
  flees, and gives no experience. At 0 HP: "Iolo killed!" and it dies as above at the end of the turn.
- **Karma**: a party member (the Avatar included) brought to 0 HP costs 5 karma ("allowing your
  party to die is bad karma"), taken before the Avatar's experience loss below. u6e takes it at the
  death itself, whatever the cause.
- **Whenever a dead companion rises** (the Avatar's death, Help, the Resurrect spell or a healer), its
  body is used up: the one the party carries first (a dead body, 339, whose quality names it), else
  one on the map (reference: Actor::find_body). Before 2026-10-31 a carried body stayed in the pack
  after Help or the Avatar's death had raised its owner (found by the bot).
- A companion dies once: the death sets its dead flag (the one the save keeps), and only a
  resurrection clears it; a dead companion that something puts back on the map is simply taken off
  again, without a second body or karma loss.
- Every cause (a blow, the Avatar's own, a field, poison) leaves the companion on the map at 0 HP
  until the end of the turn, so the death above (remains, roster) always runs. A non-party actor
  killed by a field or by poison leaves its remains like one slain in combat (§3c).
- **The Avatar at 0 HP**: at the end of that turn —
  1. "An unending darkness engulfs thee..." then "A voice in the darkness intones, "KAL LOR!"".
  2. A horse is dismounted, a vessel left where it is.
  3. Every dead companion stands up again (not after Armageddon).
  4. The Avatar's readied items go back into the pack, except the amulet of submission (76).
  5. The whole party is healed: full HP, no poison, sleep, paralysis or charm.
  6. The party is moved to Lord British's throne room, (307,352) on the surface.
  7. The clock runs on in whole hours until Lord British sits on his throne: an hour in 8..11 or
     14..17.
  8. Foes that were hunting the party forget it (they stop assaulting until they see it again).
  9. The Avatar's experience becomes exp × karma / 100 when karma is below 98 (actor_resurrect).
  10. Combat mode ends.
  The reference also fades the screen and plays the death and KAL LOR sounds; u6e prints the two
  lines only (deviation).
- The key pressed in the frame the death is noticed is dropped: the death ends the turn.
- Test hook: `DEATH.REQ '1'` (Dupre falls, then the Avatar, on dungeon level 1 at 20:00): body, off
  the map, still in the party; then (307,352) on the surface at 08:00, everyone at full HP and cured,
  Dupre back, exp 1000 → 500 at karma 50, combat mode off, the readied item in the pack, Lord British
  at (307,348). Identical on the emulated TT and the stock 4 MB Falcon.

## 3e. What some creatures' blows do (DOS GAME.EXE 4.5, read 2026-11-05)
After a blow lands on an actor that lives through it:
- **Corpser (357):** `%s dragged under!`. The victim takes no action and can't be attacked. Each of its turns, a d30 under its
  strength frees it (`regurgitated!`); otherwise `ARGH!` and d15 damage. Death and the party's death clear it.
- **Acid slug (364):** `A slug dissolves %s's %s!`: a random one of what it carries or has readied, of types 2-5, 7, 10-13, 15,
  19-22, 35, 38, 43, 113, 114 (metal arms and armour).
- **Gremlin (353):** `%s has been robbed!`: a random one of types 128-133, 135, 209-211 (food).
- **Types 342, 345, 358, 360, 361, 375, 413, 426:** one chance in four of `%s poisoned!`.

DOS reads this from a table of creature classes, keeping a bit for the poison. Only party members' things are taken here: a town
NPC's pack isn't in memory.

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

## 6b. Targeting and projectiles (A)ttack; 2026-10-27)
- A)ttack arms a **crosshair** (tile 365) on the Avatar; the arrow keys walk it one tile at a
  time, never beyond the weapon's range (Chebyshev distance; hands = 1) nor the view; Enter
  attacks the tile under it, Esc cancels, a click attacks the clicked tile (beyond range:
  `Out of range!`, no turn). Aboard a ship the weapon is the ship (cannons.md §1). The scroll
  names what the crosshair rests on (the actor — a party member by its own name — else the top object, else the terrain).
- **Melee** (an adjacent tile with a non-missile weapon; the morning star and halberd never
  shoot): the hit roll (§2) and the blow (§3), or §3b on an object.
- **A shot** (a missile weapon, or any target beyond one tile): the hit roll against the actor
  on the aimed tile first; a miss at a foe **scatters** the aim by −1..1 in x and y. Then the
  missile flies the line (Bresenham) from the attacker: it stops ON a tile with the missile
  boundary flag (tileflags2 bit 5, base or object tile) — actors and other objects do not stop
  it — else it reaches the aimed tile. Whoever stands where it lands is the defender: a foe met
  only by scattered aim gets a second hit roll; an object there is hit outright (§3b); nothing
  there, nothing happens. The missile tile is drawn tile by tile: sling 398, spear 547, throwing
  axe 548, dagger 549, bow/magic bow 566 (arrow), crossbows 567 (bolt), boomerang 560, oil
  flask 601, Zu Ylem 612, lightning wand 392, fire wand 393, cannons 399.
- Ranges (§6): spear 4 and boomerang 5 as the reference's table; Zu Ylem 4, wands 7.
- Ammunition, thrown weapons, the half-damage creatures and the rest: §6c.

## 6c. What DOS GAME.EXE 4.5 does (read 2026-11-04)
DOS is the rule; this replaces §6 and §6b wherever they differ.

**Distance** is not the larger of dx and dy but this table (rows |dx|, columns |dy|; 8 or more either way = 9):

```
0 1 2 3 4 5 6 7
1 1 2 3 4 5 6 7
2 2 2 3 4 5 6 7
3 3 3 4 5 6 7 7
4 4 4 5 6 7 7 8
5 5 5 6 7 7 8 8
6 6 6 7 7 8 8 8
7 7 7 7 8 8 8 8
```

An attack costs **10 movement points**, the player's and anyone else's; a step costs 5.

**Ranges** (anything else 1): morning star 40 and halberd 47 = 2 (a strike, never a shot); dagger 38 and throwing
axe 37 = 3; sling 33, spear 36, Zu Ylem 91 = 4; flask of oil 83, bow 41, boomerang 49, ship 412 = 5; crossbow 42,
triple crossbow 50, magic bow 54, wands 79/80 = 7.

**What counts as a weapon:** an item with a row in the damage table of §4, which also has Zu Ylem 91 = 0 and the
tools 100 = 6, 101, 103, 104, 105, 113, 114, 141 = 4, 109 = 2.

**The player's A)ttack** goes through every readied weapon of the party leader in turn, up to 8, one crosshair each:
"Attack with sword-" for the first, the same on a new line for the next. With none: "Attack with bare hands-"; aboard
a ship "Attack with ship cannons-". A later weapon's crosshair stays where the last one was unless that is beyond its
range, then it goes back to the attacker. A new A)ttack also starts where the last crosshair was, if in range. All of
this is one turn, and only if something was actually tried.
- Aimed at the attacker's own tile: `pass.`, no attack.
- Beyond range: `Out of range!`, the turn isn't spent by that weapon.
- Bow or magic bow without an arrow (55): `Out of arrows!`; crossbow or triple crossbow without a bolt (56):
  `Out of bolts!`. No attack.

**One attack** (anyone's: the player, a companion, a foe). In this order:
1. Out of range: stop. No ammunition: stop (silently for all but the player).
2. It is a **shot** when the target is beyond one tile, or the weapon is a sling, throwing axe, bow, crossbow,
   boomerang, flask of oil, triple crossbow, Zu Ylem or wand; never for the morning star or halberd.
3. **Interference:** a shot isn't fired if the last one to strike the attacker from beside it still stands beside it,
   alive, awake, not paralysed and not invisible (in Time Stop, only if that one is in the party). The player sees
   "<name> interferes!". The turn is spent and no ammunition is used. A blow "from beside" is a blow that landed, from
   within one tile.
4. Hit roll against the actor on the target (§2). A shot that misses scatters the target by −1..1 each way, and if an
   actor stands on the new target's line end the roll is made for it instead when the flight ends (as §6b).
5. A shot flies (§6b) and uses what it uses:
   - bow, magic bow: 1 arrow; crossbow: 1 bolt; triple crossbow: 3 bolts (all that are left if fewer);
   - spear, throwing axe, dagger, if the landing tile is beyond one tile from the attacker: one of them, a spare from
     the pack first, else the readied one. If an actor is on the landing tile, it is gone. Else it lies there free to
     take, unless the tile is water and impassable, or one of the four tiles 252..255, with nothing there to hold it:
     then it is gone.
   - flask of oil: one; unless the landing tile is water, it burns: a fire field there.
   - Zu Ylem: one; an actor on the landing tile is put to sleep, as by the spell, hit roll or not; no damage.
   - wands: 1 chance in 100 the wand goes: `A wand has vanished!`.
   - boomerang: flies back to the attacker afterwards.
6. Damage on a hit, with the actor on the landing tile as the defender, weapon max as §4 and §3. Against a daemon 367,
   ghost 352 or skeleton 368, the max is halved rounding up, except with the glass sword, triple crossbow or magic bow.
7. A hit with the glass sword 48: `Thy sword hath shattered!`, the sword is gone.

**The triple crossbow** fires up to 3 bolts, the number of bolts carried at most: one on the target (after any scatter),
two on tiles beside it, given by this 11×11 table of the target's offset from the shooter (row dy+5, column dx+5, high
and low nibble = the two directions, 0 north and clockwise to 7). Offsets beyond 5 are taken as 5 (DOS reads a neighbouring
row there; the player's crosshair can't reach them).

```
42 52 52 52 52 62 63 63 63 63 64
41 42 52 52 52 62 63 63 63 64 74
41 41 42 52 52 62 63 63 64 74 74
41 41 41 42 52 62 63 64 74 74 74
41 41 41 41 42 62 64 74 74 74 74
40 40 40 40 40 00 04 04 04 04 04
30 30 30 30 20 26 06 05 05 05 05
30 30 30 20 27 26 16 06 05 05 05
30 30 20 27 27 26 16 16 06 05 05
30 20 27 27 27 26 16 16 16 06 05
20 27 27 27 27 26 16 16 16 16 06
```

The first bolt uses the first hit roll; the others each roll against the actor on their own line's end. The crossbow's
damage is not halved (above).

**A foe or a fighting companion** picks its weapon when it attacks: among its readied weapons, the one with the highest
damage whose range reaches the target; with none, its bare hands (the creature table's damage; range 1). It attacks
when the target is in that range with a clear line, else it moves. So a bow foe shoots from up to 5 tiles and, out of
arrows, stands and does nothing there. A hatchling's arrows and thrown spares are those it was given (eggs.md §3c).

Ours: a town NPC's pack isn't in memory, so it never runs out and doesn't lose what it throws. One person's arrows are
the party's pooled pack (usecode_vm.md §6f). The order of a member's weapons is by slot, DOS's by carrying order. A
triple crossbow's bolts fly one after the other. Sounds are not played.

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

## 7c. Movement types (2026-10-27)
Every creature type moves by one of the reference's movement types (its actor table; engine
monsters.c monster_movetype), applied to each path step and wander step:
- **land** (everyone not listed): the walker's passability (terrain + blocking objects).
- **low air** — insects 343, giant bat 344, bird 356, drake 369, wisp 373, dragon 411, the
  inflated balloon 423: any tile except a Wall-flagged map tile, or one holding an object whose
  tile is Wall-flagged or both double-width and double-height (tileflags2 bits 6-7); water and
  deep water pass beneath.
- **high water** — giant squid 345, sea serpent 346, a ship 412: open water only (a Wet tile
  that is not a shoreline tile 16..47) with no object on it.
- **low water** — skiff 414, raft 415: any Wet tile with no object on it.
- **none** — the tangle vine pod 365 never moves.
No U6 type uses the reference's "high air" (map boundaries only). From DOS's tile test (15d7:000f): rot worms (360) burrow, entering only ground with tileflags2 bit 3 and no object on it; a
mouse (354) also enters a tile holding its hole (213, 282, 310); anyone may share a tile with a bird, mouse, insects, rabbit or
corpser (356, 354, 343, 359, 357), unless it is of that same type. Shallow water is wadeable by
walkers (the terrain passability says so); only deep water tells a bird from a rat.

## 7b. Foes casting spells (2026-10-27)

> Derived from: READING the U6 scripts in Nuvie's `ultima.dat` — `u6/actor.lua` actor_init (one
> "charge" object 336 per entry of the creature table's spell column, quality = the spell number),
> actor_get_weapon (the charge roll, the storm test, the fallback to the best weapon in range),
> actor_attack (a charge weapon casts the spell at the target; the wisp's 129 is its teleport),
> the weapon range table (a charge reaches 5), and the per-spell scripts for a non-player caster
> (no Success / Failed lines; the effects print their own) — to understand; re-expressed. The
> spell lists are in engine/monsters.c `monster_spells`. Confidence: probable. Validated by
> FOESPELL.REQ (§9).

Fifteen creature types cast: daemon, dragon, drake, winged gargoyle, gazer, ghost, hydra, mage,
mimic, reaper, sea serpent, giant spider, giant squid, wisp, tangle-vine flower (the list per type
is the table in monsters.c). A creature's charges are never used up.

**When.** Each turn a hostile that has a target within 5 tiles and in line of sight runs the
charge roll BEFORE moving or striking, in list order: a charge fires when `rand(0..3) == 0` and
`rand(0..spell_num) < 16` (so a 1st-circle spell is tried at 1-in-4, an 8th-circle one at about
1-in-32); the first that fires is cast and the turn is spent. No charge ever fires while the
magical storm runs (objlist timer 13: Negate Magic, or a worn storm cloak — that is the cloak's
purpose). Nothing fires → the usual melee (adjacent) / approach.

**Targets and effects** (the target = the nearest party member; its tile for area spells; the
reference's scripts with the monster as caster):

| spell | effect on the party |
|---|---|
| Magic Arrow 17 | dexterity check `((target_dex/2 + 30) − caster_int)/2 ≤ d30` → 1..10 damage |
| Poison 18 | §5b's strength roll (magic_skills.md) → poisoned |
| Sleep 20 | the intelligence contest → asleep |
| Fireball 34 | fire damage 1..20 (double / immune by the §5b flags) |
| Conjure 49 | a creature of the caster's side appears within ±5 of it: troll, giant rat, giant spider, giant bat or snake (a d16: 0-3 / 4-6 / 7-9 / 10-12 / else) |
| Fire Field 51 | a fire field on the target's tile (not on water) |
| Explosion 65 | fire damage to everyone in the 21-cell area around the target |
| Insect Swarm 66 | up to 8 hostile insect swarms within ±5 of the caster |
| Lightning 68 | 1..30 damage |
| Paralyze 69 | paralyzed (no roll): "X is paralyzed." |
| Charm 80 | the contest; a charmed COMPANION fights for the monsters until the charm lifts (the §5b roll): each turn it strikes an adjacent party member (the Avatar included), else stands. The Avatar itself resists (u6e deviation: no leader change) |
| Flame Wind 83 / Energy Wind 98 | the five-cell line through the target, perpendicular to the caster's aim (magic_skills.md §5f): fire damage / 1..30 |
| Web 89 | webs on the free cells of the 3×3 around the target |
| Chain Bolt 96 | 1..30 on the target, then up to 7 jumps to the nearest other actor within 4 of the caster |
| Fear 99 | every NON-GOOD, non-undead actor within 5 of the caster that loses the contest flees — the reference's script spares good actors, so a ghost's fear scares other monsters, not the party |
| Summon 119 | a daemon of the caster's side beside the player |
| 128 "monster invisible" | the caster vanishes from sight (a long invisibility; Reveal dispels it) |
| 129 (wisp) | "Wisp teleports!" — the wisp jumps to a free tile beside the player |

Damage to a party member goes through the same path as a monster's blow (it CAN reach 0 HP, like
melee); damage to anyone else through the spell damage routine. The cast shows the spell's
projectile tile on the target and a line "<creature> casts <spell>." (u6e feedback; the original
shows its casting flash).

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
  (5) **Exp**: a kill of an evil/chaotic creature grants the slayer the creature table's award (§3).
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
§7b VERIFIED 2026-10-27 (FOESPELL.REQ 1-7, seeded RNG, identical logs on emulated TT and the stock 4 MB Falcon
from the canonical save template): a mage three tiles from the party casts 15 times in 30 turns (Sleep, Magic
Arrow with its dexterity check, Fireball), never closing to melee, leaving one companion wounded and one asleep;
under Negate Magic the same mage casts nothing in 30 turns; a gazer's Charm turns Dupre against the party
("Dupre hits thee!") until the charm lifts after 10 turns; a giant spider's Web lays nine webs around a dog and
Iolo gets stuck walking into one; a daemon fades from sight; a ghost's Fear scares a dog and leaves the party
unafraid; a wisp teleports beside the Avatar; a dragon summons a hostile daemon; a winged gargoyle's Explosion
burns the whole party. Host `make combattest` / `magictest` pass.
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
- `ATTACK.REQ` (§6c): '1' a bow: arrows, the distance table, `pass.`, the time; '2' thrown daggers, spare first, lying
  free to take, gone on an actor; '3' interference, half damage, the glass sword; '4' a foe's bow: 3 arrows, then it
  stands; '5' two weapons in one command, the triple crossbow, oil, Zu Ylem; '6' a wand until it vanishes.
- `FOESPELL.REQ` (§7b, seeded RNG): '1' a mage 3 tiles from the party casts over 30 turns (which
  spells, the party's wounds / conditions); '2' the same under Negate Magic: no cast; '3' a gazer's
  Charm turns a companion against the party until it wears off; '4' a giant spider webs the party;
  '5' a daemon goes invisible; '6' a wisp teleports beside the Avatar; '7' a dragon's Summon.

## 8. Combat modes, Solo and COMMAND (2026-11-04)

> Provenance: the U6 manual's "Begin/Break Off Combat" and keyboard list; READING GAME.EXE 4.5 (the status display, the
> scheduler at $1ab7d, the worktype switch at $19c75 and its routines). Re-expressed, no code transcribed.

Each actor has a combat mode byte, objlist 0x12f1, saved: 2 COMMAND, 3 FRONT, 4 REAR, 5 FLANK, 6 BERSERK, 7 RETREAT,
8 ASSAULT. A new game: the Avatar COMMAND, Dupre and Shamino FRONT, Iolo REAR, anyone who joins ASSAULT. In combat a member's
worktype is its mode. The inventory display shows a companion's mode beside the buttons; the fifth button steps it,
wrapping from ASSAULT to COMMAND. The leader has no button.

What a mode does in combat, when a foe is within 8 tiles of the leader; with none, the member follows as usual:
- A foe in reach of a readied weapon or beside it is attacked (10 points), whatever the mode but RETREAT.
- FRONT holds a place abreast of the leader, across the way to the nearest foe. REAR holds two tiles behind the leader the
  same way. FLANK makes for a place two tiles to the foe's side. ASSAULT goes for the nearest foe, BERSERK for the one with
  the most health. RETREAT runs from them and does not fight.
- COMMAND: the member gets a turn at the keys after the leader's action, one for each COMMAND member, in roster order: the
  view moves to it and the keys are its own. A commanded action does not pay the others' time again.

Solo: keys 1-8 play that member of the roster alone; the others wait; 0 returns (`Solo mode`, `Party mode`). Refused
aboard and in combat. In Solo: `Not in solo mode!` at T)alk, ladders and holes, R)est (`-Not in solo mode!`); `Not in solo
mode.` at mounting; `You must be in party mode to enter.` at a moongate. A save, a load or a fall ends it.

(Engine: whoever is played takes actor slot 1 and the Avatar its slot, since everything reads slot 1 as "the one at
(px,py)"; they swap back before a save.)
