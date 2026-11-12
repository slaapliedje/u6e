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
> - Validated by : doors — `make bot-smoke` (SE leaves the waking hut); `make check-se-use` (SEUSE.BIN) for the doors, ground, fire, food, crate, grenades, gunpowder, the boulder, the giant gem, the ribbon, the knives, the hammers, the vine, the notebook, the flintlock, the camera, the mortar, the stalks, the bones, the plachta, the skin, the extinguisher, the liquor, the digging stick, the strips, the bucket, the machine, the loom, the wire screen, the crate's opening, the brazier, the burnt logs, the metal head, the tarred strips, the clay, the gunpowder, the torches, the fishing pole, the crystal garden, the tar pit, the statue and the crystals; not yet against DOS running
> - Confidence   : confirmed for the dispatcher, the usable list and the doors; the rest is a map
> - Last updated : 2026-11-11 (the giant gem, the magnesium ribbon, the knives, the hammers, the vine, the notebook, the flintlock, the camera, the mortar, the stalks, the bones, the plachta, the skin, the extinguisher, the liquor, the digging stick, the strips, the bucket, the machine, the loom, the wire screen, the crate, the brazier, the logs, the head, the tarred strips, the clay, the gunpowder, the torches, the fishing pole, the crystal garden, the tar pit, the statue, the crystals)

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
| 188 crate | shut (frame 1): it opens (frame 0); else `Not possible.`; then the search (0x1ec8a): an open crate within a tile of the leader spills what it holds, `Searching here, you find A, B and C.` (or `nothing.`); any other frame, nothing more | in the switch, 0x1ec8a | yes |
| 6 giant gem | set on the gem stand: noon and the trap door, §5d | 0x24302 | yes |
| 10 magnesium ribbon | burn one: a flash; wakes the giant stone head, §5e | 0x24f6b | yes |
| 20 rope | in solo mode `Not in solo mode!`; else climb (0x8cb:0x855, SE's level changes: item 11) | in the switch | item 11 |
| 22 hole in the ceiling | frame 1 and not solo: climb (as the rope); frame 0: `You need a rope dropped from above.`; else `Not in solo mode!` | 0x8cb:0x855 | item 11 |
| 25 44 52 obsidian knife, knives, scissors | `On `: cloth into strips; meat, a skin or feathers from a body, §5f | 0x22488 | yes |
| 29 119 rock hammer, metal hammer | `On `: a blue stone off the blue boulder, charcoal from burnt logs, §5g | 0x21436 | yes |
| 42 vine | `On ` a hole: it becomes a rope down, §5h | 0x24fde | yes |
| 43 bell | `On ` a fallen tyrannosaurus: the bell on its neck, §5i | 0x21293 | item 6 |
| 47 turtle bait | a turtle (302) comes up beside the holder, near water, in a part of the sea, §5w | 0x24783 | yes |
| 49 notebook | Jimmy's shorthand, §5j | 0x24d88 | yes |
| 50 bamboo flintlock | load with gunpowder, §5k | 0x21e67 | yes, but readied (item 6) |
| 51 camera | a picture (its quality is the film left), and the ribbon's flash, §5e | 0x24edf | yes |
| 59 60 mortar, grinding stone | grind what is in it: corn flour, or gunpowder, §5l | 0x210ad | yes |
| 61 62 corn and bean stalks | a corn or a bean to the acting member, §5m | 0x23ea5, 0x23e52 | yes |
| 63 jug of plachta | `On ` a spear: the spear of Shamap, §5m | 0x254bc | yes |
| 79-82 bones, skeleton | a rib bone to the acting member, §5m | 0x23f4b | yes |
| 86-88 the three totems | Triolo's invocations, §5n | in the switch, then 0x11ff:0xf07 | item 6 |
| 92 folded skin | unfolds into the skin (245) where there is room, §5o | 0x25c2e | yes |
| 94 fire extinguisher | `On `: a fire out, burning logs out, lava hardened, §5o | 0x243e9 | yes |
| 95 bottle of liquor | drunk; past three drinks the Avatar staggers, §5o | 0x21c2d | yes |
| 131 digging stick | clay from a clay tile beside its holder, §5p | 0x21f60 | yes |
| 134 cloth strips | `On ` a person: bind their wounds, §5p | 0x23c06 | yes |
| 136 metal bucket | tar or water into it; tar poured sets fires, water puts them out, §5q | 0x235a9 | yes, but the fire's harm (item 6) |
| 137 240 grenades, devices | light, §5b | 0x21671 | yes, but trees |
| 143 machine | a gem in, a fruit out, §5r | 0x25b44 | yes |
| 148 jungle drum | with the drumsticks and the tribes united, the scene underground, §5r | 0x2542b | item 11 |
| 165 loom | `On ` flax: cloth, §5r | 0x23f9e | yes |
| 173 242 254 257 oven, fire, fire pit | the fire's `On ` (§5) | 0x2215f | yes |
| 177 wire screen | sulfur from a sulphur pit, §5r | 0x23b10 | yes |
| 186 brazier | a frame with bit 0x02 (unlit): `Not possible.`; else a fire: its `On ` (§5) | in the switch | yes |
| 190 metal head | on the headless statue: the statue (actor 11) joins, §5s | 0x24147 | yes |
| 191 tarred cloth strips | `On `: a torch from a branch, a grenade from a fired pot, §5s | 0x23777 | yes |
| 192 clay | a soft clay pot, §5t | 0x2204a | yes |
| 204 gunpowder | `On ` a flintlock: loaded, §5t | 0x24c0a | yes |
| 207 burnt logs | frame 0: `Not possible.`; frame 1: frame 2, `You started a fire.`; frame 2 (burning): a fire's `On ` (§5); others: nothing | in the switch | yes |
| 208 209 torches, lit torch | a torch is lit into a free hand; a lit torch's `On ` burns and incinerates, §5t | 0x20ecc, 0x25686 | yes |
| 212 fishing pole | a fish, 1 time in 5, beside deep water, §5u | 0x21b48 | yes |
| 216 crystal garden | Rafkin or Johann finds potassium nitrate, §5u | 0x25d54 | yes |
| 229 tar pit | `On ` cloth strips: all tarred, §5v | 0x23211 | yes |
| 233 237 raft | board with four paddlers; aboard, land next to the shore, §5w | 0x21c98 | yes |
| 247 headless metal statue | with the metal head carried, the statue (actor 11) joins, §5s | 0x2423a | yes |
| 252 crystals | `On ` a person: healed to the full, the poison gone, §5v | 0x25972 | yes |
| 302 turtle | mount it, §5w | 0x24b00 | yes |
| 307 a rider on a turtle | dismount next to the shore, §5w | 0x249a9 | yes |

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
- a tree (263): hit for 30, SE's object damage (0x1abcd, se_combat.md §3f): a tree's count is 5, so it falls;
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

**D)rop** (0x1f530, rules/se_move.md §6): dropping an activated device anywhere but into a container sets
it off at the place it lands. A thing thrown more than a tile that is on the breakable list
(DS:0x1170: 3 6 25 59 60 95 97 132 133 181 184 137 63) breaks: `It broke!`, or for the soft clay pot `The
soft clay pot went squish.` and clay (192) is left. A lit grenade is not on the list: thrown, it lands
and its fuse burns on.

