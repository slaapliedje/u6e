# Rules: Savage Empire U)se

Games: Savage Empire only (U6's U)se is usecode_use.md; Martian Dreams is checklist item 13)
Implemented by: /render gamedemo.c — the SE entries of the game layer (`gl_se`: `use_at`, `usable`,
`use`), design/game_layer.md
Tracked in: /docs/INDEX.md · Checklist: design/se_md_checklist.md item 5

> Provenance
> - Derived from : READING SE's GAME.EXE 1.6 (Turbo C++, not packed; ~/src/u6e-re/dos/se.bin, image
>                  addresses below, code segment base 0x1dbd0). The disassembly stays outside the repo.
> - References   : none
> - Tools        : objdump -m i8086; our objsurvey for the object names (se_md_objn.md)
> - Validated by : doors — `make bot-smoke` (SE leaves the waking hut) and SEUSE.REQ; the rest NOT YET
> - Confidence   : confirmed for the dispatcher, the usable list and the doors; the rest is a map
> - Last updated : 2026-11-11

## 1. The dispatcher (image 0x25e46)

The verb echo ("Use-" and the thing's name) and the range test are the shared engine's. What SE adds:

1. **Nothing on the tile, in reach** (|dx| and |dy| at most 1, else `Out of range!`): the map tile
   decides.
   - a tree (19 map tiles, DS:0x1134) prints `Tree` and pulls a branch (0x23dff);
   - lava (map tiles 15-27, 170, 171) prints `Lava` and goes to the fire handler (0x2215f);
   - the yucca plant (map tile 0x34) prints `Yucca plant` and gets flax (0x23ef8);
   - anything else prints `nothing!`.
2. **An actor on the tile** is taken only if it is the turtle (302) or 307, or while the party is
   aboard (the acting slot is the vehicle's, one past the party). Otherwise the object under it is
   taken (0x2ec00), and so is anything under an object whose tile has tileflags3 bit 0x10 (ignore).
3. **Not in the usable list** (§2): `Not possible.` No usable thing under it is looked for (U6 looks).
4. **Aboard** with an object 0x21e5e accepts: `Not while aboard
   ship!`.
5. **Out of reach**: unless the object is carried (flags 0x18), it must be within one tile of the
   leader, else `Out of range!`.
6. **The switch** (81 cases, table cs:0x8965). Every case, and an object in the list with no case
   (217, 221), costs the leader 5 (0x130f:0x1dcc).

## 2. The usable list (DS:0x11aa, 84 words; 110 appears twice)

6 10 20 22 25 29 42 43 44 47 49 50 51 52 59 60 61 62 63 79 80 81 82 86 87 88 92 93 94 95 102-111 113
114 119 120 131 134 136 137 143 148 161 165 173 177 186 188 190 191 192 197 204 207 208 209 212 213 214
216 217 221 229 233 234 237 240 242 247 252 254 257 275 302 307

## 3. The cases (object → handler)

| objects | what | handler | built |
|---------|------|---------|-------|
| 234 door | the sliding door, §4 | 0x25150 | yes |
| 161 door | Yunapotli's door, §4 | 0x253d0 | yes |
| 275 door | the swinging door, §4 | 0x246a8 | yes |
| 93 102-111 113 114 120 197 213 214 | food: eat | 0x21df5 | no (old "You eat the") |
| 188 crate | search it | 0x1ec8a | no (old spill) |
| 6 giant gem | | 0x24302 | no |
| 10 magnesium ribbon | | 0x24f6b | no |
| 20 rope | | in the switch | no |
| 22 hole in the ceiling | | 0x8cb:0x855 | no |
| 25 44 52 obsidian knife, knives, scissors | cut: strips, skin, meat, feathers | 0x22488 | no |
| 29 119 rock hammer, metal hammer | | 0x21436 | no |
| 42 vine | | 0x24fde | no |
| 43 bell | | 0x21293 | no |
| 47 turtle bait | | 0x24783 | no |
| 49 notebook | Jimmy's shorthand | 0x24d88 | no |
| 50 bamboo flintlock | load | 0x21e67 | no |
| 51 camera | | 0x24edf | no |
| 59 60 mortar, grinding stone | grind | 0x210ad | no |
| 61 62 corn and bean stalks | | 0x23ea5, 0x23e52 | no |
| 63 jug of plachta | | 0x254bc | no |
| 79-82 bones, skeleton | rib bones | 0x23f4b | no |
| 86-88 the three totems | | 0xa5d:0x1bf3 | no |
| 92 folded skin | | 0x25c2e | no |
| 94 fire extinguisher | | 0x243e9 | no |
| 95 bottle of liquor | | 0x21c2d | no |
| 131 digging stick | | 0x21f60 | no |
| 134 cloth strips | | 0x23c06 | no |
| 136 metal bucket | | 0x235a9 | no |
| 137 240 grenades, devices | light | 0x21671 | no |
| 143 machine | | 0x25b44 | no |
| 148 jungle drum | | 0x2542b | no |
| 165 loom | | 0x23f9e | no |
| 173 242 254 257 oven, fire, fire pit | | 0x2215f | no |
| 177 wire screen | sift | 0x23b10 | no |
| 186 brazier | | in the switch | no |
| 190 metal head | | 0x24147 | no |
| 191 tarred cloth strips | | 0x23777 | no |
| 192 clay | | 0x2204a | no |
| 204 gunpowder | | 0x24c0a | no |
| 207 burnt logs | | in the switch | no |
| 208 209 torches, lit torch | | 0x20ecc, 0x25686 | no |
| 212 fishing pole | | 0x21b48 | no |
| 216 crystal garden | | 0x25d54 | no |
| 229 tar pit | | 0x23211 | no |
| 233 237 raft | | 0x21c98 | no |
| 247 headless metal statue | | 0x2423a | no |
| 252 crystals | | 0x25972 | no |
| 302 turtle | | 0x24b00 | no |
| 307 | | 0x249a9 | no |

## 4. Doors

A door's frame is the high 6 bits of its object word, so frame f is the tile basetile + f.

**234, the door (0x25150).** Shut, it fills its doorway: frame 5 is two tiles wide (tileflags2 0x80),
frame 7 two tall (0x40), anchored at the doorway's tile like the doorway (236) itself; the map ships
every door this way. U)se turns it along the wall, one tile over:

| frame | U)se | moves to | new frame |
|-------|------|----------|-----------|
| 5 | opens | (x-1, y) | 1 |
| 7 | opens | (x, y-1) | 3 |
| 1 | shuts | (x+1, y) | 5 |
| 3 | shuts | (x, y+1) | 7 |
| other | nothing happens, silently | — | — |

