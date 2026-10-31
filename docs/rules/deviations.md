# Known deviations from the reference (U6)

Games: Ultima VI. One register of every place u6e knowingly differs from the original as
reimplemented by ScummVM/Nuvie — so nothing is lost across sessions. Each entry names the spec
section that holds the detail. "Accepted" = a deliberate simplification or a better-than-original
call; "open" = worth closing later; "data" = unreachable in the user's own data.

> Provenance
> - Derived from : the deviation / simplification sections of the rule specs listed below (each of
>   which cites the reference code it was read against). Nothing new is asserted here.
> - Last updated : 2026-10-28 (completion-checklist items 1-14)

## Combat and creatures
| Deviation | Where | Status |
|---|---|---|
| Not modelled: the "half damage" creature flag, the triple crossbow's three bolts, thrown weapons landing on the map, ammunition (bows need no arrows), "pass." on the Avatar's own tile | combat.md §4/§6 | open |
| The ship's cannonball bursts where it lands (the reference's ship shot is a plain projectile); another ship's centre loses the rolled damage and "breaks" at 0 without a raft | cannons.md §5 | accepted |
| NPC ships never fire back: no ship actor and no ship-hatching egg exist in the data | cannons.md §5 | data |
| Movement types: no passing over / under other actors (mice, dragons), no mouseholes | combat.md §7c | open |
| Hatched worktypes: the coward's sight range is 5 for every flee code (reference 3 / 8, and a cornered retreater fights back); the stationary strike is a per-neighbour 16/81 chance, not 16 random picks; "toward a point ten tiles off" is one step; the tangle vine never crawls | eggs.md §5 | accepted |
| Hatching: free actor slots only (an embryo is skipped when none is free); toss-to-free-tile searches 5×5 (the reference tries 3×2 / 2×3 / 4×4 spreads), no grouping by type | eggs.md §5 | accepted |
| The experience formula's reading of the reference's actor table is ours (table values, evil/chaotic only); the shrine thresholds come from the scripts | combat.md §3 | accepted |
| L)ook at an ACTOR shows the terrain under it (the reference names the actor and shows its portrait) | usecode_use.md (Clock and sundial) | open |

## Magic
| Deviation | Where | Status |
|---|---|---|
| The caster is never struck by its own area spell (explosion / winds / hail / chain bolt). (The missing Avatar-death sequence was added 2026-10-31, combat.md §3d.) | magic_skills.md §5f | open |
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
| Spell messages now follow the reference's words (§5g, 2026-10-28). Still ours: the direction prompts "<Spell>: aim a direction." stand in for the reference's "On whom: / On what: / Location: " targeting, Gate Travel's choice of gate (so it is silent like the reference but picks differently), Telekinesis's mechanics | magic_skills.md §5g | accepted |
| The eclipsed sun is drawn only while the sun is up (5-19); our night / eclipse outdoor ambient is moonlight, the reference's is 0 | lighting.md §2b | accepted |

## Objects, use, movement
| Deviation | Where | Status |
|---|---|---|
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
| The vanish slot (objlist 0x1c13) is OUR encoding `obj_n \| frame<<10` — the reference's `+1023+frame` does not round-trip | magic_skills.md §5f, objlist.md | accepted |
| L)ook prints no weights, weapon or armour lines after the name (the reference's look_obj does) | usecode_use.md (Clock and sundial) | open |
| A plural marker (`\s`) reaches the screen in some conversation lines ("gold piece\s short") | usecode_vm.md | open |
| Music: "Stones while camping" is a FIXME in the reference too; not played | music_areas.md §6 | accepted |
| Host tool tools/tilestrip renders tile n+1 for tile n (off by one) | — | open (tool) |

## Performance
| Deviation | Where | Status |
|---|---|---|
| Schedule walkers follow a cached route of up to 16 steps instead of a fresh path search every turn (re-searched when the goal, level or blocking map changes, when blocked, or when the route runs out); routes differ in detail from a per-turn search, arrival does not (performance.md 2026-10-28) | design/performance.md | accepted |
