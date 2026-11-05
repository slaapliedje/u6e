# magic_skills.md — Magic / spellcasting (U6; SE & MD deltas)

System: the Ultima VI **magic** rules — reagents, the spellbook, magic points
(mana), the eight circles, and per-spell reagent requirements + effects. This
is the U6 default RuleSet; Savage Empire (tribal skills) and Martian Dreams
(psychic/era mechanics) replace the spell set behind the same hooks (§7).

> Provenance
> - Derived from : reagent + spellbook OBJECT NUMBERS extracted from the user's
>   own data (refdata/dos/u6 LOOK.LZD names + BASETILE, via tools/magicprobe);
>   per-actor Intelligence read from the OBJLIST at 0xb00 (objlist.md). The
>   spell/circle/reagent TABLE (§5) was derived by READING public U6 magic
>   references (understood, then written here in our own structure) — NOT
>   transcribed from any engine source.
> - References   : Ultima VI magic reference (ultimawebarchive nullneuron
>   ultima6/magic.html) + StrategyWiki "Ultima VI: The False Prophet/Magic",
>   read for the spell list, words of power, and reagent combinations. Casting
>   FLOW understood from Nuvie/ScummVM behaviour by observation (not copied).
> - Tools        : tools/magicprobe (obj_n↔name, objlist stat scan).
> - Validated by  : reagent/spellbook obj_n confirmed against the game's own
>   LOOK names (§2, CONFIRMED). Spell table cross-checked between two references
>   (probable). Mana model + casting effects NOT YET validated in-engine
>   (implementation pending); marked accordingly.
> - 2026-11-04: names, words and reagents checked against GAME.EXE 4.5's own tables; every spell now agrees with it (Lock, Mass Invis; Sleep Field, Poison Field, Mass Dispel and Explosion's reagents changed).
> - Confidence   : mixed — CONFIRMED (reagent/spellbook obj_n, mana-cost rule),
>   probable (spell/reagent table), guessed (exact save mana offset, per-spell
>   numeric effect magnitudes).
> - Last updated : 2026-11-03

---

## 1. Overview & engine placement

Magic is a shared `/engine` system driven by a data table (§5) plus a per-spell
effect dispatcher — the same shape as USE (usecode_use.md): the spell TABLE is
data; each spell's EFFECT is engine logic keyed by spell number. The player
triggers it with the **C)ast** verb (command-bar tile 401, already present).

Casting needs four things, all of which the engine already models or can:
- **Reagents** — ordinary stackable inventory objects (§2). Casting consumes
  them (engine/objects.c inventory, already stacks by obj_n + quality).
- **Magic points (mana)** — a per-actor pool (§3). New engine field.
- **The spell** — must be in the caster's spellbook (§4).
- **Circle access** — Intelligence gates which circles are castable (§3).

## 2. Reagents (CONFIRMED — obj_n from the user's data)

Eight reagents, contiguous object numbers, each a stackable object (LOOK plural
marker `\s`). Extracted via tools/magicprobe from LOOK.LZD + BASETILE:

| obj_n | reagent            | LOOK name (game's own)     |
|-------|--------------------|----------------------------|
| 65    | Black Pearl        | "black pearl"              |
| 66    | Blood Moss         | "bit of blood moss"        |
| 67    | Garlic             | "bulb of garlic"           |
| 68    | Ginseng            | "ginseng root"             |
| 69    | Mandrake Root      | "mandrake root"            |
| 70    | Nightshade         | "nightshade mushroom"      |
| 71    | Spider Silk        | "strand of spidersilk"     |
| 72    | Sulfurous Ash      | "bit of sulfurous ash"     |

A reagent bit-flag set (one bit per reagent) is the compact way to express a
spell's requirement (§5). Bit order used in this spec:
`0x01 BlackPearl · 0x02 BloodMoss · 0x04 Garlic · 0x08 Ginseng ·
 0x10 Mandrake · 0x20 Nightshade · 0x40 SpiderSilk · 0x80 SulfurousAsh`
(bit i ⇒ obj_n 65+i). Casting consumes **one** of each required reagent from
the caster's (or the party's) inventory.

Related magic objects (obj_n, from the same probe): **spellbook = 57**,
**spell = 58** (an individual known-spell object; see §4), Book of Circles = 61,
scroll = 152, staff = 78, lightning wand = 79, fire wand = 80.

## 3. Magic points (mana) & circle access

**Mana cost rule (CONFIRMED by reference):** casting a spell drains mana equal
to its circle number — a 1st-circle spell costs 1, a 5th-circle spell costs 5,
an 8th-circle spell costs 8. `mana_cost(spell) = circle(spell)`.

**Max mana formula (CONFIRMED by reference):** in U6 the **Avatar's** maximum
magic points = **2 × Intelligence**; a magic-using **companion's** = **Intelligence
/ 2** (Ultima Codex / character-attributes reference). We compute
`mana_max = is_avatar ? 2*int : int/2` from the save's Intelligence (OBJLIST
0xb00; Avatar int 15 → 30 mana). Implemented in engine/magic.c `magic_max_mana`.
(An earlier `max = int` model was WRONG for the Avatar — fixed.)
- OPEN QUESTION: the OBJLIST offset holding *current* mana was NOT found
  (tools/magicprobe + spellprobe: no per-actor array reads as current mana —
  candidates 0x1000/0x1300 are uniform; 0x1500 = 16/15/16/15 doesn't match
  2×int). So we load `current_mana = mana_max` (rested/full). See §8.
- SIMPLIFICATION: we do not model the caster/non-caster CLASS distinction —
  every companion gets int/2 (a non-mage should get 0). Honest but coarse.

**Circle access (MODELING DECISION, confidence guessed):** in U6 higher circles
require greater Intelligence. We gate with `castable_circle(actor) =
min(8, intelligence / 4)` as a first cut (int 15 → circles 1–3), refined once a
save with a high-Int mage is checked. A spell is castable iff
`circle(spell) <= castable_circle(caster)`.

## 4. The spellbook (which spells are known)

In U6, spells are **learned individually** (bought from mages) and recorded in
the **spellbook** (obj_n 57); casting requires the spell be in the book
(reference: Ultima Codex "Spellbook"). The known spells are the **spell** objects
(obj_n 58) the spellbook holds, each carrying a spell number in `quality`/`frame`.

**What a new game holds (the user's LZOBJBLK and CONVERSE, read 2026-11-03):** the Avatar carries no
spellbook, spell or reagent. The book lies in Lord British's castle at (300,367), holding spell
objects of quality 1, 3, 6, 7, 9, 11, 14, 18, 24, 28. Detect Magic, Dispel Magic, Heal, Help, Light,
Poison and Untrap have numbers in the table; 11, 14 and 28 name no spell and do nothing. Reagents lie
in containers around the world or are sold. Xiao, Nicodemus, Horance and Rudyom sell books (45-60
gold, NEW(player, 57, 0, 1)) and mages teach spells: "hands thee a piece of rune-covered parchment" is
NEW(player, 58, spell number, 1).

**Engine:** each book has its own spells, as in DOS (2026-11-04): the page shows the books the one played has readied, both hands joined.
- Taking a book from the map takes the spells it holds; a dropped book leaves with them; a save writes each one's under it.
- A parchment (58) stays in the pack until the player puts it in the book, as in the reference. M)ove,
  M again opens the pack, M steps to the next item, Enter puts the item in the book the leader has readied, or else the first it
  carries: "The spellbook already has this spell." and "Only spells can go into the spellbook." refuse and
  keep the item, and with no book it answers "Not possible". M)ove pushes a parchment on the map into a
  book on the map (usecode_use.md), and taking that book brings its spells. Mages' scripts sell a spell
  already in the book like any other.
- Casting needs a book readied by the caster (the Avatar; §6).
- MAGIC.REQ, or a headless scene that skipped the title, seeds the old test stock: every spell but
  Armageddon, 10 of each reagent, no book or level gate. It is not for play.

**Spell numbering (authentic U6 encoding):** spells are numbered by
`spell_num = (circle-1) * 16 + index_in_circle` (16 slots per circle, not all
used). So circle = `spell_num / 16 + 1`. This is the encoding to use for the
spellbook/save data so our numbers line up with the originals.

## 5. Spell table (probable — from references, our own structure)

