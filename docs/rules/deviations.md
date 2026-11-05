# Known deviations from the reference (U6)

Games: Ultima VI. The rules follow the DOS release (GAME.EXE 4.5), decided 2026-11-04; where it hasn't been read yet
the reference is ScummVM/Nuvie. One register of every place u6e knowingly differs from the original as
reimplemented by ScummVM/Nuvie — so nothing is lost across sessions. Each entry names the spec
section that holds the detail. "Accepted" = a deliberate simplification or a better-than-original
call; "open" = worth closing later; "data" = unreachable in the user's own data.

> Provenance
> - Derived from : the deviation / simplification sections of the rule specs listed below (each of
>   which cites the reference code it was read against). Nothing new is asserted here.
> - Last updated : 2026-11-04 (L)ook and the verb cursor as DOS has them)

## Combat and creatures
| Deviation | Where | Status |
|---|---|---|
| Combat modes follow DOS by what they aim at (combat.md §8), but the places are simpler: DOS works FRONT, REAR and FLANK out from the two sides' centres along a line across the fight; a COMMAND member gets one action per round whatever its dexterity | combat.md §8 | accepted |
| Attacks follow DOS (combat.md §6c), except: a town NPC never runs out of ammunition and keeps what it throws, as its pack isn't in memory; a member's weapons go by slot, not carrying order; the triple crossbow's bolts fly in turn; no sounds | combat.md §6c | accepted |
| Time follows DOS's movement points and prices (movement_turns.md §3b), except: actors are paid per command rather than taking turns within a minute, at most four actions a command; actors beyond 39 tiles keep walking; effect timers tick per command | movement_turns.md §3b | accepted |
| The ship's cannonball bursts where it lands (the reference's ship shot is a plain projectile); another ship's centre loses the rolled damage and "breaks" at 0 without a raft | cannons.md §5 | accepted |
| NPC ships never fire back: no ship actor and no ship-hatching egg exist in the data | cannons.md §5 | data |
| Hatched worktypes: the coward's sight range is 5 for every flee code (reference 3 / 8, and a cornered retreater fights back); the stationary strike is a per-neighbour 16/81 chance, not 16 random picks; "toward a point ten tiles off" is one step; the tangle vine never crawls | eggs.md §5 | accepted |
| Hatching: free actor slots only (an embryo is skipped when none is free); toss-to-free-tile searches 5×5 (the reference tries 3×2 / 2×3 / 4×4 spreads), no grouping by type | eggs.md §5 | accepted |
| The experience formula's reading of the reference's actor table is ours (table values, evil/chaotic only); the shrine thresholds come from the scripts | combat.md §3 | accepted |
| A party member brought to 0 HP costs 5 karma whatever the cause (the reference takes it in its blow / spell damage path) | combat.md §3d | accepted |

## Magic
| Deviation | Where | Status |
|---|---|---|
| Help dismounts a horse and leaves a vessel where it is, as the Avatar's death does (the reference moves the party alone) | magic_skills.md §5d | accepted |
| CANCARRY in conversations weighs the whole party's things against its combined capacity, and scripts' HASOBJ look through the whole party (DOS asks one member) | usecode_vm.md §6c | accepted |
| Conversations: the party's things and the gold answer when a script asks about one member, so at "Which of you?" the Avatar pays and sells and a companion has nothing; a companion whose body was lost (not saved, or on another level) counts as carried for a healer's resurrection | usecode_vm.md §6f | accepted |
| Party death: no screen fade or death / KAL LOR sounds; a dead companion's inventory is not moved into its body (the pack is pooled) and its body is not saved | combat.md §3d | accepted |
| Time Stop freezes everything but the clock display; Tremor has no screen shake; Enchant's second menu is the C)ast menu in "Enchant with:" mode | magic_skills.md §5f | accepted |
| Hail / wind geometry derived from the reference's offset table for the 8 aims (no table shipped) | magic_skills.md §5f | accepted |
| Clone copies type / alignment, not exact HP; allied summons follow no schedule | magic_skills.md §5c | accepted |
| Wizard Eye's view hides the Avatar's sprite where the party waits, as the eye flies | magic_skills.md §6a | accepted |
| The magic-condition int contest's direction is our reading | magic_skills.md §5b | accepted |
| The party has one known-spell set, not one per book: a second book holds what the first did, and a dropped book keeps its spells for the party | magic_skills.md §4 | open |
| A carried thing moves to another member (equipment.md §5) or a parchment to the book, not into a carried container or onto the map; things inside a carried bag show loose in the pack, so there is no look inside; the gold is one purse; a dead companion's things stay with it | equipment.md §5 | accepted |
| Casting follows DOS (magic_skills.md §6a), except: a summoned creature is temporary (it does not stay after the party leaves); Time Stop freezes everything but the clock display; Enchant still picks the staff for you | magic_skills.md §6a | accepted |
| The eclipsed sun is drawn only while the sun is up (5-19); our night / eclipse outdoor ambient is moonlight, the reference's is 0 | lighting.md §2b | accepted |

