# Savage Empire: eggs, hatchlings and the orchid's wounds

Games: Savage Empire. U6's eggs are docs/rules/eggs.md; SE keeps the egg object model (an egg with its
embryos inside, status 0x40 = hatched on this approach) and changes nearly every rule around it.
Implemented by: /render gamedemo.c (`se_eggs_update`, `se_hatch_creature`, `se_orchid_wound`), /engine
monsters.c (SE's creature table, se_combat.md §4).

> Provenance
> - Derived from : SE GAME.EXE (DOS, Turbo C++), read in the RE workspace outside this repo: the window
>   rebuild 0x8faa, the unload 0xbcf7, the egg scan 0x26f50, the hatch 0x26b67, the creature spawn
>   0x267cd with its stat roll 0x26712 and kit 0x2674a, the toss 0x1810:0x394, the orchid's wounds 0x1a3f0
>   and the damage routine's call to it (0x1a99b).
> - References   : none (ScummVM's SE has no egg rules of its own).
> - Tools        : objdump (i8086) on the unpacked image; our objname/senames dumpers.
> - Validated by  : SEUSE self-test wants "SE egg ..." / "SE orchid ..." (§8); NOT yet compared with DOS running.
> - Confidence   : probable (read from the code, not observed)
> - Last updated : 2026-11-12

## 1. The egg

Object 230. Its quantity is the hatch chance in percent; its quality is unused (SE has no day/night
periods and no alignment digit). Its contents are the embryos: obj_n and a quantity each.

## 2. When eggs are looked at

DOS holds a 40 x 40 window of the map, moved in steps of 8 tiles as the party walks, and on every
move (and on a level change or a teleport):
1. **Unload.** Everything farther than 20 tiles from the window's centre on either axis, or on another
   level, is dealt with:
   - a temporary hatchling (actors 192-255 with status 0x20) is deleted;
   - an orchid bulb (78) lying on the map, temporary or not, becomes an egg: a new egg at its tile
     (chance 201, status 0x22: temporary), holding one carnivorous orchid (292, quantity 1); the bulb is
     deleted. The new egg is temporary like everything the object maker makes, so the next unload that
     finds it still far away deletes it: only a party that comes back before then sees it hatch;
   - any other temporary object is deleted;
   - an egg marked hatched (0x40) is re-armed;
   - object 247 has its quality cleared.
2. **Scan.** Each egg in the window, on the party's level, farther than 8 tiles from the party on
   either axis (or any distance after a level change or teleport), hatches (§3).

Nothing hatches while a game is being started or loaded (DS:0x2362).

## 3. Hatching (0x26b67)

An egg already marked 0x40 does nothing. Otherwise one roll of 1-100; a roll above the chance means no
hatch. Either way the egg is then marked 0x40 | 0x02.

For each embryo, the number made:
- the bamboo plant (127) and bamboo (203): 10-20;
- the myrmidex drone (323): 1 once talk flag 1 has bit 0x08; else 5-15 on level 1; else the embryo's
  quantity;
- the argent sergeant (301), the carnivorous orchid (282) and the people (310-326): 1 to the embryo's
  quantity, or exactly the quantity when the chance is above 100;
- anything else: one.

Each one made:
- **an object** (obj_n below 282):
  - a boulder (167) is made on the egg's tile, frame 3;
  - anything else goes to a tile 0-6 minus 3 tiles off on each axis. A tile with nothing on it that is
    open water, or whose tile flags have 0x02 or 0x04, is refused, and then that one isn't made (DOS
    goes on with whatever its register last held: slip 14, §7). The bamboo plant and bamboo are made with
    a quantity of 5;
  - all are temporary.
- **a creature** (282 and up): a new actor (§4). Its worktype is assault (8) when its status has 0x20 or
  0x40, else wander around (0x8f).

The first one an egg makes stays where it was made. Every later one is tossed: 8 tries at a tile
(d4 + d4 - 5) off on each axis, away from a level change or teleport never within 5 tiles of the party
on both axes, on a tile it may stand on. With no such tile it is deleted.

After all embryos, if a creature with status 0x20 hatched and nothing has been heard yet this turn:
`You hear something to the <direction>!` (North, Northeast, East, Southeast, South, Southwest, West,
Northwest: from the party towards the egg), with a sound (item 9).

## 4. A new creature (0x267cd)

The actor goes into the first free slot of 192-254 (DOS's free list starts at 192 and takes back the
last one freed first). It stands at the given tile with its obj_n, status 0x20 (temporary), worktype 8.

With a row in the creature table (se_combat.md §4): strength, dexterity, intelligence and hit points are
each `b - t + rand(0..t) + rand(0..t)`, `t = b / 2` (b itself when t is 0); level 0; its alignment status
the row's, except the man (316), who is hostile (0x60) until talk flag 53 has bit 0x80. With no row:
10, 10, 10, 30 hit points, level 1, status 0x20. Experience 0.

Its frame: 1 for the pteranodon (288) and the creatures 283, 300, 327 and 330-335; 3 for the gorilla
(296). The big creatures of several tiles are given their other pieces around them.

**Its kit (0x2674a):** the man 320 an obsidian sword (33) and bark armor (9), both readied; the man 316 a
blowgun (32) readied and 30 poisoned darts (36), or an obsidian knife (25) readied if the blowgun could
not be given. Everyone else nothing. All temporary.

**The carnivorous orchid (292)** is the root: frame 0, its quality 4 (the heads it has). It puts out four
heads (282), one on each tile north, east, south and west of it, made as new creatures; each head's
direction is its side, its quality the root, its alignment the root's, its worktype 13.

## 5. The orchid's wounds (0x1a3f0)

The damage routine hands every wound of a 282 or a 292 here, after the hit's flash:
- **the root** (a 292 that is an actor): every head (282) among actors 16-255 takes half the wound;
- **a stalk** (a 292 that is an object; the heads leave them as they reach out, §6): its head (the
  stalk's quality) takes the whole wound;
- **a head** with more hit points than the wound loses them. Otherwise it dies (in silence; with a flash
  when within 5 tiles of the party), its root has one head fewer, and its stalks go: from the head's tile,
  step by step along the head's direction, each stalk of this head found there is deleted and the next
  step's direction read from that stalk's frame and the old direction (table DS:0x10c3). When the root's
  count reaches 0 (or wraps to 255) the root dies too, leaving an orchid bulb (78, temporary) on its tile.

What it gives the striker as experience: 10 when the root dies, else nothing. DOS gives the head's
remaining hit points when it lives and an unset register when it dies but the root lives (slip 15, §7).

## 6. Not built here
- How the heads reach out and leave stalks, and what worktype 13 does in SE (the creatures' doings,
  item 6).
- The frames of §4 and the big creatures' pieces (the renderer draws SE's creatures from their roster).
- The sound with the message (item 9).

## 7. Slips and engine choices
- Slip 14: a refused tile for an object embryo makes nothing (DOS reuses the last thing it made).
- Slip 15: an orchid head's wound gives no experience unless the root dies (DOS gives a register's leftover).
- Engine: eggs are looked at when the party enters a new 8 x 8 block or teleports, within 20 tiles of
  the party (DOS: its 40 x 40 window, moved by 8), as U6's are.
- Engine: free slots are taken lowest first, not last freed first.
- Engine: a game start or load looks at no eggs, as DOS; the first block change does.

## 8. Test hook
SEUSE self-test: `SE egg chance`, `SE egg counts`, `SE egg orchid heads`, `SE egg worktype`,
`SE egg rearm`, `SE egg bulb`, `SE orchid head`, `SE orchid root`.

## 9. Open questions
- What the bulb's egg being temporary means for planting orchids: as read, an orchid only grows back
  if the party returns before the window moves again.
- The 0x02 bit DOS sets on a hatched egg.
- Object 247's cleared quality.
- A wound to a root reaches every orchid head among actors 16-255, not only its own (built as read).

## Status
IN PROGRESS: §1-§5 built; §6 waits on the creatures' doings.
