# Rules: Savage Empire's level changes

> Provenance
> - Derived from : SE's GAME.EXE (`~/src/u6e-re/dos/se.bin`): the level change 0x8cb:0x855 (image 0x9505), its
>                  callers (the step at 0x1553a, U)se in the switch at 0x26330), the coordinate list at DS:0x4d4,
>                  and the objects' qualities in the starting objlist and object blocks (our mksave output)
> - References   : none
> - Tools        : objdump -m i8086; scratch scans of build/SAVE.SE
> - Validated by  : the qualities of each pair of caves (163 on the surface, 227 on level 2) lead to each other;
>                  `make check-se-use` (`SE level ...`); NOT YET compared with DOS on screen
> - Confidence   : probable
> - Last updated : 2026-11-27 (the gather and the arrival; the floor plates)

Implemented by: render/gamedemo.c (se_level_change, se_level_go, step_triggers, se_use_object). SE only.

## 1. What changes the level
- **Walking onto** a cave going down (163), a cave going up (227), the stairs (259) or the trap door (160), when
  the leader steps there and the party is not in solo mode (0x1553a); in solo mode `You must be in party mode to enter.`
  The same step onto a floor plate (68) runs another move (0x8cb:0xb8b, image 0x983b; §1b), refused in solo mode
  with the same words.
- The cave behind the waterfall (293,67) is shut by the falls (a waterfall's edge and object 195 cover its tile,
  both impassable) until a boulder dams them (se_use.md); the cave's own tile forces passage only over the ground.
- **U)se** of a rope (20; in solo mode `Not in solo mode!`) or of the hole in the ceiling (22): frame 1 and not
  solo climbs; frame 0 `You need a rope dropped from above.`; else `Not in solo mode!`.
- The jungle drum's scene (se_use.md §5r) moves the party to (243,204) on level 1.

## 1b. Floor plates (0x983b)
A plate moves the party within its level:
1. A combat in progress ends (0x1810:0x23b, when DS:0x2860 is set).
2. The gather (§3) in mode 1.
3. Where to: below the surface, the first other plate (68) in the object list with the same quality, at its x and
   y, the level unchanged; with none, the leader's own tile (the party gathers and arrives where it stands). On the
   surface, the plate's quality indexes a list of 15 (x, y) pairs at DS:0x5c0: 0 = (412,291), the start; 1-7 lead
   to the seven plates around (182-187, 452-457), 8-14 from those out to the plates of qualities 1-7. Past 14 DOS
   reads the text that follows the list; no plate has such a quality.
4. The view drawn, a pass of the world of 0 minutes (0x359:0x11b6), the arrival (§3) in mode 1, and the place's
   song comes back (0x2701:0x1f1). No level is loaded and the effects are not stopped.

The new game has 14 plates on the surface (qualities 1-14, one each) and 22 on level 3 (qualities 1-11, two each).

## 2. Where each leads (0x9505)
The destination comes from the object's quality, as an index into a list of (x, y) pairs. DOS indexes four bases
(DS:0x38c, 0x3dc, 0x4d4, 0x4f8) into one list that begins at DS:0x4d4, so the caves' qualities (101-120) reach far
into it:

| Object | Destination | Level |
|---|---|---|
| 20 rope | entry quality + 9 (base 0x4f8) | 1 |
| 163 cave (down) | entry quality - 62 (base 0x3dc): quality 101 = (45,130) | 2 |
| 22 hole in the ceiling (up) | entry quality (base 0x4d4) | 2 for quality 8, else 0 |
| 227 cave (up) | entry quality - 82 (base 0x38c): quality 101 = (291,69) | 0 |
| 259 stairs | (192,460) | 0 |
| 160 trap door | (157,137) | 3 |

The list has 59 entries (entry 0 is unset); the first (quality 1-8) are the holes' ways up, then the ropes',
the surface caves' exits and level 2's caves' landings. A combat in progress ends first (0x1810:0x23b).

After a rope: the hole in the ceiling (22) at the destination with the rope's quality turns frame 1 (the rope
hanging in it); if there is none, any hole there is deleted and a new one made, frame 1, with the rope's quality.

After the stairs, once the control panel's scene has happened (talk flag 1 bit 0x04): `The mechanism supporting
the trap door fails just as you and your companions leave the stairwell, sealing the entrance to the underground
city.` and the trap door at (192,460) is deleted.

## 3. Engine notes
- No message for the move (U6 prints "You descend."; SE prints nothing).
- **The gather** before the move (0x8cb:0x564, image 0x9214), with a mode: 0 for the cave going up (227), 2 for the
  rest. Mode 0: nothing is drawn. Otherwise up to 30 rounds; in each, every member (the sleepers woken first) on the
  party's level, not held and not dead: on the leader's tile it vanishes (the leader at once, in the first round),
  the view drawn just before; else it takes a step toward the leader's tile (0x130f:0x3ab8); failing that, within a
  tile (the larger of |dx| and |dy| under 2) it is put on the tile, and further off it tries a step N, E, S or W
  at random. The view is drawn after each round; the rounds stop after one in which nobody stepped. Then every
  member not yet gone is moved to the leader's tile on level 6 (out of the world).
- **The arrival** (0x8cb:0x754, image 0x9404): every member on the destination tile, the view drawn, then the
  followers' routine (0x14fb7, movement_turns.md) five times with its flag set, so a follower beside the others
  still moves in the second pass unless it stands on its own place; drawn after each unless the mode is 0 (both
  caves 163 and 227), 2 for the rest. Mode 1 is the floor plate's: it draws again after each member vanishes in the
  gather, and once in the arrival before the five rounds, with the party stacked on the tile (mode 2 does neither).
- The engine: as DOS, a drawn step every 73 ms (DOS draws as fast as the machine allows; slip 30's pace). Checked on
  screen 2026-11-27 (Falcon, Hatari): walking into the cave at (272,68) the Avatar vanishes, the party arrives on
  level 2 together. `SE level party came along` checks the arrival.
- DOS also lends tile 0x10b (the rope's way up) or 0x10c the picture of an object 21 the acting member carries; not
  built.
- A cave's second half (quality 0) leads nowhere in the list; the engine does nothing there.

## Test hooks
`make check-se-use`: `SE level cave down`, `SE level cave up`, `SE level party came along`, `SE level plate` (the
plate at (376,173), quality 14, to (183,452)), `SE level plate back` (quality 7 back, the party with the leader),
`SE level drum`.

## Status
IN PROGRESS: §1-§2 built 2026-11-22; §3's gather and arrival 2026-11-27; §1b's floor plates 2026-11-27 (seen on
screen in Hatari, Falcon: the step onto (376,173), the Avatar vanishing, the party arriving at (183,452)); left: the
picture lent from object 21, a comparison with DOS on screen.