The engine doesn't save the fuse; whether DOS's save keeps DS:0x2842 is an open question (§6).

### 5d. The giant gem (0x24302)

U)se of the giant gem (6) looks for a gem stand (66) in the 3x3 around whoever holds the gem: the
member carrying it, or the acting member for one on the ground (not around the gem itself). It looks
column by column, west first, and stops after the column it found one in. For each stand found at (x, y):

1. the gem moves to (x-1, y-1) on the current level, out of the pack if it was carried;
2. `You place the gem on the stand.`;
3. the first time (bit 0x02 of talk flag 1 clear), the scene (0x1dd70).

None found: `No effect.` The new game has one stand, at (170,483); the gem goes to (169,482).

M)ove of the gem onto a stand does the same (0x2071f, rules/se_move.md §3), without the search. The gem is also on the breakable list (§5c, M)ove): thrown
more than a tile, it breaks.

**The scene**, in order:
- song 15 plays (0x2701:0x190); afterwards the song for the place comes back (0x2701:0x1f1);
- the message area is cleared, so the line above is gone at once;
- the eyes in the top border turn blue (map tiles 373 and 374 drawn at (64,0) and (80,0); 371/372 are
  the green pair and 375/376 the red);
- the clock runs on: if the minute isn't 0, to the next hour, then an hour at a time until 12:00. Each
  step is one pass of the world for that many minutes (0x359:0x11b6): a lit fuse runs down by that much
  and blows if it runs out, and the schedules catch up. On the hour exactly, no time passes at all, even
  far from noon (a DOS slip; the engine runs on to noon from the hour too, se_deviations.md 1);
