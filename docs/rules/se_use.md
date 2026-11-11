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
> - Validated by : doors — `make bot-smoke` (SE leaves the waking hut); `make check-se-use` (SEUSE.BIN) for the doors, ground, fire, food, crate, grenades and gunpowder; not yet against DOS running
> - Confidence   : confirmed for the dispatcher, the usable list and the doors; the rest is a map
> - Last updated : 2026-11-11 (the boulder and the waterfall)

## 1. The dispatcher (image 0x25e46)

The verb echo ("Use-" and the thing's name) and the range test are the shared engine's. What SE adds:

1. **Nothing on the tile, in reach** (|dx| and |dy| at most 1, else `Out of range!`): the map tile
   decides. Each prints its name on the echo line, then:
   - a tree (map tiles 50 51 64-72 192-195 198 179 180 130, DS:0x1134): the acting member gets a
     tree branch (206): `You pulled a branch from the tree.` (0x23dff);
   - lava (map tiles 15-27, 170, 171): the fire's `On ` (§5);
   - the yucca plant (map tile 0x34): flax (210): `You got some flax from the yucca plant.`
     (0x23ef8);
   - anything else: `nothing!`, and no time passes.

   A new thing for a pack (0xa5d:0x102a) is refused when the member's strength × 20 can't take its
   weight on top of what they carry: `You are carrying too much already.`
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

## 2. The lists

**Stackable** (DS:0x806, 48 words; 137 twice): 10 26 27 36 41 44 45 47 48 54 55 56 69 70 72 73 74 93
102-105 108-110 115 120 123 129 130 132-134 137 139 140 180 191 192 204 206 208 210 211 214 240 252.
LOOK.LZC's plural markers, which U6 uses for this, miss 18 of them (gold, corn meal, meat, corn,
charcoal, cloth, clay, gunpowder, flax and others), so mkworld takes SE's from this list. A stackable's
record keeps the high byte of its quantity in the quality byte (objblk.md §2).

**Usable** (DS:0x11aa, 84 words; 110 appears twice):

6 10 20 22 25 29 42 43 44 47 49 50 51 52 59 60 61 62 63 79 80 81 82 86 87 88 92 93 94 95 102-111 113
114 119 120 131 134 136 137 143 148 161 165 173 177 186 188 190 191 192 197 204 207 208 209 212 213 214
216 217 221 229 233 234 237 240 242 247 252 254 257 275 302 307

## 3. The cases (object → handler)

| objects | what | handler | built |
|---------|------|---------|-------|
| 234 door | the sliding door, §4 | 0x25150 | yes |
| 161 door | Yunapotli's door, §4 | 0x253d0 | yes |
| 275 door | the swinging door, §4 | 0x246a8 | yes |
| 93 102-111 113 114 120 197 213 214 | food: `You eat the food.`, one of a stack (or the thing) used up; nothing more | 0x21df5 | yes |
| 188 crate | open (frame 0): `Searching here, you find A, B and C.`, the contents spill out; shut: nothing | 0x1ec8a | yes |
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
| 137 240 grenades, devices | light, §5b | 0x21671 | yes, but trees |
| 143 machine | | 0x25b44 | no |
| 148 jungle drum | | 0x2542b | no |
| 165 loom | | 0x23f9e | no |
| 173 242 254 257 oven, fire, fire pit | the fire's `On ` (§5) | 0x2215f | yes |
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

## 5. "On ": the second thing (0x5d0:0x22c8)

Many uses ask for a second thing: `On ` is printed after the echo and a cursor picks a map tile or
a thing in the pack (29 functions call the picker). The answer's name follows on the same line; a
map thing must be within one tile of the acting member (`Out of range!`); no answer prints
`On nothing!`. The engine's verb is `X`: the arrows walk the crosshair and Enter takes the tile, U
picks from the pack instead (U again for the next thing, Enter takes it), Esc answers nothing.

**The fire** (0x2215f; lava, the oven, a fire, the fire pit):

| put on it | what happens |
|-----------|--------------|
| soft clay pot (132) | `You fired the pot.` One pot used; the member gets a fired clay pot (133), or it lands at their feet |
| corn meal (93) | `You made corn tortillas.` The whole stack goes; that many tortillas (102) go to the member if it was carried, else onto the meal's tile |
| tree branch (206) | `You made some charcoal.` One branch used; 10 charcoal (129) to the member, or at their feet |
| 4 5 7 9 13 17 36 39 42 45 134 171 180 183 191 210 (DS:0x118a) | `You incinerated the <name>.` The whole thing goes |
| gunpowder (204) | all of it goes, and it blows as a grenade (§5b) where it lies, or where its carrier stands |
| anything else | `No effect.` |

### 5b. Grenades and the device (0x21671)

U)se on a grenade (137) or a device (240), on the map or carried (readied too):

- frame not 0: `No effect.`;
- a fuse already burning (there is one, DS:0x2842, for both kinds): `A grenade has already been lit.` or
  `A device has already been activated.`;
- else one of the stack is used up and a lit grenade (138) or an activated device (241) takes its place:
  on the map at the same tile; carried, to the carrier (into the same bag in DOS; the engine puts it in
  the pack), or at their feet if it is too heavy. `Grenade lit!` / `Device activated.` The fuse is 3.

