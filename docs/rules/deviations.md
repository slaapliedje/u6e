# Known deviations from the reference (U6)

Games: Ultima VI. The rules follow the DOS release (GAME.EXE 4.5), decided 2026-11-04; where it hasn't been read yet
the reference is ScummVM/Nuvie. One register of every place u6e knowingly differs from the original as
reimplemented by ScummVM/Nuvie — so nothing is lost across sessions. Each entry names the spec
section that holds the detail. "Accepted" = a deliberate simplification or a better-than-original
call; "open" = worth closing later; "data" = unreachable in the user's own data.

> Provenance
> - Derived from : the deviation / simplification sections of the rule specs listed below (each of
>   which cites the reference code it was read against). Nothing new is asserted here.
> - Last updated : 2026-11-04 (attacks as DOS has them)

## Combat and creatures
| Deviation | Where | Status |
|---|---|---|
| Attacks follow DOS (combat.md §6c), except: a town NPC never runs out of ammunition and keeps what it throws, as its pack isn't in memory; a member's weapons go by slot, not carrying order; the triple crossbow's bolts fly in turn; no sounds | combat.md §6c | accepted |
| G)et, U)se, D)rop and the other verbs that aren't a walk, a push or an attack spend no time; not yet checked against DOS | movement_turns.md | open |
| The ship's cannonball bursts where it lands (the reference's ship shot is a plain projectile); another ship's centre loses the rolled damage and "breaks" at 0 without a raft | cannons.md §5 | accepted |
| NPC ships never fire back: no ship actor and no ship-hatching egg exist in the data | cannons.md §5 | data |
| Movement types: no passing over / under other actors (mice, dragons), no mouseholes | combat.md §7c | open |
| Hatched worktypes: the coward's sight range is 5 for every flee code (reference 3 / 8, and a cornered retreater fights back); the stationary strike is a per-neighbour 16/81 chance, not 16 random picks; "toward a point ten tiles off" is one step; the tangle vine never crawls | eggs.md §5 | accepted |
| Hatching: free actor slots only (an embryo is skipped when none is free); toss-to-free-tile searches 5×5 (the reference tries 3×2 / 2×3 / 4×4 spreads), no grouping by type | eggs.md §5 | accepted |
| The experience formula's reading of the reference's actor table is ours (table values, evil/chaotic only); the shrine thresholds come from the scripts | combat.md §3 | accepted |
| A party member brought to 0 HP costs 5 karma whatever the cause (the reference takes it in its blow / spell damage path) | combat.md §3d | accepted |
| L)ook at an ACTOR shows the terrain under it (the reference names the actor and shows its portrait) | usecode_use.md (Clock and sundial) | open |

## Magic
| Deviation | Where | Status |
|---|---|---|
| Help dismounts a horse and leaves a vessel where it is, as the Avatar's death does (the reference moves the party alone) | magic_skills.md §5d | accepted |
| The caster is never struck by its own area spell (explosion / winds / hail / chain bolt). (The missing Avatar-death sequence was added 2026-10-31, combat.md §3d.) | magic_skills.md §5f | open |
| CANCARRY in conversations weighs the pooled pack against the whole party's combined capacity (the original weighs one member's own inventory against that member's strength) | usecode_vm.md §6c | accepted |
| Conversations: the pooled pack and the gold belong to the party leader when a script asks about one member (HASOBJ / OBJCOUNT), so at "Which of you?" the Avatar pays and sells and a companion has nothing; a companion whose body was lost (not saved, or on another level) counts as carried for a healer's resurrection | usecode_vm.md §6f | accepted |
| Party death: no screen fade or death / KAL LOR sounds; a dead companion's inventory is not moved into its body (the pack is pooled) and its body is not saved | combat.md §3d | accepted |
| Party members in a mass area take Sleep / Curse / Awaken / Dispel but not Mass Kill | magic_skills.md §5f | accepted |
| Time Stop freezes everything but the clock display; Tremor has no screen shake; Enchant's second menu is the C)ast menu in "Enchant with:" mode | magic_skills.md §5f | accepted |
| Hail / wind geometry derived from the reference's offset table for the 8 aims (no table shipped) | magic_skills.md §5f | accepted |
| Each field spell drops ONE tile (the original spreads some fields into a short line) | magic_skills.md §5a | open |
| Clone copies type / alignment, not exact HP; allied summons follow no schedule | magic_skills.md §5c | accepted |
| Gate Travel cycles the moonstone list rather than the moon-phase schedule | magic_skills.md §5d, moongates.md §5 | open |
| Detect Magic senses magic locks only (no magic-item flag); Wizard Eye is the overview, not a flying-eye camera | magic_skills.md §5e | open |
| The magic-condition int contest's direction is our reading | magic_skills.md §5b | accepted |
| Companions' mana: every companion gets int/2 (a non-caster should get 0) | magic_skills.md §1 | open |
| The party has one known-spell set, not one per book: a second book holds what the first did, and a dropped book keeps its spells for the party | magic_skills.md §4 | open |
| M)ove from the pack reaches only the spellbook, keyboard only (M, M, Enter), with the book chosen for you; in the reference any carried thing moves to any container, party member or the map, by cursor or mouse | magic_skills.md §4 | accepted |
| U)se on the spellbook does not open the page to read spell descriptions | magic_skills.md §6 | open |
| C)ast does not print the chosen spell's name and words before its effect, as the reference does, and a click on the map cancels the page instead of casting at that tile | magic_skills.md §6 | open |
| Spell messages now follow the reference's words (§5g, 2026-10-28). Still ours: the direction prompts "<Spell>: aim a direction." stand in for the reference's "On whom: / On what: / Location: " targeting, the friendly targets of Heal / Great Heal (the most-wounded member), Protection (the party) and Invisibility (§5h), Gate Travel's choice of gate (so it is silent like the reference but picks differently), Telekinesis's mechanics | magic_skills.md §5g | accepted |
| The eclipsed sun is drawn only while the sun is up (5-19); our night / eclipse outdoor ambient is moonlight, the reference's is 0 | lighting.md §2b | accepted |