All 80 spells, grouped by circle, with words of power and required reagents.
Reagent abbreviations: **BP** BlackPearl · **BM** BloodMoss · **Ga** Garlic ·
**Gi** Ginseng · **Ma** Mandrake · **Ni** Nightshade · **SS** SpiderSilk ·
**SA** SulfurousAsh. `idx` = index-in-circle (spell_num = (circle-1)*16 + idx).
"Effect" = the behaviour our engine will implement to match observed U6; each is
verified per-spell as implemented (status starts unimplemented `·`).

### Circle 1 (cost 1)
| idx | spell        | words         | reagents        | effect (planned) |
|-----|--------------|---------------|-----------------|------------------|
| 0 | Create Food    | IN MANI YLEM  | Ga Gi Ma        | add food object |
| 1 | Detect Magic   | WIS ORT       | Ni SA           | flag magic objects |
| 2 | Detect Trap    | WIS JUX       | Ni SA           | reveal traps near |
| 3 | Dispel Magic   | AN JUX ORT    | Ga Gi           | remove field/effect |
| 4 | Douse          | AN FLAM       | Ga BP           | extinguish light/flame tile |
| 5 | Harm           | AN MANI       | Ni SS           | ranged damage (small) |
| 6 | Heal           | IN MANI       | Gi SS           | restore HP to target |
| 7 | Help           | KAL LOR       | (none)          | return Avatar to Lord British |
| 8 | Ignite         | IN FLAM       | SA BP           | light a flame / torch |
| 9 | Light          | IN LOR        | SA              | temporary light radius |

### Circle 2 (cost 2)
| idx | spell        | words         | reagents        | effect (planned) |
|-----|--------------|---------------|-----------------|------------------|
| 0 | Infravision    | QUAS LOR      | Ni SA           | see in dark (light) |
| 1 | Magic Arrow    | ORT JUX       | SA BP           | ranged damage bolt |
| 2 | Poison         | IN NOX POR    | Ni BM BP        | poison target |
| 3 | Reappear       | IN YLEM       | SS BM BP        | reveal invisible |
| 4 | Sleep          | IN ZU         | Ni SS BP        | sleep target |
| 5 | Telekinesis    | ORT POR YLEM  | BM Ma BP        | operate distant object |
| 6 | Trap           | IN JUX        | SS Ni           | set trap on object |
| 7 | Unlock Magic   | EX POR        | SA BM           | open magically-locked door/chest |
| 8 | Untrap         | AN JUX        | SA BM           | disarm trap |
| 9 | Vanish         | AN YLEM       | Ga BM BP        | make object vanish |

### Circle 3 (cost 3)
| idx | spell        | words         | reagents        | effect (planned) |
|-----|--------------|---------------|-----------------|------------------|
| 0 | Curse          | AN SANCT      | SA Ni Ga        | debuff target |
| 1 | Dispel Field   | AN GRAV       | BP SA           | remove a field tile |
| 2 | Fireball       | POR FLAM      | SA BP           | ranged fire damage |
| 3 | Great Light    | VAS LOR       | SA Ma           | large light radius |
| 4 | Lock     | AN POR        | SA BM Ga        | magically lock door/chest |
| 5 | Mass Awaken    | AN VAS ZU     | Gi Ga           | wake sleeping party |
| 6 | Mass Sleep     | VAS ZU        | Gi Ni SS        | sleep foes in area |
| 7 | Peer           | VAS WIS YLEM  | Ni Ma           | show area map |
| 8 | Protection     | IN SANCT      | SA Gi Ga        | party defense buff |
| 9 | Repel Undead   | AN XEN CORP   | Ga SA           | drive off undead |

### Circle 4 (cost 4)
| idx | spell        | words         | reagents        | effect (planned) |
|-----|--------------|---------------|-----------------|------------------|
| 0 | Animate        | ORT YLEM      | SA Ma BM        | animate object |
| 1 | Conjure        | KAL XEN       | SS Ma           | summon creature ally |
| 2 | Disable        | AN VAS MANI   | Ni SS Ma        | disable target |
| 3 | Fire Field     | IN FLAM GRAV  | BP SA SS        | create fire field |
| 4 | Great Heal     | VAS MANI      | Gi SS Ma        | large HP restore |
| 5 | Locate         | IN WIS        | Ni              | sextant position |
| 6 | Mass Dispel    | VAS AN JUX ORT| Ni SS BP | dispel fields in area |
| 7 | Poison Field   | IN NOX GRAV   | Gi SS BP | create poison field |
| 8 | Sleep Field    | IN ZU GRAV    | Ga Gi | create sleep field |
| 9 | Wind Change    | REL HUR       | SA BM           | set the wind's direction |

### Circle 5 (cost 5)
| idx | spell        | words         | reagents           | effect (planned) |
|-----|--------------|---------------|--------------------|------------------|
| 0 | Energy Field   | IN SANCT GRAV | Ma SS BP           | create energy field |
| 1 | Explosion      | VAS POR FLAM  | Ma SA BP BM        | area fire damage |
| 2 | Insect Swarm   | KAL BET XEN   | BM SS SA           | summon insects |
| 3 | Invisibility   | SANCT LOR     | Ni BM              | make target invisible |
| 4 | Lightning      | ORT GRAV      | BP Ma SA           | ranged lightning damage |
| 5 | Paralyze       | AN XEN POR    | SS SA Ni BP        | paralyze target |
| 6 | Pickpocket     | POR YLEM      | BM SS Ni           | steal from target |
| 7 | Reveal         | AN SANCT LOR  | SS Ni Ma           | reveal hidden |
| 8 | Seance         | KAL MANI CORP | BM Ma Ni SS SA     | speak with dead |
| 9 | X-ray          | WIS YLEM      | Ma SA              | see through walls |

### Circle 6 (cost 6)
| idx | spell        | words         | reagents                | effect (planned) |
|-----|--------------|---------------|-------------------------|------------------|
| 0 | Charm          | AN XEN EX     | BP Ni SS                | charm target |
| 1 | Clone          | IN QUAS XEN   | SA SS BM Gi Ni Ma       | duplicate object |
| 2 | Confuse        | VAS QUAS      | Ma Ni                   | confuse target |
| 3 | Flame Wind     | FLAM HUR      | SA BM Ma                | fire area (wind) |
| 4 | Hail Storm     | KAL DES YLEM  | BM BP Ma                | area cold damage |
| 5 | Mass Protect   | VAS IN SANCT  | SA Gi Ga Ma             | party defense (area) |
| 6 | Negate Magic   | AN ORT        | Ga Ma SA                | suppress magic in area |
| 7 | Poison Wind    | NOX HUR       | Ni SA BM                | poison area (wind) |
| 8 | Replicate      | IN QUAS YLEM  | SA SS BM Gi Ni          | replicate object |
| 9 | Web            | IN DES POR    | SS                      | web target/area |

### Circle 7 (cost 7)
| idx | spell        | words         | reagents                | effect (planned) |
|-----|--------------|---------------|-------------------------|------------------|
| 0 | Chain Bolt     | VAS ORT GRAV  | BP Ma SA BM             | lightning chain |
| 1 | Enchant        | IN ORT YLEM   | SS Ma SA                | enchant object |
| 2 | Energy Wind    | GRAV HUR      | Ma Ni SA BM             | energy area (wind) |
| 3 | Fear           | QUAS CORP     | Ni Ma Ga                | fear target(s) |
| 4 | Gate Travel    | VAS REL POR   | SA BP Ma                | moongate travel |
| 5 | Kill           | IN CORP       | BP Ni SA                | high single-target damage |
| 6 | Mass Curse     | VAS AN SANCT  | SA Ni Ga Ma             | curse area |
| 7 | Mass Invis | VAS SANCT LOR | Ma Ni BM BP          | party invisibility |
| 8 | Wing Strike    | KAL ORT XEN   | BM SS Ma SA             | knock down flyers |
| 9 | Wizard Eye     | POR ORT WIS   | BM Ni Ma SA BP SS       | remote view (flying eye) |