- a trap door (160) is made at (192,460), level 0, frame 1 (the open one);
- talk flag 1 gets bit 0x02, so the scene never comes again;
- the "light" object (11) takes map tile 284's picture (a beam) in place of its own (522), until the game
  is reloaded. No light objects are in the new game's object data, surface or dungeon, so nothing shows it.

The engine has no SE music yet (item 9) and draws U6's border (item 8), so the song and the eyes wait for
those items. Where the trap door leads is item 11.

### 5e. The magnesium ribbon (0x24f6b) and the camera (0x24edf)

U)se burns one ribbon (10): one of a stack, or the last one. The flash is at the tile of whoever holds
it (the acting member for one on the ground), and it goes in three frames: the burst (map tile 264)
over that tile; the whole view at light level 7 (lit everywhere, as at noon); then the light as it
was. No time passes beyond the U)se.

Then, unless bit 0x02 of talk flag 3 or bit 0x80 of talk flag 53 is set, and only while the giant stone
head (actor 25, on level 2 at (48,206) in the new game) is on screen (within 5 tiles across and 4 up or
down of the leader, any level: DOS doesn't test it):
- the flash within 2 tiles west or north, or 1 east or south, of the head's anchor (it fills 2x2,
  anchored bottom right): talk flag 25 becomes 0x28 (the whole byte), flag 53 gets 0x80, and the head
  talks (no name line first);
- anywhere else: `You hear a barely audible voice whisper, "Closer...".`

The camera (51) makes the same flash (0x24dd4, the same head test), at its holder's tile, or at its own
tile when it lies on the map. Its quality is the film left: above 0 it goes down by one and `You take a
picture.`; at 0, `The camera's out of film, but the flash still works.` Nothing is used up.

### 5f. Knives and scissors (0x22488)

The obsidian knife (25), the knives (44) and the scissors (52) ask `On ` at once, as the fire does (§5).
"The holder" is whoever carries the knife (the acting member for one on the ground). No answer:
`nothing!`. A thing on the map must be within one tile of the holder, else `Out of range!` (DOS measures
from the holder, not the leader as the fire does). "Its place" is the thing's tile, or its carrier's.
New things are made on the current level, at frame 0 unless given.

| on | what happens |
|----|--------------|
| cloth (180) | one cloth goes (one of a stack); 10 cloth strips (134) go to the acting member if the cloth was in their pack, else onto the holder's tile: `You made some cloth strips.` |
| a dead beast (280), by its frame | see below |
| a body (249) frame 15 | blood (248, frame 0-2 at random) on its place, the body goes, `You remove the feathers.`, and 5-20 feathers (48) to the holder, else on its place |
| a body (249) frame 11 | `You make a mess.` and blood (frame 0) on its place; the body stays |
| a body (249), other frames | `You ghoul!` |
| anything else | `No effect.` |

A dead beast's frame (the jump table at cs:0x4dfa; the names are LOOK's for each frame):

| frames | beast | what happens |
|--------|-------|--------------|
| 1 | serpent woman | `You ghoul!` |
| 21 39 41 45 | gorilla, sloth, jaguar, bear | a skeleton (82, frame 1) and blood on its place, blood one tile west; the body goes; `You cut up some meat.`; 2-7 meat (110) to the holder, else on its place |
| 47 | eohippus | bones (80) and blood on its place; the body goes; the meat as above |
| 37 | tiger | as the gorilla, but `You skinned the big cat.`, and after the meat a folded skin (92) to the holder, else on its place |
| 46 | spider | `You make a mess.` and blood (frame 0); the body stays |
| 3 4 6 8 9 11 15 17 27 28 30 31 33 35 43 | the dinosaurs | `The hide is too tough.` |
| 23 24 25 | robosaurus, argent sergeant | `Clank!` |
| 0, and the others | the rest of a big beast's tiles | nothing at all, not even a message |

Every new thing above comes from DOS's object maker (0xa5d:0xeb6), which also sets status bit 0x20;
the gem's trap door is the only one DOS clears it on. What 0x20 does in SE (probably "temporary", as
in U6's save) is an open question (§6); the engine keeps them like any other object.

### 5g. The hammers (0x21436)

The rock hammer (29) and the metal hammer (119) ask `On ` as the knives do, with the same holder, the
same `nothing!`, and the same range test from the holder. The holder's strength here is the one DOS
works with (0x1810:0x4a0): their strength, less 3 when their object flags (objlist 0x000) have bit 0x40,
and with bit 0x80 as well the 3 back, kept between 1 and 30.

