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
> - Confidence   : mixed — CONFIRMED (reagent/spellbook obj_n, mana-cost rule),
>   probable (spell/reagent table), guessed (exact save mana offset, per-spell
>   numeric effect magnitudes).
> - Last updated : 2026-07-05

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

**SAVE FINDING (tools/spellprobe, 2026-07-08):** the sample ScummVM save is the
GAME-START state in Lord British's castle — where LB *hands the Avatar the
spellbook* — so the Avatar has NOT obtained it yet. Its inventory chunk (145
records) contains **no spellbook (57), no spell (58), and no reagent (65-72)**
records for the Avatar (a1 carries only a sword + two items). So the known-spell
set is **not loadable from this save**; it is modeled (see below), and the
`quality`/`frame` spell-number encoding stays UNVERIFIED until a spellbook-bearing
save is available. See §8.

**Engine model (until a real spellbook save exists):** a per-caster "known" set.
The demo Avatar knows the spells whose EFFECTS are implemented (magic_init); the
menu further intersects with `castable_circle` at cast time. When a save DOES
carry a spellbook, the loader should read its spell objects into this set.

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
| 2 | Fireball       | POR FLA       | SA BP           | ranged fire damage |
| 3 | Great Light    | VAS LOR       | SA Ma           | large light radius |
| 4 | Magic Lock     | AN POR        | SA BM Ga        | magically lock door/chest |
| 5 | Mass Awaken    | AN VAS ZU     | Gi Ga           | wake sleeping party |
| 6 | Mass Sleep     | VAS ZU        | Gi Ni SS        | sleep foes in area |
| 7 | Peer           | VAS WIS YLEM  | Ni Ma           | show area map |
| 8 | Protection     | IN SANCT      | SA Gi Ga        | party defense buff |
| 9 | Repel Undead   | AN XEN CORP   | Ga SA           | drive off undead |

### Circle 4 (cost 4)
| idx | spell        | words         | reagents        | effect (planned) |
|-----|--------------|---------------|-----------------|------------------|
| 0 | Animate        | ORT YLEM      | SA Ma BM        | animate object |
| 1 | Conjure        | KAI XEN       | SS Ma           | summon creature ally |
| 2 | Disable        | AN VAN MANI   | Ni SS Ma        | disable target |
| 3 | Fire Field     | IN FLAM GRAV  | BP SA SS        | create fire field |
| 4 | Great Heal     | VAS MANI      | Gi SS Ma        | large HP restore |
| 5 | Locate         | IN WIS        | Ni              | sextant position |
| 6 | Mass Dispel    | VAS AN JUX ORT| Ga Gi           | dispel fields in area |
| 7 | Poison Field   | IN NOX GRAV   | Ni SS BP        | create poison field |
| 8 | Sleep Field    | IN ZU GRAV    | Gi SS BP        | create sleep field |
| 9 | Wild Change    | REL HUR       | SA BM           | random weather/effect |

### Circle 5 (cost 5)
| idx | spell        | words         | reagents           | effect (planned) |
|-----|--------------|---------------|--------------------|------------------|
| 0 | Energy Field   | IN SANCT GRAV | Ma SS BP           | create energy field |
| 1 | Explosion      | VAS POL FLAM  | Ma SA BP           | area fire damage |
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
| 7 | Mass Invisibility | VAS SANCT LOR | Ma Ni BM BP          | party invisibility |
| 8 | Wing Strike    | KAL ORT XEN   | BM SS Ma SA             | knock down flyers |
| 9 | Wizard Eye     | POR ORT WIS   | BM Ni Ma SA BP SS       | remote view (flying eye) |

### Circle 8 (cost 8)
| idx | spell        | words         | reagents                   | effect (planned) |
|-----|--------------|---------------|----------------------------|------------------|
| 0 | Death Wind     | CORP HUR      | Ma Ni SA BM                | death area (wind) |
| 1 | Eclipse        | VAS AN LOR    | Ma SA Ni Ga BM             | darken world |
| 2 | Mass Charm     | VAS AN XEN EX | BP Ni SS Ma                | charm area |
| 3 | Mass Kill      | VAS CORP      | BP Ni Ma SA                | kill area |
| 4 | Resurrect      | IN MANI CORP  | Ga Gi SS SA BM Ma          | resurrect the dead |
| 5 | Slime          | VAS REL XEN   | BM Ni Ma                   | split/spawn slime |
| 6 | Summon         | KAL XEN CORP  | Ma Ga BM SS                | summon creatures |
| 7 | Time Stop      | AN TYM        | Ma Ga BM                   | freeze game time |
| 8 | Tremor         | VAS POR YLEM  | BM SA Ma                   | earthquake area |

