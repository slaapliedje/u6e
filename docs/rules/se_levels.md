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
> - Last updated : 2026-11-22

Implemented by: render/gamedemo.c (se_level_change, se_level_go, step_triggers, se_use_object). SE only.

## 1. What changes the level
- **Walking onto** a cave going down (163), a cave going up (227), the stairs (259) or the trap door (160), when
  the leader steps there and the party is not in solo mode (0x1553a).
- **U)se** of a rope (20; in solo mode `Not in solo mode!`) or of the hole in the ceiling (22): frame 1 and not
  solo climbs; frame 0 `You need a rope dropped from above.`; else `Not in solo mode!`.
- The jungle drum's scene (se_use.md §5r) moves the party to (243,204) on level 1.

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
- DOS's effects around the move (0x9214 and 0x9404, called with 0 for the caves and 2 for the rest: probably the
  walk in versus a fade) are not built: the view changes at once.
- DOS also lends tile 0x10b (the rope's way up) or 0x10c the picture of an object 21 the acting member carries; not
  built.
- A cave's second half (quality 0) leads nowhere in the list; the engine does nothing there.

## Test hooks
`make check-se-use`: `SE level cave down`, `SE level cave up`, `SE level drum`.

## Status
IN PROGRESS: §1-§2 built 2026-11-22; §3's effects not built; not yet compared with DOS on screen.