| on | what happens |
|----|--------------|
| the blue boulder (172) | a roll of 1-30 below (45 - strength) / 2: `Glanced off!`; else `A piece of rock flakes off.` and a blue stone (58) to the holder, else onto the holder's tile. The boulder stays |
| burnt logs (207), frame 0 | 1 to strength charcoal (129). If the holder carries a charcoal stack that can take it (no more than 255), it joins that stack and DOS says nothing (the engine says `You made some charcoal.` here too, se_deviations.md 2); else `You made some charcoal.` and a new stack to the holder, else onto the holder's tile. The logs stay |
| anything else | `No effect.` |

### 5h. The vine (0x24fde)

The vine (42) asks `On ` as the knives do (holder, `nothing!`, range from the holder). On a hole (168):
the vine goes, and a rope (20) is made at (x-1, y-1) of the hole, on the current level, with the hole's
quality (the hole's quality is its link to the level below, which the rope's climb reads: item 11); DOS
clears the new rope's status bit 0x20. On anything else: `You can't use a rope here!`. The new game's
holes are 2x2 (frame 3), e.g. at (396,68) with quality 1.

### 5i. The bell (0x21293), with item 6

The bell (43) asks `On ` as the knives do (holder, `nothing!`, range from the holder). It works only on
an actor whose object is a dead beast (280) at frame 13 or 17: SE leaves a fallen creature in its actor
slot with a body's object number (249 or 280) and status bit 0x10 (such an actor takes no turns,
0x18767). Frames 13 and 17 are tyrannosaurus frames. Then:
- the actor's status (objlist 0x800) without bit 0x04: `No effect.`;
- its talk flag already nonzero: `It already has a bell on its neck.`;
- else the bell goes, its talk flag becomes 0xff, `You put the bell on the dinosaur's neck.`, and for
  actor 181 (the new game's tyrannosaurus at (421,584)) talk flag 20 gets bit 0x80.
Anything else: `No effect.` (DOS also notes a frame-17 target's quality, then never uses it.)

Waits for item 6: the engine doesn't yet turn SE's fallen creatures into bodies in their actor slots,
and its `On ` takes things, not actors.

### 5j. Jimmy's notebook (0x24d88)

The acting member reads it. If that member's object is 312 (Jimmy): `Jimmy says, "Hey, boss, you'll
never be able to figure out my shorthand."`, a blank line, `"Why don't you just ask me what you want to
know?"`. Anyone else: `You can't read Jimmy's shorthand.`

### 5k. The bamboo flintlock (0x21e67)

The acting member loads it (whoever holds it). Frame 1 is loaded:
- frame 1 already: `Flintlock already loaded.`;
- no gunpowder (204) in the member's pack: `You don't have any gunpowder.` for the leader, else
  `<name> doesn't have any gunpowder.`;
- else one gunpowder goes, the flintlock turns to frame 1, and `Flintlock loaded.` for the leader, else
  `<name>: Flintlock loaded.`

The engine keeps a readied thing's object number but not its frame, so a readied flintlock is loaded
with item 6, where firing it unloads it.

### 5l. The mortar and the grinding stone (0x210ad)

Both are containers; U)se grinds what is in them. The things inside are looked at in order:
- empty: `There's nothing to grind!`;
- anything but corn (108), charcoal (129), sulfur (123) or potassium nitrate (130), met before any corn:
  `You can't grind that!`, and nothing changes;
- corn stops the look. Until then, the smallest stack among the other three is noted (each of the
  three is noted once, the last of its kind found).

Then:
- **corn**, with none of the other three seen before it: `You grind the corn into corn flour.` The corn
  goes and as much corn meal (93; 1 for a quantity of 0) goes into the container. With any of the three
  seen first: `The mixture in the <container> is wrong.`;
- **no corn**: charcoal, sulfur and potassium nitrate in that order: the smallest stack's worth goes from
  each (a stack no bigger goes whole); if one is missing, `You're missing some ingredients.` and it
  stops there, but in DOS what was taken from those before it stays gone (the engine checks all three first and
  takes nothing, se_deviations.md 3). With all three:
  that much gunpowder (204) goes into the container and `You mix a batch of gunpowder.`

SE's containers and how things get into them are rules/se_move.md.

### 5m. The stalks, the bones and the jug of plachta

The corn stalk (61) and the bean stalk (62) give the acting member (the first of the party while aboard)
one corn (108) or one bean (109): `You got some corn from the plant.`, `You got some beans from the
plant.`; the bones and the skeleton (79-82) one rib bone (211): `You got some rib bones.` None of them is
used up. Too heavy (strength x 20): `You are carrying too much already.`

The jug of plachta (63) asks `On ` as the knives do (holder, `nothing!`, range from the holder). On a
spear (26): one spear goes; the spear of Shamap (1) goes to the acting member if the spear was in their
pack (and they can carry it), else onto their tile; then the jug goes. Nothing is said. On anything else:
`No effect.`

### 5n. The totems (in the switch), with item 6

The three totems (86-88) are Triolo's (actor 9). The acting member has the totem and is Triolo: his
invocation (0x11ff:0xf07). The acting member doesn't have it: the invocation if the leader is Triolo,
else `<member> can't use that!`. Someone else has it in their pack: `<member> can't use that!`. The
invocations are item 6.

### 5o. The folded skin, the fire extinguisher and the bottle of liquor

**The folded skin (92)** unfolds where it lies into the skin (245) at its frame + 3, a 2x2 thing anchored
at the folded skin's tile. Carried, or no room: `There is not enough room to unfold the skin.` No room
means a thing at (x-1, y) or (x-1, y-1) (DOS means to look at the whole 2x2 but looks at that column
twice: a slip; the engine looks at the three tiles, se_deviations.md 6), or an impassable tile (tile flags 1, bit 0x02) at (x-1, y), (x, y-1) or (x-1, y-1).

**The fire extinguisher (94)** asks `On ` as the knives do (holder, range from the holder):
- a fire (242, 254): `You put the fire out.` and it goes;
- burnt logs (207) burning (frame 2): `You put the fire out.` and they are frame 0;
- another thing: `No effect.`;
- a tile with nothing on it, in reach: lava (map tiles 15-27, 170, 171): on level 3 `The lava is too far
  below you.`; else `Lava`, `You harden some of the lava.`, and frozen lava (57) lies there, its frame the
  lava tile's place in that list (0-14). Not lava: `nothing!`
DOS plays sound 0x1d each time; SE's sounds are not yet built.

**The bottle of liquor (95)** goes: `You drink it.`, and the drinks count (DS:0x280e) goes up by one; it
goes down by one each hour. Past 3, each step of the Avatar's is, 1 time in 2, `Hic!` and a step one of
the eight ways at random (N, NE, E, SE, S, SW, W, NW, DS:0xac/0xb4) instead. (U6's is 3 in 4, any way or
none.) Where SE's save keeps the count is item 10.

### 5p. The digging stick and the cloth strips

**The digging stick (131)** looks at the eight tiles around its holder (the party's tile when aboard),
column by column from the west, top to bottom. The first clay tile (map tiles 0x20-0x2f, 0xbe): `You get
some clay.` and one clay (192) to the holder, else onto that tile. None: `There is nothing to dig!` The
stick stays.

**The cloth strips (134)** ask `On ` (holder, range from the holder; a person is taken here, not only
things):
- a person: their name, then, below their maximum HP (SE's: strength x 2 + level x 4, 1-255, strength as
  in §5g), 1-5 HP back (no more than the maximum), `<name>'s wounds are bound.` and one strip goes; at the
  maximum, `<name> doesn't need any help.`;
- a person who has fallen (their object a body, 249, or blood, 248), or a body or blood on the map: `a
  body.` and `That person is beyond help.`;
- anything else: `nothing`.
Where SE's save keeps the levels is item 10.

### 5q. The metal bucket (0x235a9) and fire

The bucket (136) holds what it holds inside it: 10 tar (139) or 10 water (140). Its frame shows it: 0
empty, 1 tar, 2 water.

**Empty**, U)se looks at the eight tiles around its holder (or its own tile on the map), column by
column from the west; on each tile, at each thing there:
- a tar pit (229): 10 tar go into it, frame 1, `You got some tar in the bucket.`;
- else water: a wet tile (tile flags 1, bit 0x01), or a fountain (162, 166), the well (200), the
  waterfall (150) or the falls (151): 10 water, frame 2, `You got some water in the bucket.` DOS tests
  the wet tile at (x, holder's y - i + j) for the column i and row j it is at, not (x, y): a slip; the engine tests (x, y), se_deviations.md 7.
  A tile with nothing on it is still tested for wet once.
The first one found ends it; none: `There is nothing to get in the bucket.`

**Tar** asks `On ` (holder, range from the holder for a thing on the map):
- cloth strips (134): `cloth strips`, `You tarred some cloth strips.` As many tar go as there are strips;
  the acting member gets as many tarred cloth strips (191) as there was tar for, if the strips were in
  their pack, else they lie where the strips were; the strips go (all, or as many as there was tar for).
  With no more tar than strips the bucket is emptied;
- a pteranodon nest (171), or a dinosaur egg (111) lying on one: `pteranodon nest`; a fire (242) and a
  burned nest (76, frame 3) on the nest's tile, and the fire burns (below). More than 4 tar: one goes;
  else the bucket is emptied;
- a dinosaur egg not on a nest: `dinosaur egg`, a fire on its tile, and it burns. DOS takes no tar (the engine takes one,
  se_deviations.md 8);
- a web (17): `web`, a lasting fire (242, quality 2) and a fire on its tile, and it burns. DOS takes no tar (the
  engine takes one, se_deviations.md 8);
- anything else, or a tile with nothing on it (in reach, else `Out of range!`; no tile, `nothing!`): its
  name; one tar goes (the last empties the bucket); unless the tile is wet, a lasting fire and a fire,
  and it burns.
Nothing of these done: `No effect.`

**A fire burns** (0x22a28): what is on its tile: a person takes the tile's harm (the hazard routine,
0x359:0xda8: item 6); a thing on the burnable list (DS:0x118a, the fire's incinerate list, §5) or an egg
(230) goes, the egg with what it holds; a web or a nest that goes lights a fire on every web around it
(the 8 tiles). Then the fire itself goes. The new fires burn next, wave after wave (0x22b5e and 0x22c39
take turns, marking each wave's fires with quality 0 or 1), until a wave lights none; the view is drawn
between waves. The lasting fire (quality 2) isn't in the waves and stays.

**Difference** (se_deviations.md 11): each DOS wave takes every fire of the wave's quality in the
loaded objects, so a campfire nearby with quality 0 would burn out with the first wave. The engine holds
the whole world rather than DOS's loaded area, so its waves take only the fires the pour started.

**Water** asks `On ` (holder; range for a thing on the map). All the water goes and the frame is 0, then:
a fire (242, 254): `You put the fire out.` and it goes; burnt logs burning (207, frame 2): `You put the
fire out.`, frame 0; anything else, or a tile (its name first; no tile, `nothing!`): `You pour the water
out.`

### 5r. The machine, the jungle drum, the loom and the wire screen

**The machine (143)** takes the first diamond, emerald or ruby (72, 73, 74) in the leader's pack (not in
a container), one of the stack: `You put a gem in the machine.`, and a fruit (197) lies on the machine's
tile. None: `You don't have any gems.` DOS plays sound 0x20.

**The jungle drum (148)** needs small bones (91, the drumsticks) somewhere in the leader's pack, else
`You have nothing with which to play the drum.` With them, if bit 0x08 of talk flag 1 is clear and bit
0x80 is set in talk flags 16, 17, 19, 20, 27, 29, 32, 34, 40, 48 and 53 (the tribes), the scene
(0x1e1ac): song 17, the message area cleared, the blue eyes, talk flag 1 gets 0x08, the party goes to
(243,204) on level 1, the hole in the ceiling (22) there turns frame + 1, and if the acting member
carries object 21 its picture is lent to tile 268; then the view and the place's song. Otherwise
nothing happens. The move between levels is item 11, so the drum comes with it.

**The loom (165)** asks `On ` for the leader (range from the leader): on flax (210), `You wove cloth from
the flax.`, one flax goes, and one cloth (180) goes to the leader if the flax was carried (else at their
feet), or lies where the flax was. Anything else: `No effect.`

**The wire screen (177)** looks at the eight tiles around its holder, column by column from the west;
the first sulphur pit (231): `You made some sulphur powder.` and 10 sulfur (123) to the holder, else at
the holder's feet. The pit stays. None: `There is nothing to sift.`

### 5s. The metal head and the tarred cloth strips

**The metal head (190)** looks at the eight tiles around its holder, column by column from the west.
The first headless metal statue (247): the head and the statue go, actor 11 (the golden statue) is put
on the holder's own tile (as DOS does) and joins the party (it doesn't count against the five, §4),
talk flag 11 gets 0x40, and `You replace the statue's missing head.` None: `No effect.`

**The headless metal statue (247)** U)sed does the same when the metal head is anywhere in the leader's
pack; actor 11 then stands where the statue stood. Without it: `No effect.` (0x2423a)

