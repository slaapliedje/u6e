# Savage Empire: the big creatures

Games: Savage Empire. U6's big monsters are single objects drawn over two or four cells; SE adds creatures
made of two or three objects (the creature plus its pieces) that move, turn and bite as one.
Implemented by: /render gamedemo.c (`se_big_step`, `se_big_turn`, `se_big_carry`, `se_big_hatch`, `se_big_drop`,
`se_dist`, `se_actor_tile`).

> Provenance
> - Derived from : SE GAME.EXE (DOS, Turbo C++), read in the RE workspace outside this repo: the kind's base
>   0xa5d:0xdef, its piece list 0xa5d:0xd5b, the step 0x1693c, the enter test 0x13355 with its kind tests
>   0x130f2/0x1313d/0x13160/0x13197/0x131ba/0x131dd, the turn 0x142e4 (big branch 0x149a4, its clearance
>   0x1415d, its phase test 0x14289), the move 0xa5d:0x1c76 and its carry 0xb0f8, the death 0x1a6ad, the
>   spawn 0x267cd, the reach 0x1be13 (its table distance 0x1bdc2).
> - References   : none.
> - Tools        : objdump (i8086) on the unpacked image; TILEFLAG/BASETILE read with our own dumpers.
> - Validated by  : SEUSE self-test wants "SE big ..." (§9); NOT yet compared with DOS running.
> - Confidence   : probable (read from the code, not observed)
> - Last updated : 2026-11-13

## 1. Which creatures

| creature (actor kind) | pieces | frame shifts (creature, pieces) | phases |
|---|---|---|---|
| 284 | 285 | 1, 0 | 2 |
| 289 allosaurus | 290 | 1, 0 | 3 |
| 294 tyrannosaurus | 293 and 295 | 1; 2, 1 | 3 |
| 297 robosaurus | 298 | 1, 0 | 2 |
| 303 stegosaurus | 304 | 1, 0 | 2 |
| 305 triceratops | 306 | 1, 0 | 2 |

Every kind in a row maps to the row's creature (the "base"); anything else has none.

A piece is an ordinary map object whose quality is the creature's actor number. Bits 0x80/0x40/0x20/0x10
of tileflags1 of a creature's or piece's current tile say that the neighbour north/east/south/west
continues the creature. A piece belongs to a creature when it stands on the neighbour the creature's tile
names, has the creature's number as quality, and its own tile names the way back (the opposite bit).

The art lies so that, facing north or south, the creature's object is the north end with its pieces
south of it (the tyrannosaurus: 293 north, 295 south); facing east or west, the creature is the west end with
its pieces east (the tyrannosaurus: 293 west, 295 east). The tiles are double-high or double-wide, so a
two-object creature covers three cells in a line and the tyrannosaurus two by four.

## 2. Frames

A creature's frame is `(code << shift) - 1`, a piece's `(code << its shift) - 1`, where
`code = phases * facing + phase + 1` (facing 0 north, 1 east, 2 south, 3 west; phase 0 .. phases-1).
On each step the phase moves on: with two phases it alternates 0, 1; with three it runs 0, 1, 2, 1, 0 ...
(a flag per actor remembers the way back: phase 0 clears it and goes to 1, phase 2 sets it and goes to 1,
phase 1 goes to 0 with the flag set, else to 2). The current facing and phase are read back from the
creature's frame: `(frame >> shift)` divided by, and modulo, the phases.

The two-cell creatures of one object (283, 300, 327, 330-335: double-high facing north or south, double-wide
facing east or west) have frames `(2 * facing + phase + 1) * 2 - 1`, the phase alternating (frame & 2 set: phase 0,
else 1), the facing `frame >> 2`. A quarter turn from north or south needs room (§5) on (x-1, y), one from east or
west on (x, y-1). A hatched one starts in frame 1.

The pteranodon (288, one object, wide facing north or south and tall facing east or west) has three phases:
frames `(3 * facing + phase + 1) * 2 - 1`, the facing `(frame >> 1) / 3`, the phase `(frame >> 1) mod 3`, moving on
as the big creatures' three phases do (after the room test). A quarter turn needs room (§5) on (x-1, y) from north or
south and (x, y-1) from east or west, as above (slip 24). A hatched one starts in frame 1.

The gorilla (296) and the raft carrying the party (237) are 2x2 single objects: frames
`(2 * facing + phase + 1) * 4 - 1`, the phase alternating (frame & 4 set: phase 0, else 1). No room is needed to
turn. A hatched gorilla starts in frame 3; the raft is boarded in frame 3 (se_use.md §5w) and turns and paddles as
it moves. DOS reads the old facing as `frame >> 4` (slip 25).

## 3. A step (0x1693c)

A step is in one of the eight directions. In order:
1. A party member in combat does not step more than 8 tiles from the leader (as U6).
2. The enter test (§4). Refused: no step.
3. The turn (§5), for the creatures of §1. Refused: no step.
4. The move: the creature goes to the new tile, and each piece to the tile beside the new tile that the
   creature's (new) tile names, keeping its side; nothing is checked for the pieces (0xb0f8).
5. The facing is set to the step's direction.

## 4. The enter test (0x13355)

Kinds of normal size are 282, 291, 292, 299, 301, 302, 307-326 and 336 and up. Any other kind (the creatures of
§1, 283, 288, 296, 300, 327, 330-335 among them) may not step diagonally (329 excepted), and is tested on
other tiles than the one it steps to. With (x, y) the tile stepped to:

