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
these). Each row says what DOS does, so any of them can be put back.

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

## Engine choices, for the user to confirm
| Difference | Where | What DOS does |
|---|---|---|
| A boulder that crushes things spares them by their tile's flags | se_use.md §5c | Reads the flag byte by the thing's place in its object table, not its tile |
| A rolling boulder stops after 64 rounds | se_use.md §5c | No limit |
| A member other than the leader mounting or leaving a turtle doesn't move the view | se_use.md §5w | Sets the party's tile to that member's, so the view jumps to it until the leader next moves |
| The raft carrying the party stays a map object that moves with it (as U6's vessels in the engine) | se_use.md §5w | Actor 0 becomes the raft and the object is deleted; the same on screen, it matters only to the save (item 10) |
| A step of the raft costs 5, as a vessel's in the engine | se_use.md §5w | Not read yet |

## Waiting on other items
Read in DOS, not yet built, listed in the rule specs: SE's levels (item 11: the rope, the hole in the
ceiling, the trap doors, the drum's scene), combat (item 6: the totems, the bell, fire's harm, a
SE's creature table for the turtle's other numbers), SE's music and sounds (item 9),
the border (item 8).