**The fuse** (image 0x4746): every call takes away the turns passed (1 a turn, 60 for an hour's rest); when
it reaches 0 the lit thing (0x21838) is deleted and blows where it lies, or where its carrier stands.

**The blast** (0x1810:0x1941 grenade, 0x19c1 device): the effect (0x276b:0xe85) sends 9 rays from the
centre to the 3x3 around it (tile 0x105, the device 0x109); the actors they reach and those on the centre
are hit (actors only: the hit list is a 256-bit actor set). A grenade does 1-20 to each, the device 1-35,
and only to Myrmidex drones (323) and the queen (328). The hit (0x1810:0x289b) is flat: no armour. Then, in
the 3x3 (0x218f5):

- a grenade (137) there: one of it is lit and goes off at once (a chain); this clears the fuse, so a
  grenade lit elsewhere then stays lit for good (DOS's own);
- a boulder (167): pushed, and it may roll (§5c);
- a tree (263): hit for 30; a felled tree comes down across its parts (0x1abcd, SE's object damage,
  checklist item 6) — **not yet built**;
- a web (17): gone.

DOS plays sound 6 (the device 0x12 and 0x1d); SE's sounds are not yet built.

### 5c. The boulder (0x1810:0x14ea)

The boulder (167) fills 2x2, anchored at its bottom right (x, y), like SE's other big things. A blast at
(bx, by) in the 3x3 around the anchor meets it (dx = bx - x, dy = by - y). Nothing happens once its
quality is 1 (DOS plays sound 0x1c). Three lists of map tiles steer it: *slope* (DS:0xf80, 68 tiles),
*no-south* (DS:0x1008, 19) and *no-east* (DS:0x102e, 24).

**The push.** It goes one tile west when dx = 1, or dx = 0 and dy is 0 or -1; else one tile east when
dx = -2, or dx = -1 and dy is 0 or -1, unless the tile at (x+1, y) is no-east. Likewise north when
dy = 1, or dy = 0 and dx is 0 or -1; else south when dy = -2, or dy = -1 and dx is 0 or -1, unless the
tile one east of the (new) column is no-south (DOS tests x+1 here too). Then:

- pushed west onto a no-east tile: three steps west (the first may not push actors, the others may);
- else pushed north onto a no-south tile: three steps north, the same way;
- else one forced step to the new place; if it fails, that's all.

**The roll** repeats until the boulder's quality is 1 (DOS has no other limit; the engine stops after 64
rounds):

1. if any of its four tiles is a slope tile, it rolls downhill: east unless the tile east is no-east,
   south unless the tile south is no-south; if it can go neither way it stops; else one step;
2. else a forced step where it is (crushing what is under it); if that fails and it was rolling or
   pushed three steps, it tries each tile around (column by column, west first; not back the way it
   came; no-south and no-east tiles skipped for the south and east tiles); with none taken it goes on
   one tile (downhill diagonally, or on north or west) and rolls again.

**A step** to (x, y) (0x194b1). Unless forced, it fails when the boulder's quality is 1 or the place is
barred: a wall tile in its 2x2 (map tiles 0x90-0x92, 0x95, 0x98-0xa9, 0xac-0xb6, 0xba-0xc5, or a
tree), or a tree (260-266) or waterfall (150, 151) object at (x-1, y-1), (x-1, y), (x, y) or (x, y+1)
(DOS's own cells). Then what is under the 2x2 (0x19201): another boulder is blasted on from that cell
(if it can't go, the step fails unless forced); an actor, or a big creature's other tile (284 285 289
290 293-295 297 298 303-306, whose quality is its actor), is crushed on a step that may push actors,
else moved to the first free tile around (column by column) and hurt 1-30, crushed if none is free;
other things are destroyed, except 249, 280 and those whose tileflags3 has bit 0x20 (DOS reads that
byte by the thing's index in its object table, not its tile: a slip; the engine reads the tile). Then
the boulder moves, and:

- **the waterfall:** a waterfall (150) at (x, y+2) means it has fallen onto the ledge above it. Its
  quality becomes 1 for good; the waterfall at (x, y+2), the falls (151) at (x+2, y+3), the earth (195)
  at (x+1, y+3) and (x, y+3) and the water (152) at (x+2, y+4) are removed, and so is the egg
  (230) under the boulder's first tile, so no other boulder hatches there. On the new-game surface this is the falls at (292,66),
  in front of the cave at (293,67): the boulder dams it from (292,64);
- **water:** a map tile 0x0c or 0x0f in (x, y-1), (x, y), (x+1, y-1), (x+1, y) also makes its quality 1.

DOS plays sound 0x1f at each of these. The waterfall's boulders hatch from eggs (230): the new game's
eggs at (295,64) and (299,65) each hold one (no boulder lies on the map itself). SE's eggs are not yet
built, so in the engine the boulder appears only once they are.

**M)ove** (0x1f530, SE's own M)ove rules, not yet built): moving an activated device anywhere but into a
container sets it off at the place it lands. A thing thrown more than a tile that is on the breakable list
(DS:0x1170: 3 6 25 59 60 95 97 132 133 181 184 137 63) breaks: `It broke!`, or for the soft clay pot `The
soft clay pot went squish.` and clay (192) is left. A lit grenade is not on the list: thrown, it lands
and its fuse burns on.

The engine doesn't save the fuse; whether DOS's save keeps DS:0x2842 is an open question (§6).

## 6. Open questions
- Whether actors are in the map window's object index (0xa72c) that the blocked test walks. Taken as
  objects only.
- Whether the save keeps the fuse (DS:0x2842/0x2844).
- Item 5 covers the remaining cases, one handler at a time.