### Circle 8 (cost 8)
| idx | spell        | words         | reagents                   | effect (planned) |
|-----|--------------|---------------|----------------------------|------------------|
| 0 | Armageddon     | VAS CORP BET MANI | (none)                 | every living thing dies but the Avatar and Lord British |
| 1 | Death Wind     | CORP HUR      | Ma Ni SA BM                | death area (wind) |
| 2 | Eclipse        | VAS AN LOR    | Ma SA Ni Ga BM             | darken world |
| 3 | Mass Charm     | VAS AN XEN EX | BP Ni SS Ma                | charm area |
| 4 | Mass Kill      | VAS CORP      | BP Ni Ma SA                | kill area |
| 5 | Resurrect      | IN MANI CORP  | Ga Gi SS SA BM Ma          | resurrect the dead |
| 6 | Slime          | VAS REL XEN   | BM Ni Ma                   | turn foes into slimes |
| 7 | Summon         | KAL XEN CORP  | Ma Ga BM SS                | summon creatures |
| 8 | Time Stop      | AN TYM        | Ma Ga BM                   | freeze game time |
| 9 | Tremor         | VAS POR YLEM  | BM SA Ma                   | earthquake area |

(Every circle has 10 spells = 80; the 16-slot circles leave the rest unused. Armageddon is the
circle's FIRST spell (spell_num 112, so Death Wind is 113 and the rest shift with it — the
reference's own numbering, which matters for a spellbook's spell objects and staff charges). It
needs no reagents and is never in a bought spellbook: the engine's demo seed leaves it unknown; a
save's spellbook holding spell 112 (or the all-spells 255) makes it castable. CORRECTED 2026-10-27:
an earlier revision of this table misspelled Wind Change as "Wild Change" and omitted Armageddon.)

## 5a. Fields (the field-tile layer)

Several spells create a persistent **field** on the ground — a tile that affects
whatever stands on it and lingers for a while. In U6 a field is a normal world
OBJECT (it has an obj_n and draws as its tile). Object numbers CONFIRMED from the
user's LOOK.LZD (tools/magicprobe):

| field           | obj_n | spell (circle)      | our effect (per turn on an actor over it) |
|-----------------|-------|---------------------|-------------------------------------------|
| Fire field      | 317   | Fire Field (4)      | fire DAMAGE (a moderate roll)             |
| Poison field    | 318   | Poison Field (4)    | small poison DAMAGE                        |
| Sleep field     | 320   | Sleep Field (4)     | SLEEP the actor (it skips its turns)      |
| Force field     | 51    | Energy Field (5)    | IMPASSABLE (blocks movement); no damage   |
| (electric field | 175/176 — alt energy variant, not used yet) |          |

**Model (engine, confidence: probable — behavior derived from U6, magnitudes
ours):** a small fixed-size list of active fields, each `{x, y, obj_n, kind,
turns}`. On every game turn the field layer:
- applies its per-turn effect to any actor (incl. the Avatar) standing on it —
  fire/poison via the combat damage roll, sleep via an actor "asleep" counter;
- decrements `turns` and removes the field at 0 (fields are temporary).
An **energy/force field** additionally marks its tile impassable (folded into the
`build_blocked` bitmap that `world_is_passable` consults), so actors cannot walk
through it. Fields draw in the map compose UNDER actors (an actor in a fire field
shows on top of the flames). **Dispel Field** (circle 3, AN GRAV) removes the
field on the targeted tile.

OPEN / simplified (honest): each field spell currently drops ONE tile at the
targeted adjacent square (U6 spreads some fields into a short line/wall — future);
poison is modeled as damage (no lingering poison STATUS yet); durations are a
fixed guess (~12 turns), not the original's exact decay.

## 5b. Status effects, area spells and mind spells (actor conditions)

