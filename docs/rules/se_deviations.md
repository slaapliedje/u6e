# Known deviations from the reference (Savage Empire)

Games: Savage Empire. Its rules follow its DOS GAME.EXE, read as U6's were (design/se_md_checklist.md);
this register is item 12 of that list and is meant to end up empty but for the user's choices. U6's own
register is deviations.md.

> Provenance
> - Derived from: reading SE's GAME.EXE. The rule specs named in each row cite the routines read.
> - Validated by: `make check-se-use` checks each fix below.
> - Last updated: 2026-11-12 (the first rows: the user takes the intent over eleven DOS slips; the vehicles' engine choices)

## Fixed by the user's choice (2026-11-12): DOS's own slips we don't copy

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
| 23 | The pteranodon's reach is the box round its head | se_big.md §8 | Each of its four boxes lacks one bound, so a foe any distance off on that side counts as in reach; the engine closes the box at the creature's own row or column (the user to confirm the closing) |
| 24 | A pteranodon's quarter turn needs room on the cells it will cover | se_big.md §5 | Tests (x-1, y) from north or south and (x, y-1) from east or west, as for the two-cell animals; but the pteranodon is wide facing north or south, so DOS tests cells it already covers. The engine tests (x, y-1) and (x-1, y) |
| 26 | A big creature that wakes stands up whole | se_combat.md §3i | Only the tyrannosaurus gets its pieces back; the others wake with none, their second remains left lying. The engine rebuilds every big creature facing north, as hatching does (the tyrannosaurus's tail in frame 1 where DOS gives 3) |
| 25 | The gorilla's and the raft's old facing is read as frame / 8 | se_big.md §5 | DOS reads frame / 16, so south reads as east and west as south; it matters only for a diagonal turn, which no step makes (the engine makes none) |
| 27 | A sleeping or dead actor below 16 is named by what it was | se_look.md §2, §7 | Actors 16 and up asleep or dead are named by their saved object; those below 16 (out of the party) by the tile they show, so a sleeper reads `a body`. The engine names them all by their saved object |
| 28 | A lit torch picked up goes into the leader's free hand | se_look.md §5, §7 | Only when the leader is the member whose inventory is shown; otherwise it is marked readied in no hand. The engine always uses the leader's first free hand |

## Engine choices, for the user to confirm
| Difference | Where | What DOS does |
|---|---|---|
| A boulder that crushes things spares them by their tile's flags | se_use.md §5c | Reads the flag byte by the thing's place in its object table, not its tile |
| A rolling boulder stops after 64 rounds | se_use.md §5c | No limit |
| A member other than the leader mounting or leaving a turtle doesn't move the view | se_use.md §5w | Sets the party's tile to that member's, so the view jumps to it until the leader next moves |
| The raft carrying the party stays a map object that moves with it (as U6's vessels in the engine) | se_use.md §5w | Actor 0 becomes the raft and the object is deleted; the same on screen, it matters only to the save (item 10) |
| A step of the raft costs 5, as a vessel's in the engine | se_use.md §5w | Not read yet |
| Eggs are looked at when the party enters a new 8 x 8 block, within 20 tiles of the party, as U6's are | se_eggs.md §2 | Its 40 x 40 window, moved in steps of 8, with the 20 measured from the window's centre |
| A hatchling takes the lowest free slot of 192-254 | se_eggs.md §4 | The last slot freed first |
| A dead or unloaded big creature's pieces are removed | se_big.md §7 | Deletes the pieces and leaves the second remains (§3i); only a creature with no remains is moved to level 6 |
| A death leaves the remains as a map object holding the victim's things, and the actor leaves the map | se_combat.md §3i | The actor stays in its slot as its remains (status bit 0x10); the same on screen, it matters to the save (item 10) and to who stands up at Intanya's hut |

| Looking at a dead creature names its remains object and searches it like a body | se_look.md §7 | The dead actor is looked at as an actor: named by what it was, never searched (follows from the row above) |
| G)et of gold puts it in the party's pool, as everywhere else in the engine | se_look.md §5 | Gold (69) is a counted thing taken into the leader's pack |
## Waiting on other items
Read in DOS, not yet built, listed in the rule specs: SE's levels (item 11: the rope, the hole in the
ceiling, the trap doors, the drum's scene), combat (item 6: the totems, the bell, fire's harm, a
SE's creature table for the turtle's other numbers), SE's music and sounds (item 9),
the border (item 8).
