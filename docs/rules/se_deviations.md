# Known deviations from the reference (Savage Empire)

Games: Savage Empire. Its rules follow its DOS GAME.EXE, read as U6's were (design/se_md_checklist.md);
this register is item 12 of that list and is meant to end up empty but for the user's choices. U6's own
register is deviations.md.

> Provenance
> - Derived from: reading SE's GAME.EXE. The rule specs named in each row cite the routines read.
> - Validated by: `make check-se-use` checks each fix below.
> - Last updated: 2026-11-23 (slip 30, the intro's pace)

## Fixed by the user's choice (2026-11-12; slips 22-28 2026-11-13): DOS's own slips we don't copy

The user's rule (row 12 added 2026-11-12 under it, as plain as the first eleven): where DOS's code plainly can't do what it was written to do, the engine does what it
was meant to do (ScummVM was looked at first: its SE U)se is mostly unwritten, so it decides none of
these). Each row says what DOS does, so any of them can be put back; from slip 16 on, the engine's `g_dos_slips`
setting (not yet offered to the player) puts DOS's way back at each slip's site.

| # | Difference | Where | What DOS does |
|---|---|---|---|
| 1 | The giant gem's scene runs the clock on to noon from any time | se_use.md §5d | Used exactly on the hour, no time passes at all |
| 2 | Charcoal from burnt logs says `You made some charcoal.` when it joins a stack too | se_use.md §5g | Says nothing when it joins a stack |
| 3 | The mortar checks for all three ingredients before it takes any | se_use.md §5l | Takes from the ones before the missing one, and they're lost |
| 4 | The fishing pole looks at all eight tiles around its holder | se_use.md §5u | One counter for both axes: only the diagonal through the holder |
| 5 | A lit grenade thrown more than a tile bursts where it lands; an unlit one lands whole | se_move.md §6 | Its breakable list holds the unlit grenade (137), not the lit one (138): the unlit one breaks, the lit one's burst is never reached |
| 6 | The folded skin needs the three tiles of its 2x2 free of things | se_use.md §5o | Looks at the west column twice, never the tile north of it |
| 7 | The empty bucket tests the tile it is looking at for water | se_use.md §5q | Tests (x, y - i + j), a tile off to the side |
| 8 | Tar on a web, or on an egg with no nest, uses one tar | se_use.md §5q | Uses none |
| 9 | Only the gem goes onto the gem stand by M)ove; anything else is `nobody.` | se_move.md §3 | Anything moved onto the stand is placed as the gem, with the gem's words and scene |
| 10 | A breakable container thrown and broken spills what it held onto the tile | se_move.md §6 | Moves it there, then deletes it with everything in it |
| 11 | A tar fire's waves burn only the fires the pour started | se_use.md §5q | Each wave takes every fire of its quality in the loaded objects, so a campfire nearby burns out; the engine holds the whole world, not DOS's loaded area |
| 12 | The modern weapons can't be readied by those who don't know them at any frame | se_combat.md §1 | Compares the whole object word, so a loaded flintlock (frame 1) can be readied by anyone |
| 13 | A blow whose hit roll fails misses | se_combat.md §3a | 0x1b47d compares the roll's result with -1 only, so a failed roll (0) strikes anyway: every blow that reaches someone lands |
| 14 | An egg's object whose scattered tile is refused isn't made | se_eggs.md §3 | Goes on with the last thing it made (or a stale register): that one is tossed again or deleted |
| 15 | An orchid head's wound pays nothing unless its root dies (then 10) | se_eggs.md §5 | Pays the head's remaining hit points while it lives, and a register's leftover when it dies but the root lives |
| 16 | An orchid head's random reach never goes back on itself (that way becomes straight on) | se_eggs.md §6 | Turns the way it already faces into its opposite, which the reach refuses: never straight on, a quarter of the reaches wasted |
| 17 | A fighter whose line of fire is blocked sidesteps one tile | se_ai.md §2 | Walks to the sidestep tile twice (falls into the walk meant for a target out of range) |
| 18 | A creature in worktype 0xe strikes only an actor standing on the tile it picks | se_ai.md §6 | Looks its target up before picking the tile, with coordinates not yet set, then swings at the picked tile, mostly empty ground |
| 19 | A dancer shows the new dance frame | se_ai.md §7 | Adds the new frame to the one it shows, running past the dancer's eight frames |
| 20 | Motazz's frenzy gives +3 strength to every wild fighter | se_invoke.md §4 | Gives it only to one also cursed (the bonus sits inside the curse test) |
| 21 | An invocation's resist roll weighs the foe against Triolo | se_invoke.md §4 | Weighs it against the actor numbered by the shown member's party slot |
| 22 | The bell takes a lying tyrannosaurus by either of its tiles | se_use.md §5i | Reads the second tile's actor and then tests the tile's own object, so that tile gives `No effect.`; the engine takes the actor the second tile names, in reach of that tile |
| 23 | The pteranodon's reach is the box round its head | se_big.md §8 | Each of its four boxes lacks one bound, so a foe any distance off on that side counts as in reach; the engine closes the box at the creature's own row or column |
| 24 | A pteranodon's quarter turn needs room on the cells it will cover | se_big.md §5 | Tests (x-1, y) from north or south and (x, y-1) from east or west, as for the two-cell animals; but the pteranodon is wide facing north or south, so DOS tests cells it already covers. The engine tests (x, y-1) and (x-1, y) |
| 26 | A big creature that wakes stands up whole | se_combat.md §3i | Only the tyrannosaurus gets its pieces back; the others wake with none, their second remains left lying. The engine rebuilds every big creature facing north, as hatching does (the tyrannosaurus's tail in frame 1 where DOS gives 3) |
| 25 | The gorilla's and the raft's old facing is read as frame / 8 | se_big.md §5 | DOS reads frame / 16, so south reads as east and west as south; it matters only for a diagonal turn, which no step makes (the engine makes none) |
| 27 | A sleeping or dead actor below 16 is named by what it was | se_look.md §2, §7 | Actors 16 and up asleep or dead are named by their saved object; those below 16 (out of the party) by the tile they show, so a sleeper reads `a body`. The engine names them all by their saved object |
| 28 | A lit torch picked up goes into the leader's free hand | se_look.md §5, §7 | Only when the leader is the member whose inventory is shown; otherwise it is marked readied in no hand. The engine always uses the leader's first free hand |
| 29 | A boulder spares a thing by the flags of the thing's tile | se_use.md §5c | Reads tileflags3 by the thing's place in its object list, not its tile: which things are spared depends on where they sit in memory, which the engine can't reproduce (taken as a slip, 2026-11-22) |
| 30 | The intro (Origin FX and the film) runs at a steady pace: a step every 73 ms | design/se_front_end.md "The intro" | Draws a step a frame with one retrace's wait, so its pace is the machine's: on a fast one the pan's credits show for 0.2 s each and the conductor's four taps take 0.3 s. At 73 ms the pan fills song 18's "Melody, horn, flute" (cue 3 to 4, 15.5 s) and the opening's finale ends at song 19's last cue, so this is taken as the pace it was made for (2026-11-23; the cue waits are kept as DOS) |

## Built as DOS by the user's choice (2026-11-22)
The user's rule for the rows that were engine choices: build DOS's behaviour. Each row says what the engine did before.

| Was | Where | Now as DOS |
|---|---|---|
| A rolling boulder stopped after 64 rounds | se_use.md §5c | No limit (the roll always ends: each round moves on and the slopes end) |
| A member other than the leader mounting or leaving a turtle didn't move the view | se_use.md §5w | The view goes to that member until the next key |
| The raft carrying the party was saved as a map object | se_use.md §5w | Saved as actor 0 (237, frame 3, its quantity as hit points, in the party after the members), no raft object; a load boards it again |
| A step of the raft cost 5 | se_use.md §5w, movement_turns.md §3b | A walking step's charge on actor 0 (5 + impedance), less 2 out of combat as for every actor 0-15 |
| Eggs were looked at within 20 tiles of the party | se_eggs.md §2 | DOS's 40 x 40 window: unload measured from its centre, the scan taking the eggs inside it |
| A hatchling took the lowest free slot of 192-254 | se_eggs.md §4 | DOS's free list: the last slot freed is taken first |
| The ending's time taken | design/se_front_end.md "The ending" | Kept as DOS (it counts from the calendar's origin, so a new game is already "6 months, 3 days and 61 years") |
| A death left the remains as a map object holding the victim's things, the actor off the map | se_combat.md §3i | The actor lies in its slot as its remains, its things with it, saved there; with no remains an object of its own look holds its things and it goes to level 6 |
| Looking at a dead creature named its remains object and searched it | se_look.md §2, §7 | Looked at as the actor: named by what it was, never searched; G)et passes over it |
| A dead or unloaded big creature's pieces were removed | se_big.md §7 | The pieces go and the second remains is made, the creature lying in its slot; only one with no remains goes to level 6 |
| G)et of gold put it in the party's pool | se_look.md §5 | Gold (69) is a counted thing taken into the leader's pack |

## Kept as ours by the user's choice (2026-11-22)
| Difference | Where | What DOS does |
|---|---|---|
| Eight save slots; Create always allowed | design/se_front_end.md | One saved game (`savegame\\`); Create refuses while one is in progress |

## Waiting on other items
Read in DOS, not yet built, listed in the rule specs: SE's levels (item 11: the rope, the hole in the
ceiling, the trap doors, the drum's scene), combat (item 6: the totems, the bell, fire's harm, a
SE's creature table for the turtle's other numbers), SE's sounds (item 9: the fireworks' sound effects in the Origin FX opening
are not played, design/se_front_end.md "The intro").