**The tarred cloth strips (191)** ask `On ` (holder, range from the holder):
- a tree branch (206): `You make a torch` (DOS has no full stop). One strip and the branch go; a torch
  (208) goes to the holder if the branch was theirs (else at their feet), or lies where the branch was;
- a fired clay pot (133): a new line, then the holder needs 5 gunpowder (204) in one stack in their pack,
  else `You need five pinches of gunpowder to make a grenade.` With it: 5 gunpowder, one strip and the
  pot go, `You made a grenade.`, and a grenade (137) goes to the holder as the torch does;
- anything else: `No effect.`

### 5t. Clay, gunpowder and torches

**Clay (192):** one goes, `You shaped a small, soft pot from the clay.`, and a soft clay pot (132) goes to
the holder if the clay was carried (else at their feet), or lies where the clay was.

**Gunpowder (204)** asks `On ` (holder, range): on a bamboo flintlock (50), frame 1 `Flintlock already
loaded.`, else one gunpowder goes, the flintlock is frame 1, `Flintlock loaded.` (no names here, unlike
§5k). Anything else: `No effect.`

**A torch (208)** is lit by the acting member: with both arms holding something, `Your hands are full.`;
else one torch goes and a lit torch (209) is readied in the first free arm with 100 fuel (its quality).
(When the arms are full DOS also writes 100 into the quality of whatever object its spare register
names: a slip without an effect the engine can copy.) Each turn, 1 time in 2, a readied lit torch burns
one fuel (DOS takes as many as the turns that pass at once); at 1 or less it goes out: `A torch burned
out.` and it is gone. A lit torch in the party lights the dark (lighting.md).