> Derived from: READING the U6 scripts in Nuvie's `ultima.dat` (`u6/actor.lua`: actor_int_check,
> actor_update_flags, spell_put_actor_to_sleep / spell_poison_actor / spell_charm_actor /
> spell_kill_actor, actor_remove_charm, the creature table's immunity columns; `u6/magic.lua`:
> magic_remove_actor_enchantments and the result messages; the per-spell scripts under
> `u6/magic/circle_0N/`), and `core/effect.cpp` ExpEffect (the explosion's 16 target cells) — to
> understand; re-expressed. Immunity flags live in engine/monsters.c (`monster_flags`).
> Confidence: probable (the reference's own scripts; the intelligence contest's direction is our
> reading, see below). Validated by MAGICFX.REQ (§9).

Conditions are per-actor FLAGS (not timers): a condition stays until its own chance roll ends it,
a cure clears it, or the actor dies. Each game turn, for every living actor:

| condition | ends when (one roll per turn) | while active |
|---|---|---|
| protected | 1-in-64, then only if the actor's int ≤ d30 (the clever keep it) | halves damage taken (combat.md), poison does no harm |
| cursed | 1-in-16, then only if int ≥ d30 | −3 to strength and intelligence in every check (actor_int_adj / actor_str_adj), −3 armour class (combat.md) |
| charmed | 1-in-8, then only if int ≥ d30 | the actor fights FOR the party (ally AI); its hostility is restored when the charm ends |
| paralyzed | 1-in-4, then only if str ≥ d30 | skips its turns |
| asleep | 1-in-16 (not a scheduled sleeper) | skips its turns |
| poisoned | never by itself (cured by the red potion, a healer, Dispel Magic / Mass Dispel) | 1-in-8 per turn: loses 1 HP (none while protected) |
| invisible | 1-in-64, then only if int ≤ d30 (never while an invisibility ring is worn) | not drawn, not targeted |

`d30` = a roll of 1..30. Strength/intelligence are the cursed-adjusted values (min 1).

**The intelligence contest** (sleep, charm, kill, fear, confuse, repel undead): the spell TAKES when
`caster_int/2 + 15 − target_int > d30`, else "Failed". That is the reference's expression read
as its comment names it ("the defender lost"); its callers appear to use the opposite sense,
which would make a wiser caster LESS effective — we follow the arithmetic's evident intent
(probable; a disassembly of the original would settle it).
**Poison's roll** is strength-based: poisoned when `(target_str/2 + 30 − caster_int)/2 ≤ d30`.

**Immunities** (the creature table): sleep-immune (insects, slugs, worms, gazers, winged
gargoyles, squids, scorpions, ants, mimics, reapers, vines, undead, daemons, hydrae, wisps),
poison-immune (rats, alligators, rot worms, mimics, ghosts, skeletons), undead = immune to the
death spells (ghosts, skeletons, daemons, wisps), fire-immune (dragons, drakes, hydrae, daemons,
ghosts, skeletons, wisps), double fire damage (slugs, slimes, corpsers, reapers, vines, flowers,
rot worms), flies = tremor-immune (bats, birds, insects, mongbats, gazers, winged gargoyles,
dragons, drakes, daemons, the balloon). "No effect" is printed for an immune target.

**Single-target spells** (aim a direction; the first actor within 5 tiles, as §6):
- **Poison** (2): the strength roll, else Failed. **Sleep** (2): the contest. **Paralyze** (5),
  **Curse** (3): always take. **Charm** (6): the contest; a hostile becomes the party's ally —
  "X is charmed."; charming an already charmed actor frees it. (u6e asks for a hostile target:
  "Failed" — the reference's only failure word) **Kill** (7): undead → No effect; the contest; else the target dies
  outright and the caster earns its experience. **Dispel Magic** (1): clears sleep, poison,
  paralysis and charm on the target — Success / No effect. It may be aimed at the caster's own
  tile (Enter at "Cast-(dir)?", or a click on the caster) to clear the caster's own conditions (the
  reference picks its target with a cursor that can rest on the caster; probable). The other single-target spells still
  ask for a direction there.
- Messages follow the reference: "Success", "Failed", "No effect"; the condition lines
  only what the reference prints (§5g): Poison / Sleep succeed silently, "X is paralyzed.", Curse
  "Success", "X is charmed.".

**Area spells** (aim a direction: the centre is the first actor within 5 tiles, else the last
open tile of the line; the AREA is the 5×5 square around the centre minus its four corners — the
reference's explosion flies to 16 target cells and hits everything on those paths = 21 cells):
- **Mass Sleep** (3) sleeps every actor in the area (each its own contest + immunity);
  **Mass Awaken** (3) wakes them; **Mass Curse** (7) curses them all; **Mass Charm** (8) charms
  the hostiles (each its own contest); **Mass Dispel** (4) clears their enchantments;
  **Mass Kill** (8) kills each non-undead that loses the contest and sets off any powder keg in
  the area. The Avatar is never in an area (the aim starts beside the caster); party members in
  the area take Mass Sleep / Curse / Awaken / Dispel but not Mass Kill (u6e deviation).
- All end with "Success".

**Mind spells on everyone within 5 tiles of the caster** (an 11×11 square, no aim):
- **Fear** (7): every non-party, non-undead actor that loses the contest FLEES (the reference's
  timid worktype 0x13): a hostile runs from the party, a townsperson avoids it.
- **Confuse** (6): every EVIL actor that loses the contest turns GOOD and fights for the party
  — for good (the reference's alignment change is permanent).
- Repel Undead (3) and Tremor / Slime / Wing Strike (8) use the same square — §5f.

**Allies**: a charmed or confused actor runs the allied AI (hunts the nearest hostile, else keeps
by the Avatar; §5c's summons do the same); hostiles still single out the party.

## 5c. Summoning (spawning creatures)

Some spells create NEW creature actors in the world. A summoned creature is a
full actor (appearance/position/HP/alignment/AI) placed in a free actor slot,
with a LIFETIME after which it vanishes (U6 summons are temporary). Creature
object numbers CONFIRMED from the user's LOOK.LZD (tools/magicprobe):

| creature   | obj_n | used by            |
|------------|-------|--------------------|
| giant rat  | 343   | Conjure            |
| insects    | 344   | Insect Swarm       |
| giant bat  | 345   | Conjure (alt)      |
| gremlin    | 354   | —                  |
| daemon     | 368   | Summon (high tier) |
| mongbat    | 373   | —                  |

| spell        | circle | effect                                             |
|--------------|--------|----------------------------------------------------|
| Conjure      | 4      | summon ONE **allied** creature that fights hostiles|
| Insect Swarm | 5      | summon several allied insects                      |
| Clone        | 6      | duplicate the TARGET actor (same obj_n + alignment)|
| Summon       | 8      | summon a powerful **allied** creature (a daemon)   |

**Model (engine, confidence: probable — creatures/allegiance from U6, the AI +
lifetimes are ours):** a summon flag + lifetime counter per actor slot. An
**allied** summon runs an AI that mirrors the hostile one — it pursues the
nearest HOSTILE and attacks it (reusing the party-attack primitive); a **hostile**
summon (e.g. Clone of an enemy) reuses the existing assault AI. Each turn the
summon's lifetime decrements; at 0 the creature vanishes (slot freed). Summoned
allies are drawn by the normal actor pass and are NOT targeted by the party's own
attacks.

OPEN / simplified: Clone copies the target's type/alignment (not its exact HP);
allied summons fight but do not follow a schedule; the summoned creature TYPES are
a reasonable choice, not the originals' exact random tables; counts/lifetimes are
guesses.

## 5d. Travel (teleport spells)

Two spells relocate the whole party across the map:

| spell        | circle | destination                                            |
|--------------|--------|--------------------------------------------------------|
| Help         | 1      | recall to **Lord British** (the "return home" word)    |
| Gate Travel  | 7      | a **moongate** destination (a moonstone position)      |

**Model (engine):** a single `teleport_party(x,y)` primitive — it sets the world
position, re-seeds each party member onto a free tile beside the new spot, and
invalidates the incremental-scroll cache so the map does a FULL rebuild at the
destination (a jump is not a 1-tile scroll).
- **Help** (KAL LOR, no reagents; from READING the reference's `circle_01/help.lua` and the
  `actor.lua` helpers it calls, re-expressed, 2026-10-31) is Lord British reviving the party, the
  same recall as the Avatar's death (combat.md §3d) without the darkness, the karma or the losses:
  1. every dead companion stands up again (not after Armageddon; experience × karma / 100 below
     karma 98, as any resurrection);
  2. the whole party is healed: full HP, no poison, sleep, paralysis or charm;
  3. the party is moved to Lord British's throne room, (307,352) on the surface, from any level;
  4. the clock runs on in whole hours until he sits on his throne (an hour in 8..11 or 14..17),
     and the townsfolk stand where that hour puts them.
  It prints nothing past the spell words. Foes keep hunting, combat mode stays, readied things
  stay readied. A horse is dismounted and a vessel left where it is, as at the Avatar's death
  (the reference moves the party alone). Until 2026-10-31 u6e's Help only moved the party beside
  Lord British, wherever he stood, so a dead companion stayed dead.
- **Gate Travel** reads the 8 **moonstone positions** the save stores in the
  OBJLIST at **0x1c1b** — 8 records of `[x u16][y u16][z u16]` little-endian
  (CONFIRMED by inspecting the user's save: coordinates spanning Britannia,
  e.g. (935,262), (503,358), (159,942)). It gates a red moongate to the next
  surface (z=0) moonstone. This grounds the destinations in real save data
  rather than hardcoded coordinates.

OPEN / simplified: Gate Travel cycles the moonstone list rather than choosing by
the authentic moon-phase schedule; Help assumes Lord British is loaded (he is, in
the sample save); no gate ANIMATION.

## 5e. Detection / utility spells

A set of information + remote-action spells that reuse existing engine systems:

| spell        | circle | effect                                                    |
|--------------|--------|-----------------------------------------------------------|
| Detect Magic | 1      | senses nearby MAGIC LOCKS (report a count)                |
| Telekinesis  | 2      | operate a DISTANT object (a ranged Use — open a door...)  |
| Locate       | 4      | report the party's map COORDINATES (a sextant reading)    |
| X-ray        | 5      | SEE THROUGH WALLS for a while (the LOS flood shows all)    |

**Model (engine):**
- **Locate** prints the world position — the sextant.
- **Detect Magic** scans objects within a radius for magic-lock tiles (door frames
  13/15, chest frame 3 — see §5) and reports how many magical auras are near.
- **Telekinesis** line-scans the aim direction for the first object and applies the
  normal Use dispatcher to it from afar (docs/rules/usecode_use.md).
- **X-ray** sets a timer; while it runs, the line-of-sight flood
  (`compute_visibility`) marks every cell visible, so tiles behind walls draw
  instead of being blacked out. The timer decays each turn.
- **Peer** (circle 3) opens a modal MAP OVERVIEW: an 88x88-tile region around the
  Avatar drawn into the map window at 2 px/tile (each tile = its terrain tile's
  centre pixel; actors are dots, the Avatar a yellow cross). Any key closes.
- **Wizard Eye** (circle 7) is the same overview but PANNABLE — arrow keys scout
  the map (the view centre moves toroidally), Esc ends. It's an overview-based
  remote view rather than a full-res flying eye (which would need decoupling the
  compose_map camera + lighting/LOS from the Avatar — deferred as a bigger lift).
- **Invisibility** (circle 3) sets a per-actor invisibility timer `g_ainvis[]`
  (aimed actor, else self); **Mass Invis** (circle 7) does the whole party.
  compose_map skips drawing invisible actors; the timer decays each turn.
- **Reveal** (circle 5) clears `g_ainvis[]` for every actor within 5 tiles.
  Validated on TT (MAGIC.BIN): Invisibility on an adjacent NPC → ainvis=60 (it
  vanishes) → Reveal → ainvis=0 (it reappears); Peer/Wizard Eye render the overview.

OPEN / simplified: Detect Magic only senses magic LOCKS (no dedicated magic-item
flag yet); a true full-res Wizard Eye (flying-eye camera) is still a future lift.

## 5f. The remaining spells — utility, objects, traps, bolts, winds, the doom spells

> Derived from: READING the per-spell U6 scripts in Nuvie's `ultima.dat` (`u6/magic/circle_0N/*.lua`,
> 33 scripts + `armageddon.lua`), their helpers in `u6/magic.lua` (the wind spread, the result
> messages), `u6/actor.lua` (spell_hit_actor, spell_take_fire_dmg, actor_hit_msg, actor_tile_dmg's
> web rule, slime_update_frames, actor_use_effect = the trap effects, the creature table's columns)
> and `u6/init.lua` (the vanished-object save slot, the projectile offset table, use_keg,
> explode_surrounding_objects), the reference's `core/anim_manager.cpp` HailstormAnim / WingAnim and
> `core/effect.cpp` (hail / wing damage), `core/magic.cpp` (the storm's "No magic at this time!"),
> `gui/widgets/map_window.cpp` (infravision draws actors over the darkness; an eclipse is dungeon
> darkness) and `core/weather.cpp` is_eclipse — to understand; re-expressed. The tileflags3 "Ge"
> bit (tileflag.md bit3) is the reference's Vanish/Replicate flag.
> Confidence: probable (the reference's scripts; a few magnitudes are the reference's own choices).
> Validated by SPELLS.REQ (§9).

**Aiming.** Object spells aim a direction: the target is the first OBJECT within 5 tiles along it
(the line stops at a sight-blocker; a door is itself the object). Actor spells take the first actor
within 5 (§6). "Location" spells use §5b's centre rule (the first actor within 5, else the last open
tile). Messages follow the reference: "Success", "Failed", "No effect", "Blocked!".

**Damage spells** share one hit routine (the reference's actor_hit + actor_hit_msg): the victim
loses the roll; a slain foe earns the caster its experience ("Killed <name>!"); a survivor prints
its state by hp×4/max — 0 "critical!", 1 "heavily wounded.", 2 "lightly wounded.", 3 "barely
wounded." — and a struck townsperson turns hostile (yells for help). Party members in an area ARE
hit (the reference's explosion, winds, hail and chain bolt spare nobody), but the CASTER is never
struck by its own spell. A party member reduced to 0 HP dies as combat.md §3d says (until
2026-10-31 the Avatar was never reduced below 1 HP: there was no Avatar-death sequence).

| spell (circle) | effect |
|---|---|
| Detect Trap (1) | aim an object; when the caster's int ≥ d30 and the door (297-300) / chest (98) holds a trap (a child "effect" object 337): "It's trapped.", else "No trap." |
| Douse (1) | aim a fire device — candle 122, candelabra 145, fireplace 164, a lit map torch 90, a flaming brazier 206 (frame 1): a lit frame (1 / 3) goes down one (a torch burns out): "Success"; else "No effect" |
| Ignite (1) | the same devices unlit (frame 0 / 2) go up one (a torch is lit); an UNLIT powder keg 223 is lit (its 3-turn fuse): "Success"; else "No effect" |
| Infravision (2) | timer 1 = 20 turns: creatures on dark but visible cells are drawn in full light |
| Reappear (2) | the vanished object (below) is recreated on the tile beside the caster in the aimed direction (free, passable), as a temporary object: "Success"; nothing vanished / no room: "No effect" |
| Trap (2) | aim a door / chest: adds a trap (child 337) unless it has one: "Success" / "No effect" |
| Untrap (2) | removes the trap: "Success" / "No effect" |
| Vanish (2) | aim an object whose tile carries tileflags3 bit3: it is removed from the map and REMEMBERED (objlist 0x1c13: obj_n | frame<<10 — see objlist.md on the reference's undecodable frame encoding) for Reappear: "Success"; else "No effect" |
| Lock (3) | a CLOSED door (frame 4-7) → +8 "magically locked!"; an open one: "Can't (Un)lock an opened door"; a chest frame 1 / 2 → 3 "magically locked!"; else "No effect" |
| Repel Undead (3) | every creature within 5 of the caster that the table marks repellable (ghosts, skeletons, daemons) and that loses the §5b contest FLEES (the reference's timid worktype): "Success" |
| Animate (4) | aim a READIABLE object (equipment.md §3): it becomes an allied creature of its own type on its tile (a temporary ally, §5c): "Success"; else "No effect" |
| Disable (4) | aim an actor; it loses the contest or "Failed"; damage = its hp − 1 (it survives at 1 HP: "critical!") |
| Wind Change (4) | "Direction-": the wind now blows FROM that direction (wind.md); the echo "North." |
| Explosion (5) | aim a location; every actor in the 21-cell area takes FIRE damage 1..20 (double for the fire-weak, none for the fire-immune — §5b's flags); then every keg within 2 of the centre explodes and every door that is not magically locked is blown up (usecode_use.md's keg blast) |
| Lightning (5) | aim an actor: 1..30 damage |
| Pickpocket (5) | aim an actor: its first carried (not readied, not a container) item passes to the party pack — "<name>." and "Success" — and the Avatar loses 5 karma; a bare NPC: "No effect" |
| Seance (5) | aim a dead body (339 / dead gargoyle 341 / dead cyclops 340) whose quality names an actor: that actor's conversation opens; the ghost (352, actor 91) gets its talk flag 7 and talks; else "No effect" |
| Flame / Poison / Energy / Death Wind (6/6/7/8) | aim a location: the wind strikes a line of FIVE cells through the target — the target and the cells one and two tiles out on both sides. For an orthogonal aim the line runs at right angles to it; for a diagonal aim it is the two orthogonal directions flanking the aim (the reference's projectile offset table reduced to the eight aims). Everyone on those cells but the caster: Flame = fire damage 1..20 (flags as Explosion); Poison = §5b's strength roll; Energy = 1..30 damage; Death = §5b's kill (undead immune, the contest) |
| Hail Storm (6) | aim a location: 10..29 hailstones fall on the 5×5 around it (each at target + two triangular rolls, (d30+d30−24)/16 tiles on each axis); a stone landing on an actor does 1 damage: "Success" |
| Negate Magic (6) | timer 13 (the storm) = 20 turns: "No magic at this time!" — nobody can begin a spell while it runs (the storm cloak holds the same timer, equipment.md §8); foes cannot fire their charges either (combat.md §7b) |
| Replicate (6) | aim an object with tileflags3 bit3: a temporary copy lands on a random free tile within ±5 of the caster (8 tries): "Success" / "Failed"; else "No effect" |
| Web (6) | aim a location: webs (object 53) fill the free cells of the 3×3 around it: "Success". An actor standing on a web (not a giant spider 361) each turn: str < d30 → paralyzed, "<name> is stuck in a web!" |
| Chain Bolt (7) | aim an actor: 1..30 damage, then up to 7 jumps — each to the nearest other living actor within 4 of the CASTER (distance from the last victim, ties by coin), 1..30 each; stops when nobody is left (the caster is never a link, see above) |
| Enchant (7) | the party must carry the staff (78): "with:" then a second spell menu (not Enchant itself); the staff takes a charge (object 336, quality = the chosen spell's number); 10 charges: "It's full". Only Enchant's own reagents and mana are spent |
| Wing Strike (7) | every EVIL, visible actor within 4 of the player is struck for 1..20; none: "No foes." |
| Eclipse (8) | timer 15 = 20 turns: the sun is gone — on the surface the ambient is NIGHT's until it ends (the reference: eclipse = night = the dungeon minimum), the sky strip shows the eclipsed sun (tile 363) and no moons (lighting.md §2b), NPC torches light at the next hour tick (usecode_use.md NPC torches) |
| Resurrect (8) | aim a body (339 / 340 / 341 / 354, or a dog 349 / horse carcass 134) whose quality names an actor below the temporary range: the actor stands up on the body's tile at 1 HP and the body is gone (its contents spill to the tile first); quality 0: "The corpse is too old!"; after Armageddon: "No effect" |
| Slime (8) | every EVIL land creature within 5 of the player that loses the contest BECOMES a slime (375) — appearance and type change, hit points stay; slimes beside each other join frames (bits N/E/S/W) |
| Time Stop (8) | timer 14 = 10 turns: nothing but the Avatar moves — no creature (nor companion) steps, no field, timer or condition ticks (the clock still shows the turns passing — deviation) |
| Tremor (8) | every EVIL creature within 5 of the caster that does not FLY takes 1..30: "Success" (no screen shake yet) |
| Armageddon (8) | every actor but the Avatar (1) and Lord British (5) dies, bodies and all; NPCs will not talk afterwards and Resurrect fails; not saved (the reference's flag resets on load) |

**Bodies.** A slain NPC (a real actor, not a summon) leaves a dead body — 339, or a dead gargoyle
341 for the winged/plain gargoyle types — with quality = its actor number, so Seance and
Resurrect have something to work on. A real NPC's body is SAVED (a new record holding its inventory;
usecode_use.md "Created and destroyed objects persist", 2026-10-28); a temporary creature's is not.
**Traps.** Opening a trapped door or chest fires the trap on the Avatar and removes it: one of
Acid (1..20 damage), Poison, Bomb (1..20 fire damage to everyone within the 21-cell area) or Gas
(everyone within the area poisoned) — the reference's actor_use_effect. The pristine save ships six
such traps (chests and steel doors). A trap the Trap spell adds is SAVED as a contained record under
its holder, and Untrap deletes it (2026-10-28). Webs are saved too; fields and Reappear / Replicate
copies stay temporary, as the reference flags them.
**The vanished object** is saved (objlist 0x1c13) and reloaded; the timers 1 / 13 / 14 / 15 are
saved in the 0x1c03 timer block.

## 6a. Casting as DOS has it (2026-11-04; overrides the rest of §6 and §5 where they differ)

> Provenance: READING GAME.EXE 4.5 (notes in ~/src/u6e-re/REVERSE.md, "DOS Cast"): the Cast routine, the spell
> dispatch and the routines it calls. Re-expressed, no code transcribed.

- The lines are the spell's name, its words in quotes (`Light`, `"In Lor"`), then what comes of it. Choosing nothing
  prints `nothing`.
- The checks, in order: `Your level is not high enough.`, `Not enough magic points.`, `No Reagents.`. Then the reagents,
  the mana and the time (10 + 3 x circle) are spent **before** the spell does anything: a spell with nothing to work on
  or a cancelled crosshair gets none of it back.
- A click on the map while the page is open casts the chosen spell at that tile.
- A spell's target is a crosshair (Enter or a click; Esc answers `nothing`), starting on the caster, the very tile under
  it, not a direction:
  - `On whom: `, then the name or `nothing` and `No effect`: Disable, Sleep, Magic Arrow, Fireball, Lightning, Chain Bolt,
    Poison, Invisibility, Dispel Magic, Harm, Heal, Curse, Protection, Great Heal, Paralyze, Charm, Clone, Kill, Resurrect,
    Pickpocket, Seance. The caster's own tile is the caster: Heal and Great Heal take whoever the crosshair is on.
  - `On what: `: Douse, Vanish, Dispel Field, Animate, Replicate, Ignite, Detect Magic, Detect Trap, Trap, Unlock Magic,
    Untrap, Lock, Telekinesis.
  - `Location: `: Reappear, the Mass spells (Protect and Invis too: everyone in the area, not the party), Explosion, the
    four Fields, the four Winds, Web, Hail Storm.
  - `Direction-`: Wind Change; an arrow answers at once.
  - Something in the way of the line to the tile: `Blocked!`. Range is the window.
- No target: Create Food, Light, Great Light, Infravision, Locate, Peer, X-ray, Wizard Eye, Repel Undead, Reveal, Confuse,
  Fear, Slime, Tremor, Wing Strike, Conjure, Summon, Insect Swarm, Negate Magic, Time Stop, Eclipse, Help, Armageddon.
- An area spell strikes everyone in it, the caster and the party too (Mass Kill as well).
- Harm: a save, 1-10. Magic Arrow: (Int/2 + 30 - target Dex)/2 against 1-30, 1-10. Fireball: no save, 1-20, doubled or
  none by creature. Heal 1-30, Great Heal to full, `No effect` on the dead.
- Each field spell makes one field on one tile: fire 317, poison 318, sleep 320, Energy Field the protection field 319.
- Conjure: one creature, 4 chances in 16 a troll (371), 3 a giant rat (342), 3 a spider (361), 3 a giant bat (344), 3 a
  snake (358); Insect Swarm: eight insects (343); Summon: one daemon (367), which fails its save and turns on the caster
  with `Hostile daemon summoned.` Each lands on a free tile within 5 of the caster, eight tries.
- Telekinesis: a crank or lever works from afar; a weightless thing `Not Possible`; anything else asks `Direction-` and
  moves one tile.
- Gate Travel: `To phase `, a stone's number 1-8, Enter: to where that moonstone lies; a stone that lies nowhere does nothing.
- Wizard Eye: the view flies with a wizard eye (object 52), 40 moves at most, until Esc or rock (tile 252 up). `Done`.
- Maximum mana by what the actor is: the Avatar (410) 2 x Int, a mage (378) Int, a swashbuckler (377) or musician (386)
  Int/2, anyone else none.

## 6. Casting flow (engine algorithm)

C)ast, then the caster (default = Avatar) and the spell are chosen; U6 selects a
spell by its **words of power** (typed syllables) or from the spellbook list. Our
engine flow:

1. **Select spell.** C)ast (the 'c'/'C' key or the Cast command-bar icon) opens the
   SPELLBOOK PAGE over the party panel (reference: ScummVM Nuvie's spell view, read
   2026-11-03; art NEWMAGIC.BMP, background.md). With no spell known: "No spells in
   the spellbook!".
   - Page: the 144x82 scroll at screen (168,6), the 16-pixel strip under it holding
     the left and right buttons (tiles 412, 413) at (200,88), (216,88) and the circle
     as "3RD" at (272,88) over "level" at (264,96).
   - One circle at a time, known spells in spell order, 8 to a page (10 per circle
     means a second page). Rows at y = 14 + 8*row: glyph 26 marks the selected row at
     x=176, the name at x=184, and in a two-digit field at x=288 how many casts the reagents allow: the
     smallest count among the reagents needed, 1 for a spell that needs none.
   - Every known spell is listed; one above the caster's reach is refused by the gate.
   - The selection stays from one cast to the next (the reference keeps it in the book).
   - Up/Down walk the list; past either end of a page they act as Left/Right. Right
     goes to page 2, then the next circle with spells; Left to page 1, then the
     previous circle with spells, on its second page if it has one. Enter casts the
     selection, Esc cancels. Mouse: a click on a row selects and casts it, on a button
     acts as Left/Right, elsewhere cancels. (The reference also casts at a clicked map
     tile; ours cancels.)
   - The message scroll shows `Cast-`. WORDS OF POWER: a letter types that rune's syllable
     (a = An, m = Mani, up to 4, shown after it: `Cast-In Mani`; Backspace removes one). Enter with syllables typed casts
     the spell of those words: In Mani is i, m. Words of no spell: "That spell is not in
     thy spellbook!".
   Self/party spells cast immediately; offensive spells arm a direction
   ("Cast X where?") resolved by the existing verb→direction flow; Enter there aims at the
   caster's own tile (2026-11-01; only Dispel Magic acts on it, §5b). Host tests:
   pass spell_num directly. (Combat-mode toggle moved off 'c' to 'b' + the
   Combat command-bar icon.)
2. **Gate checks**, in order, each with its own failure message. Before the page opens: a spellbook
   readied → else "No spellbook is readied."; a spell in it → else "No spells in the spellbook!". At the cast:
   - spell known (in spellbook)          → else "That spell is not in thy spellbook!"
   - `circle(spell) <= caster's level`    → else "Your level is not high enough."
   - `current_mana >= mana_cost`          → else "Not enough magic points."
   - all required reagents present        → else "No Reagents."
3. **Target** if the spell needs one (direction/tile/actor), reusing the verb→
   direction prompt already in gamedemo (do_verb pattern).
4. **Consume**: subtract one of each reagent from inventory; `current_mana -=
   mana_cost`.
5. **Effect**: dispatch by spell_num to the effect handler (§5 column). Show the
   result line in the message scroll.

Fizzle/failure consumes nothing (checks happen before consume). Reagent/mana are
consumed exactly once, only when all gates pass.

## 7. Per-game deltas (RuleSet hooks)

Add ONE hook to the RuleSet struct (engine/ruleset.h):
`bool (*on_cast_spell)(GameState*, int caster, int spell_num, Target*);`
- U6 default = this document's table + dispatcher.
- **Savage Empire**: no reagent magic; "spells" are tribal SKILLS/shaman rituals
  — override the table + effect set, keep the mana/verb machinery.
- **Martian Dreams**: no Britannian magic; psychic powers / era gadgets —
  override likewise. The spell TABLE and reagent list are U6-only data; the
  casting *machinery* (mana pool, gate checks, dispatcher) is shared.

## 5g. Messages — the reference's words (AUDIT 2026-10-28, checklist item 16)

> Provenance: READING every one of the reference's 80 U6 spell scripts (`magic/circle_XX/*.lua`), its
> `magic.lua` result helpers and `core/magic.cpp` cast gate — to understand what each prints; re-expressed.

The framework prints only five result words: **Success**, **No effect**, **Failed**, **Blocked!**,
**Not possible**; a missing target after the "On whom: / On what: / Location: " prompt prints
**nothing**. Hit damage speaks through the combat messages ("X hits Y", "Killed X!"). Most spells
print NOTHING of their own; the exceptions and the words we now use:

| Spell | Reference | Was (u6e) |
|---|---|---|
| cast gate | `That spell is not in thy spellbook!` · `Not enough magic points.` · `No Reagents.` · `No magic at this time!` | "Thou dost not know that spell." · "Not enough magic points!" · "Thou hast not the reagents!" |
| no target | `nothing` | "<Spell>: no target." |
| Heal, Great Heal | Success (a dead target: No effect) | "Heal! +N HP" |
| Light, Great Light, Infravision, Eclipse, Time Stop, Negate Magic, Peer, Help, Armageddon, Slime, Summon, Seance, Fireball, Explosion, Flame / Poison / Energy / Death Wind, Chain Bolt, Lightning | nothing (the effect is the message) | flavour lines, "Success" |
| Create Food | `Create N food.`, N = 1..10 portions | one portion, "Thou hast conjured food!" |
| Unlock Magic | `unlocked!` / No effect | "The magical lock dissolves!" / "There is no lock…" |
| Fire / Poison / Sleep / Energy Field | Success / Not possible | "<Spell>!" |
| Dispel Field | Success / No effect | "The field vanishes." / "There is no field there." |
| Poison, Sleep | Failed / No effect; success silent | "X is poisoned." / "X falls asleep." |
| Paralyze | `X is paralyzed.` | same |
| Curse | Success | "X is cursed." |
| Charm | `X is charmed.` / Failed | same; our non-hostile refusal now says Failed |
| Protection, Mass Protect | Success | "<Spell> shields the party!" |
| Conjure | Success / Failed | "Conjure!" / "No room to summon." |
| Insect Swarm | Success | "Insect Swarm!" |
| Clone | Success / No effect | "Cloned the X!" / "No room to clone." |
| Gate Travel | nothing either way (the reference only plays a failure sound) | three flavour lines |
| Locate | `<lat>{N, <lon>{E` — `{` is the degree sign in U6's font; lat = \|45 − y/8\|, lon = \|38 − x/8\| | "Thou art at (x,y)." |
| Detect Magic | `It's a <kind> potion.` (potions by frame: awaken, cure, heal, poison, sleep, protection, invisibility, xray vision, unknown) · `It's magical.` (a magical TILE) · `It shows a charge of <spell>.` (a staff) · `It's not magical.` · no object: No effect | "Thou sensest N magical auras." |
| X-ray, Wizard Eye | `Done` | flavour |
| Invisibility | Success / No effect | "Thou fadest from sight." |
| Mass Invis, Reveal | Success | flavour |
| Disable | Failed (the hit message otherwise) | "Success" |
| Pickpocket | the item's name, then Success / No effect / `can't carry!` | "<item>. Success" |
| Tremor | Success · Wing Strike: `No foes.` | same |
| Wind Change | the direction letter after `Direction-` | same |

Kept as ours (UI prompts, not script output): our direction prompts "<Spell>: aim a direction." stand in
for the reference's target prompts, because u6e aims every spell by direction; Telekinesis keeps its own
mechanics; Enchant's menu is the C)ast menu. The magical-tile set for Detect Magic is our own table of the
80 tile numbers the reference lists (monsters/objects: wands, rings, amulets, moonstones, the Codex…).
Status: VERIFIED 2026-10-28 — SPELLS.REQ 1-c and MAGICFX.REQ 1-7 rerun identical on TT + 4 MB Falcon
with the new words; host suites + make check pass.

## 5h. Heal-type spells and their targets (2026-10-31)

> Provenance: READING the reference's `magic/circle_01/heal.lua`, `circle_04/great_heal.lua`,
> `circle_03/protection.lua`, `circle_05/invisibility.lua`, `circle_06/mass_protect.lua`,
> `circle_07/mass_invisibility.lua`, `circle_01/detect_magic.lua` and `magic.lua`
> (select_actor_with_projectile: the "On Whom: " cursor, range 7) — to understand; re-expressed.

- **Heal** adds 1-30 HP (uniform) to a living target, never past its maximum; **Great Heal**
  sets it to the maximum. A dead target: "No effect". (Until 2026-10-31 u6e used placeholder
  amounts, +20 and +40.)
- The reference picks every one of these targets with its cursor. u6e keeps its direction
  targeting (deviations.md), and for the spells that would need the cursor on a FRIEND:
  Heal / Great Heal land on the most-wounded living party member (the caster when nobody is
  hurt); Protection and Mass Protect shield the whole party; Invisibility hides the caster or
  the first actor in an aimed direction; Mass Invis hides the party.
- **Detect Magic** aims a direction (the first object along it, 5 tiles), like Lock. It
  was missing from the aimed spells, so from the C)ast menu it could never reach an object
  (found 2026-10-31 while reviewing the targets against the reference).

## 8. Open questions / to validate

- **Max mana** (§3): RESOLVED — 2×int (Avatar) / int/2 (companion), from the
  reference; loaded from the save's Intelligence (magic_max_mana).
- **Current-mana save offset** (§3): RESOLVED — **objlist 0x13f1** (1 byte per
  actor, sequential; from Nuvie ActorManager, read to understand). mkworld ships it
  (pack U6WC, now 5 B/actor) and the engine loads `g_amana` from it (0xFF sentinel =
  "unknown" → fall back to max). Validated on TT: patching the Avatar's 0x13f1 to 12
  makes the ztats show **MP 12/30** (current, not the 30 max). At a fresh game start
  everyone is rested, so the raw bytes read full (Avatar 30/30, companions at int/2).
- **Circle access** (§3): RESOLVED — the spell's circle against the caster's level (the reference's
  Magic::cast and GAME.EXE's "Your level is not high enough."); the old `int/4` guess is gone.
- **Spellbook / known-spells** (§4): RESOLVED (loader implemented; encoding CONFIRMED
  by reading Nuvie `Magic::spellbook_has_spell`). Known spells = **spell objects (obj 58)
  in the spellbook (obj 57), matched by `quality` = U6 spell_num**; a spell of
  `quality == 255` (`MAGIC_ALL_SPELLS`) means every spell (the SPAM-cheat state). mkworld
  `scan_spellbook` walks the inventory chunk (obj 57 owner=Avatar → obj-58 children by
  parent-link) into a 128-bit set (ships in pack U6WR); the engine reverse-maps each
  spell_num via `magic_spell_num()` into `g_known`, REPLACING the demo seed when a
  spellbook is present. Validated on TT by INJECTING a spellbook into the save's inventory
  chunk: 5 spells (quality 0,1,2,16,17) → engine `known=5`; one quality-255 spell →
  `known=79` (all). VALIDATED END-TO-END against a REAL carried-spellbook save (user's
  "MYST1"): the Avatar picked up the spellbook (obj 57 in-inventory) holding 10 spells with
  quality = spell_nums {1,3,6,7,9,11,14,18,24,28} — so `quality = spell_num` exactly. mkworld
  reports `known=10`; on-TT the C)ast menu then lists the 7 of them that are implemented +
  circle-accessible (Detect Magic, Dispel Magic, Heal, Help, Light, Poison, Untrap),
  REPLACING the demo seed, with the status bar showing the loaded MP 48/48. (The earlier
  MYST save had this same spellbook still ON-MAP at (300,367) — not yet carried — so the
  loader correctly reported present=0 there.)
- **Save-backed reagents** (§2): RESOLVED — mkworld `scan_reagents` sums the Avatar's
  reagents (obj 65..72, resolving owner up the container chain like party_gold) and ships
  8 counts (pack U6WR); the engine replaces the seeded stock with them when present.
  Validated on TT: injecting 8 reagents into the save's inventory chunk (qtys 3,5,7,2,4,6,8,1)
  → the engine loads exactly those counts. Confirmed against REAL data: the "MYST1" save
  (Avatar has grabbed the spellbook + reagents) ships present=1 with the actual carried stock
  [17,15,15,17,14,14,15,18]; the earlier MYST save (Avatar carries nothing yet) ships present=0
  and the seed stands until pickup.
- **Reagent mixing**: U6 casts directly (no U4/U5 pre-mix minigame is assumed);
  confirm no separate "mix" step is required. (probable)
- **Per-spell magnitudes**: damage/heal amounts, field durations, light radius —
  set to match observed U6 as each effect is implemented; start conservative.

## 9. Test hooks

- Host unit test `magictest`: reagent-flag → obj_n mapping; mana_cost = circle;
  gate-check ordering (each failure message fires for the right missing
  precondition); consume-exactly-once (reagents + mana unchanged on any fizzle).
- Golden per-spell effect checks as effects land (e.g. Heal raises target HP by
  the modeled amount and consumes Gi+SS+1 mana; Light sets a light radius).
- On-target: C)ast in gamedemo → cast Heal/Light on the TT, capture the message
  + state change (like the combat/USE captures).
- `MAGICFX.REQ` (§5b, seeded RNG so TT and Falcon agree): '1' Mass Sleep on four dogs in the
  area (+ one on an excluded corner stays awake); '2' Charm turns a gargoyle into an ally that
  strikes its neighbour, the charm later wears off and its hostility returns; '3' Fear sets two
  gargoyles fleeing, a skeleton (undead) is unmoved; '4' Confuse turns two evil gargoyles into
  allies, a dog is untouched; '5' Mass Kill slays the gargoyle, spares the skeleton, blows a
  keg in the area; '6' the wear-off rolls: turns until sleep / paralysis / curse / protection
  end, poison's slow damage and its persistence; '7' Dispel Magic clears every condition
  (Success) and then finds nothing (No effect); aimed at the caster's own tile it cures the
  poisoned caster, while Kill aimed there asks for a direction (2026-11-01, TT).
- `SPELLS.REQ` (§5f, seeded RNG): '1' Douse/Ignite a brazier, Ignite a keg; '2' Lock + Unlock
  Magic + Detect Magic on a spawned door and chest; '3' Trap, Detect Trap, Untrap, then a trapped chest
  opened; '4' Vanish + Reappear (and the save round-trip of 0x1c13) and Replicate; '5' Lightning,
  Disable, Chain Bolt on three gargoyles; '6' Explosion over a gargoyle + keg; Flame Wind's five cells;
  Death Wind vs a skeleton; '7' Tremor spares a bat, Wing Strike, Hail Storm, Repel Undead; '8' Web
  sticks a dog, Slime converts a gargoyle, Animate a sword; '9' Infravision / Eclipse / Time Stop /
  Negate Magic timers + their save round-trip; 'a' Wind Change; 'b' Pickpocket (record moves to the
  pack, karma −5), Seance, Resurrect a slain NPC's body; 'c' Enchant the staff, Armageddon.

## 10. Status
§5f VERIFIED 2026-10-27 (SPELLS.REQ 1-c, seeded RNG, identical logs on emulated TT and the stock 4 MB Falcon):
Douse / Ignite toggle a brazier and light a keg; Lock puts a door at frame 13 and a chest at 3, USE refuses, Unlock
Magic reverses both, an open door is refused; Trap / Detect Trap / Untrap on a spawned chest, the trap fires on opening
(Bomb! hurt the whole party), the save's own trapped chest at (293,351) is sensed, untrapped and its record deleted on
save (6 → 5 traps); Vanish removes a chair and the slot survives a save/load, Reappear / Replicate put it back and copy
it, a door is refused; Lightning, Chain Bolt (all three gargoyles hurt) and Disable (a dog left at 1 HP, "critical!");
Explosion hurts the gargoyle and sets off the keg, Flame Wind hits the target and the cells two north / one south of it
and spares the dog three out, Death Wind spares the skeleton and kills the dog; Wing Strike hurts the gargoyle and the
gazer, Tremor spares the flying gazer, Hail Storm lands 8 stones on the gargoyle, Repel Undead sets the skeleton fleeing;
Web lays 8 webs and holds the dog, Slime turns the gargoyle into a slime, Animate raises a sword as an ally; Eclipse
darkens noon to the night ambient (255 → 119), Infravision lights a creature's cell, Time Stop freezes three turns and the
keg fuse, Negate Magic refuses a cast ("No magic at this time!"), all four timers and the vanish slot round-trip a save;
Wind Change sets north / southeast; Pickpocket takes a gold coin from NPC 62 (karma −5, the record gone), Seance opens the
body's conversation, Resurrect raises the NPC at 1 HP and a slain NPC leaves a body with its number; Enchant stores
charge 17 (Magic Arrow) and refuses the 11th, Armageddon leaves only Lord British (silent) and Resurrect refuses.
Host `make magictest` (80 spells, Armageddon = 112) passes.
§5b VERIFIED 2026-10-27 (MAGICFX.REQ 1-7, seeded RNG, byte-identical logs on emulated TT and the stock
4 MB Falcon): Mass Sleep sleeps 2 of 4 dogs in the 21-cell area and leaves the corner dog awake, Mass
Awaken wakes them; Charm turns a gargoyle into an ally that wounds its neighbour (30 → 21 HP in 6 turns),
the charm lifts after 125 turns (int 1) and its hostility returns; Fear sets a gargoyle fleeing and
leaves the skeleton (undead) unmoved; Confuse turns a gargoyle into a lasting ally, a dog untouched;
Mass Kill slays the gargoyle, spares the skeleton and sets off the keg in the area; the wear-off rolls
end sleep after 14 turns, paralysis 6, curse 8, protection 35, and poison costs 7 HP in 80 turns and
persists; Dispel Magic clears every condition then reports No effect. Host `make magictest` passes.
IN PROGRESS — core implemented + VALIDATED. Spec written; reagent/spellbook
obj_n + mana-cost rule CONFIRMED from data/reference; spell table probable.
engine/magic.c (table + gate-check) unit-tested (`make magictest` ALL PASS);
engine/objects.c reagent count/consume. On-target (TOS 3.06 TT, COMBAT.BIN mode
16): the C)ast verb casts Heal (50→70 HP), Light (lit interior), and Fireball
(slays a slug, +9exp); console confirms mana max=Int(15), castable circle=3,
all-reagent mask, an 8th-circle spell refused, and mana/reagents spent
(build/magic.png). SPELL-SELECTION UI implemented (§6) + validated (mode 17,
build/magic_menu.png): the C)ast menu lists 6 castable spells, Great Heal
(circle 4) gated out, typed number/name-prefix selection proven. EFFECTS wired
(29): Heal, Great Heal, Light, Great Light, Harm, Magic Arrow, Fireball,
Create Food, Unlock Magic, the FIELD-TILE LAYER (§5a: Fire/Poison/Sleep/Energy
Field + Dispel Field), the STATUS-EFFECT layer (§5b: Poison/Sleep/Paralyze/Curse/
Charm/Protection/Mass Protect), the SUMMONING layer (§5c: Conjure/Insect Swarm/
Clone/Summon), the TRAVEL layer (§5d: Help / Gate Travel), and the DETECTION/
UTILITY layer (§5e: **Locate**, **Detect Magic**, **X-ray**, **Telekinesis**).
Detection/utility validated on the TT (mode 23, build/magic_util.png): Locate
reports (315,367), Telekinesis opens a closed door from afar (frame 4→0), X-ray
sets a 30-turn see-through timer (the map window reveals all cells — no
LOS blackout), Detect Magic senses 1 magical aura, mana 12. Travel validated on
the TT (mode 22,
build/magic_travel.png): Help recalls the party beside Lord British (found via
the trainer flag), Gate Travel reads moonstone[0]=(935,262) from the save's
OBJLIST and teleports the whole party there (party adjacent=1, mana 8), the map
fully recomposing at the destination. Summoning validated on the TT (mode 21,
build/magic_summon.png): Conjure+Summon place 2 allied creatures (mana 12), an
allied summon HUNTS + damages a hostile (slug 30→4), Clone duplicates a hostile
(1→2), and every summon expires by its lifetime (→0). Two bugs found via
instrumentation and fixed: (a) a slain/expired summon kept its flag because the
`g_atile==0xffff` early-continue skipped cleanup — now cleared on death; (b) the
"nearest actor" targeting (a Clone aimed down a crowded direction hit a party
member, not the slug) — the demo now aims down a VERIFIED-clear 8-way direction;
in play the nearest-actor behaviour is expected. REMAINING: more per-spell effects
(travel/detection/utility need new systems); save-backed spellbook + current-mana
(the mana offset is still OPEN — §8; max is modeled = Intelligence).