(Circle 8 has 9 spells; the 16-slot circle leaves the rest unused.)

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

## 5b. Status effects (actor conditions)

Several spells apply a temporary CONDITION to an actor rather than a one-shot
result. These are actor STATE (not objects), each a per-actor turn counter that
decays to 0. Confidence: probable — the conditions and which spell causes them
are from U6; the exact per-turn magnitudes and durations are ours.

| status    | applied by (circle)          | effect while active                         |
|-----------|------------------------------|---------------------------------------------|
| poison    | Poison (2), Poison Field     | loses a little HP each turn (damage-over-time)|
| sleep     | Sleep (2), Mass Sleep, Sleep Field | actor skips its turns (does not act)  |
| paralyze  | Paralyze (5)                 | actor skips its turns (longer than sleep)   |
| protect   | Protection (3), Mass Protect (6) | party takes reduced damage when hit     |
| curse     | Curse (3), Mass Curse (7)    | the cursed actor's attacks are less accurate|
| charm     | Charm (6), Mass Charm (8)    | a hostile stops attacking the party (pacified)|

**Model (engine):** one small counter per condition per actor. Each game turn a
status tick: applies poison damage, then decrements every counter (0 = the
condition ends). Combat consults them — a protected DEFENDER halves incoming
damage; a cursed ATTACKER rolls at a penalty; a charmed actor's `actor_hostile`
reads false so it no longer assaults. Sleep/paralyze skip the actor's turn in the
NPC step. Durations wear off by time; there is no dedicated "cure" spell in U6
(poison antidotes are items — future).

OPEN / simplified (honest): single-target spells hit the first actor in the aim
direction (the mass/area variants apply to the whole party or are TODO); charm is
modeled as "pacified" (stops attacking) rather than "fights for the party";
durations are fixed guesses; invisibility/fear/confuse not modeled yet.

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
- **Help** finds Lord British at runtime (the trainer actor, obj_n 409) and
  recalls the party to a free tile beside him. No reagents (KAL LOR).
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
  (aimed actor, else self); **Mass Invisibility** (circle 7) does the whole party.
  compose_map skips drawing invisible actors; the timer decays each turn.
- **Reveal** (circle 5) clears `g_ainvis[]` for every actor within 5 tiles.
  Validated on TT (MAGIC.BIN): Invisibility on an adjacent NPC → ainvis=60 (it
  vanishes) → Reveal → ainvis=0 (it reappears); Peer/Wizard Eye render the overview.

OPEN / simplified: Detect Magic only senses magic LOCKS (no dedicated magic-item
flag yet); a true full-res Wizard Eye (flying-eye camera) is still a future lift.

## 6. Casting flow (engine algorithm)

C)ast, then the caster (default = Avatar) and the spell are chosen; U6 selects a
spell by its **words of power** (typed syllables) or from the spellbook list. Our
engine flow:

1. **Select spell.** IMPLEMENTED: C)ast (the 'c'/'C' key or the Cast command-bar
   icon) opens a MENU listing the caster's known + circle-castable spells,
   numbered, with the conversation-style "> _" input box. The player types a
   menu NUMBER or a spell-NAME prefix (render/gamedemo cast_menu_parse); Enter
   selects. Self/party spells cast immediately; offensive spells arm a direction
   ("Cast X where?") resolved by the existing verb→direction flow. Host tests:
   pass spell_num directly. (Combat-mode toggle moved off 'c' to 'b' + the
   Combat command-bar icon.)
2. **Gate checks**, in order, each with its own failure message:
   - spell known (in spellbook)          → else "Thou dost not know that spell."
   - `circle(spell) <= castable_circle`   → else "Thou art not experienced enough."
   - all required reagents present        → else "No reagents."
   - `current_mana >= mana_cost`          → else "Not enough magic points."
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

## 8. Open questions / to validate

- **Max mana** (§3): RESOLVED — 2×int (Avatar) / int/2 (companion), from the
  reference; loaded from the save's Intelligence (magic_max_mana).
- **Current-mana save offset** (§3): RESOLVED — **objlist 0x13f1** (1 byte per
  actor, sequential; from Nuvie ActorManager, read to understand). mkworld ships it
  (pack U6WC, now 5 B/actor) and the engine loads `g_amana` from it (0xFF sentinel =
  "unknown" → fall back to max). Validated on TT: patching the Avatar's 0x13f1 to 12
  makes the ztats show **MP 12/30** (current, not the 30 max). At a fresh game start
  everyone is rested, so the raw bytes read full (Avatar 30/30, companions at int/2).
- **Circle-access formula** (§3): `int/4` is a first guess; verify against a
  high-Intelligence caster.
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

## 10. Status
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