## Objects, use, movement
| Deviation | Where | Status |
|---|---|---|
| Theft follows DOS (usecode_use.md, Theft), except: our alignment keeps only "hostile", so a good-aligned townsperson counts as neutral; a sleeping NPC is one under the sleep worktype; the type-392 shape change and one per-actor flag of the alarm are left out; the alarm has no sound effect | usecode_use.md | accepted |
| M)ove: no dark-tile refusal, no pusher→target missile-boundary line test (the wall under the thing only); push range = adjacent; a temp holder takes the pushed record out of the save; up to 64 records travel per push | usecode_use.md (M)ove) | accepted |
| The staff is used from the pack (the original's "Not readied." gate dropped) | usecode_use.md (Staff) | accepted |
| The secret-door reveal prints the reference's search line but not its name line before it (the hidden frame's run-filled look name "sign" is unverified) | usecode_use.md (Clock and sundial) | open |
| Levers: a quality-0 actuator does nothing (the reference would match every quality-0 doorway — none exist in the data) | usecode_use.md (Levers) | data |
| Pull-chains ring a bell the reference animates with a sound; we have neither (the turn passes silently) | usecode_use.md (Levers) | accepted |
| Vehicles: a ship still sails in calm at the base cost (the reference deducts nothing in calm); diagonal moves use the 8-direction cost with four sprite facings; the wind timer counts turns | wind.md §6 | accepted |
| No "high air" balloon (nothing in U6 is high air); the aloft balloon uses the inflated frame 3 | wind.md §6 | accepted |
| Moongate walk-in: the vanish is an ordered dither of the sprite (the reference fades random pixels of the whole window); the ST planar path switches the view at once; the red gate appears without the vanish; a failed level load keeps the party where it was | moongates.md §5 | accepted |
| No solo / party mode: the "party mode" and "Not in solo mode!" refusals never arise (moongates, R)est) | moongates.md §5, rest.md §5 | accepted |
| R)est: the horse refusal precedes the vehicle one (same outcome); no pathfinding to the circle; the mealtime is paced per rest turn; the "in town" scan takes every map tile within 5; the guard is a shape only — nobody attacks the camp | rest.md §5 | accepted |
| Worktype save: a saved faint is not restored (other saved codes come back from the schedule) | usecode_vm.md §6b | accepted |

## Save, text, tools
| Deviation | Where | Status |
|---|---|---|
| HEAL on a dead party member leaves it at 0 HP (the reference sets full HP on a body that stays dead) | usecode_vm.md §6f | accepted |
| The vanish slot (objlist 0x1c13) is OUR encoding `obj_n \| frame<<10` — the reference's `+1023+frame` does not round-trip | magic_skills.md §5f, objlist.md | accepted |
| L)ook prints no weights, weapon or armour lines after the name (the reference's look_obj does) | usecode_use.md (Clock and sundial) | open |
| A plural marker (`\s`) reaches the screen in some conversation lines ("gold piece\s short") | usecode_vm.md | open |
| Host tool tools/tilestrip renders tile n+1 for tile n (off by one) | — | open (tool) |

## Saved games
| Deviation | Where | Status |
|---|---|---|
| 8 slots with two older copies each, and a slot page on S)ave and Ctrl-R. The original keeps one save and asks "Save game?" | save_load.md §2b | accepted |

## Title menu
| Deviation | Where | Status |
|---|---|---|
| Transfer a Character looks on any drive and in the game's folder, for a DOS or ST save, where the original looks on one machine's own paths | transfer_saves.md §5 | accepted |
| Transfer decodes ST Ultima IV's BCD numbers, which the ST original misreads as binary (too-high stats, and no transfer at 2710 experience or more) | transfer_saves.md §1 | accepted |
| Create and Transfer don't refuse a game in progress; the new character replaces a saved game only when the player saves | transfer_saves.md §4 | accepted |

## Performance
| Deviation | Where | Status |
|---|---|---|
| Schedule walkers follow a cached route of up to 16 steps instead of a fresh path search every turn (re-searched when the goal, level or blocking map changes, when blocked, or when the route runs out); routes differ in detail from a per-turn search, arrival does not (performance.md 2026-10-28) | design/performance.md | accepted |