| kinds | east | west | north | south |
|---|---|---|---|---|
| 284, 289, 297, 303, 305 | (x+1, y) | (x-1, y) | (x, y-1) | (x, y+1) |
| 294 | (x+1, y), (x+1, y-1) | (x-2, y), (x-2, y-1) | (x, y-2), (x-1, y-2) | (x, y+1), (x-1, y+1) |
| 296 | (x, y), (x, y-1) | (x-1, y), (x-1, y-1) | (x, y-1), (x-1, y-1) | (x, y), (x-1, y) |
| 283, 300, 327, 330-335 | (x, y) | (x-1, y) | (x, y-1) | (x, y) |
| 288 | (x, y), (x, y-1) | (x, y), (x, y-1) | (x, y), (x-1, y) | (x, y), (x-1, y) |

That is, the new leading edge of the creature. Each tested tile must take the creature by its movement, hold
no actor, and hold no piece of any big creature (its own included: a piece is only ever behind the edge).

## 5. The turn (0x142e4, the creatures of §1)

With the new facing f (the step's direction halved, so a diagonal counts as the one before it clockwise)
and the old facing read from the frame (§2), the phase moves on and the code is `phases * f + phase + 1`.
Then, when the turn is a quarter turn, room is needed (0x1415d) on these tiles around the creature (x, y):

| from | to | the tyrannosaurus | the others |
|---|---|---|---|
| north or south | east or west | (x-2, y-1), (x+1, y-1), (x-2, y), (x+1, y) | (x-1, y), (x+1, y) |
| east or west | north or south | (x-1, y-2), (x-1, y+1), (x, y-2), (x, y+1) | (x, y-1), (x, y+1) |

A tile has no room when its map tile is impassable (tileflags1 0x02) or a wall (0x04), when another actor is
on it, or when an object on it is impassable (tileflags1 0x02 of its tile) and is not the creature's own.
For the pteranodon (288), only a wall (0x04) takes the room, of the map tile or of any object's tile; for the
plesiosaur (300), a map tile or any object's tile that is not water (tileflags1 0x01).
No room: the step is refused (the phase flag has already moved on). Otherwise the creature takes its new frame,
and each piece (found from the creature's old frame, §1) takes its new frame; on a quarter turn each piece
also moves from (x+dx, y+dy) to (x+dy, y+dx) (north to west, south to east and back). A half turn moves
no piece: the frames alone turn the creature round.

## 6. Hatching (0x267cd)

A hatched creature of §1 starts in frame `(1 << shift) - 1` (facing north, phase 0). Each of its pieces is made
on the neighbour its tile names whose piece tile, in frame `(1 << the piece's shift) - 1`, names the way back,
with that frame, quality the creature, on the creature's level.

## 7. Death and removal

A dying big creature lies as its remains in its own slot (se_combat.md §3i): its pieces are deleted and the second
remains made beside it. A sleeping one lies the same way and stands up again on waking (se_combat.md §3i). The
engine removes the pieces at death, as when a temporary creature is unloaded far from the party (se_eggs.md §2).

## 8. The reach (0x1be13)

The distance from an attacker to a tile, as used for its weapon choice and to decide between a blow and a
walk (se_ai.md): for kinds of normal size (§4), U6's table distance (9 beyond 7 tiles). For the others it is 1
when the tile is in the box below around the attacker (x, y), else 9. The facing is `frame >> 2` for 283, 300, 327,
330-335 and 284, 297, 303, 305; `frame / 6` for 288, 289, 294; `frame >> 3` for 296; any other kind is always 9.

| kinds | north | east | south | west |
|---|---|---|---|---|
| 283, 300, 327, 330-335 | x-1..x+1, y-2..y-1 | x..x+1, y-1..y+1 | x-1..x+1, y..y+1 | x-2..x-1, y-1..y+1 |
| 288 | x-1..x, y-1.. | ..x+1, y-1..y | x-1..x, ..y+1 | x-1.., y-1..y |
| 284, 289, 297, 303, 305 | x-1..x+1, y-2..y | x..x+2, y-1..y+1 | x-1..x+1, y..y+2 | x-2..x, y-1..y+1 |
| 296 | x-2..x+1, y-2..y-1 | x..x+1, y-2..y+1 | x-2..x+1, y..y+1 | x-2..x-1, y-2..y+1 |
| 294 | x-2..x+1, y-3..y-1 | x..x+2, y-2..y+1 | x-2..x+1, y..y+2 | x-3..x-1, y-2..y+1 |

The boxes are the cells around the head. Slip 23: the pteranodon's (288) boxes each lack one bound (an open
end is marked ".."), so a foe far away in that direction counts as in reach; the engine closes each box on the
missing side by mirroring the bound it has (north: y-1..y; east: x..x+1; south: y..y+1; west: x-1..x).

## 9. Test hook

SEUSE self-test: `SE big hatch`, `SE big step`, `SE big turn`, `SE big blocked`, `SE big reach`, `SE big drop`,
`SE big ptera`, `SE big gorilla`, `SE big raft frame`, `SE big plesio room`.

## 10. Not built / open
- Other turns DOS makes through 0x142e4 (an actor turned to face a direction from elsewhere, perhaps on talk or
  a push) are not traced; the engine turns these creatures only on their steps.
- The picture of a creature in a panel (0xa638) uses the last phase of the facing for every big kind.

## Status
DONE: §1-§8 built (slips 23-25 as the intent); §10 open.