**A lit torch (209)** asks `On ` (holder, range; no answer `nothing!`):
- a pteranodon nest, a dinosaur egg or a web: as the tar does (§5q), without the lasting fire;
- a thing on the burnable list (§5): `You incinerated the <name>.` and it goes;
- gunpowder: whoever carries it takes 1-30 harm, it blows as a grenade (§5b) where it lies or where its
  carrier stands, and it goes;
- anything else: `No effect.` The torch stays.

### 5u. The fishing pole and the crystal garden

**The fishing pole (212)** looks for deep water (map tile 0x0c) around its holder, but DOS steps both
axes with one counter, so it only looks at (x-1, y-1), the holder's own tile and (x+1, y+1) (a slip; the engine looks at all eight,
se_deviations.md 4). The first deep water: 1 time in 5, a fish (213) to the holder, `You caught a fish!` (too heavy, or
the other 4 times: `You didn't get a fish.`). None: `You need to stand next to deep water.`

**The crystal garden (216)** needs Rafkin (actor 7), or else Johann (5), in the party, not dead (status
0x10), not asleep (0x04) and on screen. Then 10 potassium nitrate (130) to the leader: `Rafkin identifies
some potassium nitrate, and you scrape a quantity from the crystal.` (or Johann); too heavy: `You are
carrying too much already.` Neither: `There is nobody in the party who can identify which crystals are
useful.` The garden stays.

