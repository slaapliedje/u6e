# Rules: Savage Empire M)ove and D)rop

Games: Savage Empire only (U6's M)ove is in usecode_use.md and equipment.md §5; Martian Dreams is item 13)
Implemented by: /render gamedemo.c, the SE entries of the game layer (design/game_layer.md)
Tracked in: /docs/INDEX.md · Checklist: design/se_md_checklist.md item 5

> Provenance
> - Derived from : READING SE's GAME.EXE (M)ove at image 0x202b8, D)rop at 0x1f530, with its helpers 0x1e693, 0x1e5d2,
>                  0x1e66c, 0x1e56a, 0x1e58c, 0x1f2fc, 0x1f4cd, 0x201ed, 0xf8d:0x159). The disassembly
>                  stays outside the repo.
> - References   : none
> - Tools        : objdump -m i8086; LOOK.LZC for the names
> - Validated by : `make check-se-use` (SEUSE.BIN): the containers, giving, the gem stand, a thing lost
>                  in the water, D)rop's breakables, the device and a lit grenade; not yet against DOS running
> - Confidence   : probable (read, not yet seen running)
> - Last updated : 2026-11-11

## 1. Overview

SE's M)ove is U6's in shape: a thing on the map is pushed one tile in a direction; a thing in a pack goes
to another member, into a container, or out of one. The words and several rules differ. Every M)ove
that does something costs the leader 5.

## 2. A thing on the map

1. Nothing chosen, or the container shown in the pack area: `nothing!`. Else its name, a new line.
2. Actors 31, 47 and 55: `Not possible.` A thing whose weight (the weight table) is 0: `Not possible.`
3. Further than a tile from the leader: `Out of range!`.
4. `To ` and a direction: anything else prints `nowhere.` and nothing happens; a direction prints its
   name and `.`.
5. An actor: `Failed.` when it is the leader, when it lies (frame & 3 = 3) and is not a body (249), or
   when the strength test fails: a roll of 1-30 below ((its strength / 2 + 30) - the leader's
   strength) / 2, strengths as DOS works with them (se_use.md §5g).
6. The 5 points are paid.
7. The tile it would go to: deep water (tile flags 1 with bits 0x01 and 0x02), lava (map tiles 15-27,
   170, 171) or a slope (the boulder's list, se_use.md §5c), with nothing on it: a thing is lost there
   (it is gone); an actor: `Failed.`
8. The first thing on that tile, if it is a container that takes it (§4) and the moved thing's weight
   isn't 255: into it (as M)ove into a container; when that fails it goes onto the tile).
9. Else `Blocked!` when it can't stand there (a diagonal also needs one of the two tiles beside the way);
   else it moves.

## 3. A thing in a pack

1. The member isn't here (§5): `That person isn't here!`.
2. A stack of more than one that isn't readied asks `How many? `; 0 or Esc, or more than there is: `0`
   and nothing happens.
3. `To ` and the crosshair: nothing under it: `nobody.`
4. A thing on the map under the crosshair must be in reach: an actor here (§5), another thing within a
   tile of the leader; else `Out of range!`.
5. **The gem stand (66):** the gem goes to (x-1, y-1) of the stand, `You place the gem on the
   stand.`, and the first time the gem's scene (se_use.md §5d). DOS takes anything moved
   onto the stand for the gem; the engine takes only the gem, and anything else is `nobody.` (se_deviations.md 9).
6. **An actor:** the member itself: `yourself.` (and the 5 points). Else its name and `.`, then:
   `That person is not around!` when not here (§5); `Only within the party!` when not in the party;
   `<name> is carrying too much already.` when its strength x 20 is less than what it carries and the
   thing weigh (a stack: the weight table's figure times the count, a tenth of that for the light
   kinds), or when it carries 16 things already. Else the thing goes to it (a readied one is taken off
   the giver first), and the 5 points are paid.
7. **Another thing:** not carried: `nobody.` Else, with `To ` and the container's name and two new lines
   before each refusal:
   - it doesn't take the thing (§4): `You can't do that!`;
   - the two aren't carried by the same one: `That container belongs to someone else!`;
   - (`That container is already full.` at 16 things: never reached, §4 refuses first);
   - the thing is readied: `You have to unready it first.`;
   - the thing is the container: `How can a container go into itself!` (only reached by an open crate);
   - the thing is in it: `out of <container>` and a new line: it goes up a level, to whoever or whatever
     holds the container;
   - else `into <container>` and a new line: it goes in; when it can't, `That container is full.`
   Both cost 5.

## 4. What a container takes (0x1e693)

The containers (DS:0x1122): mortar 59, grinding stone 60, cauldron 258, jug 181, crate 188, basket 183,
pouch 182, pot 184, empty jar 97. A container takes a thing when it holds fewer than 16 things, the
thing isn't an actor, and:

| container | takes |
|-----------|-------|
| mortar, grinding stone | the powders (DS:0x115a): chocolatl 54, pinde 55, yopo 56, corn meal 93, corn 108, beans 109, sulfur 123, charcoal 129, potassium nitrate 130, gunpowder 204, magnesium ribbon 10 |
| empty jar | a powder, if the jar is empty or its first thing is the same kind |
| crate | anything, while it is open (frame 0) |
| the others | anything but a container |

**The jar's picture:** after something goes into the jar its frame follows the kind: 1 for corn meal,
corn and sulfur; 2 for the magnesium ribbon and potassium nitrate; 3 for chocolatl, pinde, yopo, beans,
charcoal and gunpowder. When the whole of what it holds is taken out (out of it, or given away from it),
its frame goes back to 0.

## 5. "Here" (0xf8d:0x159)

An actor is here when the party is aboard and it is in the party, or when it is within 4 tiles of the
leader across and up or down and its tile in the view is seen (not black).

## 6. D)rop (0x1f530)

1. Not carried, or the container shown: `nothing!`. Else its name and a new line; a stack asks `How
   many? ` (as §3); then `Location: `.
2. The place must be in reach of a throw (range 8, the line clear) and one of: a tile it can stand on, a
   tile with a top to it, a container there that takes it (§4), or a tile that swallows things (§2 step
   7). Else `Not possible.`
3. A readied thing comes off first. The member pays 3. It flies there.
4. Where it lands:
   - a tile that swallows things: it is gone;
   - thrown more than a tile and on the breakable list (DS:0x1170: 3 6 25 59 60 95 97 132 133 181 184
     137 63): it is gone, `It broke!` (a container: DOS loses what it held; the engine spills it onto the tile,
     se_deviations.md 10); the soft clay pot (132) instead
     `The soft clay pot went squish.` and one clay (192) lies there. The list holds the grenade (137),
     not the lit one (138), so DOS's code for a lit grenade bursting where it lands is never reached and
     an unlit one breaks instead. The engine's list has the lit one: thrown, it bursts there (a grenade,
     se_use.md §5b; dropped beside you it lands and burns on) and an unlit one lands whole (se_deviations.md 5);
   - a container there that takes it: into it;
   - the activated device (241): the fuse is cleared and it goes off there (se_use.md §5b);
   - else it lies there. An orchid bulb (78) dropped gets status bit 0x20 (§7).

## 7. Open questions
- What 0xe13:0x14f3 refuses for a carried thing before `That person isn't here!` (it ends the M)ove
  silently); probably the container being shown.
- What status bit 0x20 means on what M)ove makes (se_use.md §5f), and on a dropped orchid bulb.
