# Known deviations from the reference (U6)

Games: Ultima VI. The rules follow the DOS release (GAME.EXE 4.5), decided 2026-11-04. From 2026-11-05 the aim is that this
register is empty. What's left is listed by why it's left.

> Provenance
> - Derived from: reading GAME.EXE 4.5. The rule specs listed name the routines read.
> - Last updated: 2026-11-05 (turn order and worktypes, carried containers, deaths, spells, rest, M)ove, lighting; the user kept the additions and accepted the machine rows; the three open questions settled in DOS; the cast effect found)

## A choice for the user: DOS's own faults we don't copy
| Difference | Where | Why it's a question |
|---|---|---|
| Help or the Avatar's death aboard a vessel leaves the vessel behind | combat.md §3d | DOS leaves the Avatar's figure a ship in the castle |
| The Covetous switch (quality 113) finds its doorway of quality 0 | usecode_use.md (Levers) | DOS has no such rule, so its data can't open that door. Copying DOS would block the way |

## Kept by the user's choice (2026-11-05): things added to the original
| Addition | Where |
|---|---|
| 8 save slots with two older copies each, and a slot page on S)ave and Ctrl-R. The original keeps one save and asks "Save game?" | save_load.md §2b |
| Transfer a Character looks on any drive and in the game's folder, for a DOS or ST save | transfer_saves.md §5 |
| Transfer decodes ST Ultima IV's BCD numbers, which the ST original misreads | transfer_saves.md §1 |
| Create and Transfer don't refuse a game in progress | transfer_saves.md §4 |
| The DOS sound effects are a setting (Ctrl-X, on by default); the ST original has none | sound_effects.md |

## The machine (accepted by the user, 2026-11-05)
| Difference | Where |
|---|---|
| Sound effects play on the YM at 200 Hz, where DOS drives the speaker faster; nobody has heard them yet | sound_effects.md |
| The view dissolve takes about 0.6 s on a TT and 3 s on an 8 MHz ST, drawn in steps on TT and Falcon; DOS runs it at CPU speed | moongates.md §6 |
| Schedule walkers re-use a route of up to 16 steps, as DOS keeps its stored path, but found by our own search | design/performance.md |
| No memory for a level leaves the party where it stood; DOS can't fail there | moongates.md §5 |

## Not built yet
| Difference | Where | How to settle it |
|---|---|---|
| The spell-cast effect: DOS runs a routine (15 callers, X-ray among them) that sounds a sweep pitched by the spell's circle and redraws the map window in shifted colours for about 2.8 s before the spell acts. u6e has neither | magic_skills.md | Read the routine and its video-driver calls in GAME.EXE, then build it; seen in DOS while timing X-ray |

## Settled in DOS (2026-11-05, DOSBox-X, the GOG copy)
- The far cranks work their own bridge (DOS holds only the superchunks round the party); u6e now finds the bridge the same way.
- The mealtime lasts 3 s from `Mealtime!`, a member every 4 ticks; u6e now paces it so.
- X-ray holds 5.48 s (100 ticks), then `Done`; u6e now holds it so, and potions print nothing else, as in DOS.