### 5v. The tar pit and the crystals

**The tar pit (229)** asks `On ` for the member shown in the panel, else the leader (range from them): on
cloth strips (134), `cloth strips`, and the whole stack becomes as many tarred cloth strips (191), to that
member if the strips were theirs, else where the strips were. Anything else: `No effect.`

**The crystals (252)** ask `On ` (holder, range; a person is taken here): a person below their maximum
HP (§5p) is healed to it and their poison (status bit 0x08) goes: their name, then `<name> is healed.`,
and one crystal goes. At the maximum: `<name> Doesn't need any help.` (DOS's capital D). A fallen person,
a body or blood: `a body.` and `That person is beyond help.` Anything else: `nothing`.

### 5w. The vehicles: the raft, the turtles and their riders (307)

SE's vehicles are actors, as U6's are. **The raft** is a 2x2 thing (its tile is the bottom-right one;
233 lies on the map, 237 carries the party, both at frame 3). **A rider on a turtle** is the member
itself drawn as obj 307, the turtle beneath it; each member mounts a turtle of its own.

**The shore** (0x1dbd:0xa29): map tiles 0x20-0x2f and 0xbe.

**The raft (233, 237; 0x21c98).** The thing chosen stands for its actor (0x1810:0x3329: a map
object of an actor kind with a quality names that actor). In order:
1. Solo mode: `Not in solo mode!`.
2. Fewer than 4 in the party: `You don't have enough people to paddle.`
3. The members carrying a paddle (12) are counted, up to 4: a paddle anywhere in the pack, not a
   readied one (0xa5d:0x80a).
4. Aboard, and the thing is the raft carrying the party: one of the raft's four tiles is shore
   (0x130f:0x14e): the party lands (below); else `Only next to land.`
5. Fewer than 4 paddlers: `You would need at least four people carrying paddles to operate the raft
   effectively.`
6. The raft more than a tile from the party: `Out of range!`.
7. The party boards (0x130f:0x21cb): actor 0 becomes the raft (237, frame 3) where the raft lay, with
   the raft's quantity, in the party; the members go inside it; the raft object goes; the party is
   where the raft is. No words.

**Landing** (0x130f:0x2343): a raft (233, frame 3) is made on the party's tile with actor 0's
quantity; actor 0 goes; the party stands on that tile again (0x8cb:0x754). No words.

**The raft moves** (0x130f:0x265, the movement routine, with actor 0 as the raft):
- only N, E, S or W; a diagonal fails;
- the two tiles of its leading edge must be water (tile flags 1 bit 0x01): moving E they are (x+1, y)
  and (x+1, y-1), W (x-2, y) and (x-2, y-1), N (x, y-2) and (x-1, y-2), S (x, y+1) and (x-1, y+1);
- for each of its four tiles, if that tile is shore now and the tile it moves onto is shore too, it
  fails (0x132b6): the raft can't slide along the shore;
- on the leading edge, an object whose tile is neither water (flags 1 bit 0x01) nor ignored
  (tileflags3 bit 0x10) blocks it, and so does any actor but actor 0.

**The turtle (302; 0x24b00)**, an actor on the tile. When the leader isn't a rider, the leader
mounts it. When the leader rides already, the first member (in party order) within a tile of the turtle
who isn't a rider mounts it; nobody: `You're already on a turtle!`. Mounting: the turtle goes, the
member moves onto its tile and becomes 307 with the turtle's frame (0x24965), and the party's tile
becomes that tile. No words.

**A rider (307; 0x249a9)** dismounts onto the first shore tile around it (x-1 to x+1 by column, then y,
its own tile skipped; nothing else is tested): it moves there and is itself again (its own obj_n and a
frame for its facing), the party's tile becomes that tile, and a turtle (302) is made where it was, a
friend (status 0x40) that wanders (worktype 0x8f). No shore around: `Only next to land!` (with `!`,
the raft's has `.`).

**Swimmers** (the turtle, a rider; creature type flag 0x200, DS 0x2f43:0x1b4): they move only onto deep
water (tile flags 1 bits 0x01 and 0x02); an object there blocks them unless its tile is 12 or ignored
(tileflags3 bit 0x10), and so does an actor. A party with riders and walkers moves apart on the water.

**The turtle bait (47; 0x24783).** A turtle is made (SE's creature maker 0x2667:0x15d: strength 15,
dexterity 5, intelligence 4, HP 25, each rolled as v - v/2 + two rolls of 0 to v/2), a friend that
stands (worktype 0). The eight tiles around the bait's holder (aboard: the raft), by column: the first
where the turtle can move (as a swimmer) takes it. A water tile (flags 1 bit 0x01) among those looked at
before that counts as "near water".
- None takes it: the turtle goes; not near water: `You must be near water.` and the bait stays; else one
  bait goes and `Nothing takes the bait.`
- Taken, but the holder is outside x 0x335-0x3ed or y 0xe7-0x1e7: the turtle goes, `Nothing takes the
  bait.` Either way one bait goes and the view is drawn again. A taken turtle in that part of the sea
  stays, without words.

## 6. Open questions
- Whether actors are in the map window's object index (0xa72c) that the blocked test walks. Taken as
  objects only.
- Whether the save keeps the fuse (DS:0x2842/0x2844).
- The gem scene's 0x5d0:0x25ad (a buffer at DS:0xb65f) and the palette call 0x276b:0x245a (colour 2 to
  black?) are not read; DOS running would show them.
- What status bit 0x20 means for the things SE's object maker makes (§5f).
- What SE's save keeps for actor 0 aboard and for a rider (item 10); the engine's load forgets who rides.
- How the party gathers after landing (0x8cb:0x754) and what a vehicle step costs: not read; the engine
  uses its own gathering and the walking cost.