Shutting first looks at the tile it returns to. If any object there is not a door (234), a doorway
(236) or a stone doorway (226), and its tile lacks tileflags3 bit 0x10, the game prints `The door is
blocked.` and nothing moves. Opening looks at nothing. The level stays the same.

**Jimmy's door.** Opening the shut door at (388,565) on the surface, with Jimmy (actor 4) within 5
tiles across and 4 down of the view's centre, and his talk flag 4 clear (the flag bytes at DS:0x6efc
are the talk flags, objlist 0x18f1): his asleep bit (status 0x04) is cleared, the flag set (once
only), he joins the party (0xf8d:0x290), and his conversation opens (0xf79:0xe0, with his name shown).

**Joining** (0xf8d:0x290): the party takes five members besides Aiela (2), Yunapotli (11) and Johann
(5), who don't count towards the limit; anyone else is refused when it is full.

**161, Yunapotli's door (0x253d0).** With Yunapotli (actor 11) in the party (status bit 0x80) and
on screen: `Yunapotli closes its eyes and appears to concentrate.  There is a grinding
noise froom the direction of the doors...  and they slide open!` (the original's typo kept), and the
door is removed. Otherwise `You can't budge it!`. The conversation's EVENT does the same
(converse.md §6c).

**275, the swinging door (0x246a8).** Frame 1 opens to frame 3; frame 3 shuts to frame 1; other
frames do nothing. It doesn't move. Opening it with Aiela (actor 2) within 2 tiles east and south
(the test is signed, so any distance west or north passes) on the same level, while the Avatar's
talk flag 0 is clear, starts a cutscene (0x1dbfe: music 14, its own screen). The cutscene is item 9;
until it is built, the door just opens.

## 5. Open questions
- Whether actors are in the map window's object index (0xa72c) that the blocked test walks. Taken as
  objects only.
- Item 5 covers the remaining cases, one handler at a time.