## Objects, use, movement
| Deviation | Where | Status |
|---|---|---|
| L)ook follows DOS (usecode_use.md), except: the portrait view has no paper doll yet; the type-430 horse's rider and a body's true type are not told apart; a book's text opens on the book page, not in the scroll; G)et's weight limit weighs the pooled pack against the whole party, as CANCARRY does | usecode_use.md | open |
| Theft follows DOS (usecode_use.md, Theft), except: our alignment keeps only "hostile", so a good-aligned townsperson counts as neutral; a sleeping NPC is one under the sleep worktype; the type-392 shape change and one per-actor flag of the alarm are left out; the alarm has no sound effect | usecode_use.md | accepted |
| M)ove: no dark-tile refusal, no pusher→target missile-boundary line test (the wall under the thing only); push range = adjacent; a temp holder takes the pushed record out of the save; up to 64 records travel per push | usecode_use.md (M)ove) | accepted |
| The staff is used from the pack (the original's "Not readied." gate dropped) | usecode_use.md (Staff) | accepted |
| Levers: a quality-0 actuator does nothing (the reference would match every quality-0 doorway — none exist in the data) | usecode_use.md (Levers) | data |
| Pull-chains ring a bell the reference animates with a sound; we have neither (the turn passes silently) | usecode_use.md (Levers) | accepted |
| Vehicles: a ship still sails in calm at the base cost (the reference deducts nothing in calm); diagonal moves use the 8-direction cost with four sprite facings; the wind timer counts turns | wind.md §6 | accepted |
| No "high air" balloon (nothing in U6 is high air); the aloft balloon uses the inflated frame 3 | wind.md §6 | accepted |
| Moongate walk-in: the vanish is an ordered dither of the sprite (the reference fades random pixels of the whole window); the ST planar path switches the view at once; the red gate appears without the vanish; a failed level load keeps the party where it was | moongates.md §5 | accepted |
| R)est: the horse refusal precedes the vehicle one (same outcome); no pathfinding to the circle; the mealtime is paced per rest turn; the "in town" scan takes every map tile within 5; the guard is a shape only — nobody attacks the camp | rest.md §5 | accepted |
| Worktype save: a saved faint is not restored (other saved codes come back from the schedule) | usecode_vm.md §6b | accepted |

## Save, text, tools
| Deviation | Where | Status |
|---|---|---|
| HEAL on a dead party member leaves it at 0 HP (the reference sets full HP on a body that stays dead) | usecode_vm.md §6f | accepted |
| The vanish slot (objlist 0x1c13) is OUR encoding `obj_n \| frame<<10` — the reference's `+1023+frame` does not round-trip | magic_skills.md §5f, objlist.md | accepted |
| Host tool tools/tilestrip renders tile n+1 for tile n (off by one) | — | open (tool) |

## Sound
| Deviation | Where | Status |
|---|---|---|
| The DOS sound effects play on the YM, stepped at 200 Hz where DOS drives the speaker faster, with some ids unused; heard by nobody yet (the tests run silent) | sound_effects.md | open |

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
