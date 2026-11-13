# Rules: Savage Empire combat, readying and creatures

Games: Savage Empire only (U6's are combat.md and equipment.md)
Implemented by: /engine combat.c, equip.c, monsters.c (their SE tables), /render gamedemo.c (the SE game layer)
Tracked in: /docs/INDEX.md · Checklist: design/se_md_checklist.md items 4 and 6

> Provenance
> - Derived from : READING SE's GAME.EXE: R)eady 0xf404 with its slot routine 0xe5f3; the attack
>                  0x1b47d, its hit test 0x1af13, its damage 0x1b234, weapon damage 0x1a03d, armour
>                  0x18f74/0x18f39, weapon range 0x18fc7; the creature table (segment 0x2f43). The
>                  disassembly stays outside the repo.
> - References   : none
> - Tools        : objdump -m i8086; LOOK.LZC for the names
> - Validated by : `make combattest` (the tables and the rolls), `make check-se-use` (readying)
> - Confidence   : probable (read, not yet seen running)
> - Last updated : 2026-11-12

## 1. Readying (0xf404)

`Ready-` and the thing's name (a torch at frame 0, lit or not, is called `torch`), then:
1. Its class (0xe5f3), by obj_n:
   | class | slot | objects |
   |---|---|---|
   | neck | NECK | 84 tooth necklace, 85 jade necklace, 135 lei |
   | body | BODY | 7 cloth armor, 8 leather armour, 9 bark armor |
   | two hands | ARM + ARM2 | 31 bow, 32 blowgun, 34 two handed hammer, 40 modern rifle, 50 bamboo flintlock, 94 fire extinguisher, 118 fire axe, 128 bamboo pole, 131 digging stick, 175 stick, 212 fishing pole |
   | off hand | ARM2, else ARM | 2 shield of Krukk, 13 bark shield, 14 leather shield, 15 stegosaurus shield, 67 kotl shield |
   | a hand | ARM, else ARM2 | 1 spear of shamap, 21 black staff, 24 club, 25 obsidian knife, 26 spear, 27 throwing axe, 28 axe, 29 rock hammer, 33 obsidian sword, 37 atl atl, 39 boomerang, 44 knife, 52 scissors, 119 metal hammer, 137 grenade, 138 lit grenade, 177 wire screen, 206 tree branch, 208 torch, 209 lit torch, 240 device, 241 activated device |

   Anything else: `Can't be readied!`. SE has no helms, rings or boots.
2. **Modern weapons:** the modern rifle (40), the bamboo flintlock (50) and the grenade (137) can be
   readied only by the Avatar (1), Jimmy (4), Johann (5) and Rafkin (7); anyone else: `<name> can't
   use modern weapons!` DOS compares the whole object word, frame too, so it refuses only frame 0: a
   loaded flintlock (frame 1) passes (a slip; the engine refuses it at any frame, se_deviations.md).
3. The placement is U6's (equipment.md §2 and §9): a hand thing goes to the main hand, else the off
   hand; an off-hand thing the other way; a two-hand thing needs both hands free; neck and body their
   own. A taken place: `No place to put!`.
4. One of a stack is readied; the rest stays in the pack. There is no weight test.

## 2. Weapons and armour

**Damage** (0x1a03d): a creature fighting bare-handed does its table's damage (§4); a thing does this
table's (DS:0x105e/0x1094), anything else 1:

| weapon | damage | range |  | weapon | damage | range |
|---|---|---|---|---|---|---|
| 1 spear of shamap | 10 | 5 | | 37 atl atl | 14 | 7 |
| 21 black staff | 5 (40 charged) | 1 (7 charged) | | 39 boomerang | 4 | 4 |
| 24 club | 7 | 1 | | 40 modern rifle | 35 | 8 |
| 25 obsidian knife | 5 | 1 | | 44 knife | 7 | 4 |
| 26 spear | 10 | 6 | | 50 bamboo flintlock | 25 | 7 |
| 27 throwing axe | 6 | 4 | | 94 fire extinguisher | 6 | 1 |
| 28 axe | 8 | 1 | | 118 fire axe | 15 | 1 |
| 29 rock hammer | 6 | 1 | | 119 metal hammer | 6 | 1 |
| 31 bow | 8 | 6 | | 128 bamboo pole | 3 | 1 |
| 32 blowgun | 2 | 4 | | 137, 138 grenade, lit grenade | 15 | 6 |
| 33 obsidian sword | 12 | 1 | | 175 stick | 3 | 1 |
| 34 two handed hammer | 11 | 1 | | 209 lit torch | 5 | 1 |
| | | | | 240, 241 device, activated | 3 | 6 |

The black staff is charged when its quality is 1 (the attack sets it while the target is a Myrmidex
drone 323 or the queen 328, and clears it otherwise).

**Armour** (0x18f39): 7 cloth armor 1, 8 leather armour 3, 9 bark armor 2, 2 shield of Krukk 4, 13 bark
shield 1, 14 leather shield 2, 15 stegosaurus shield 3, 67 kotl shield 7. An actor's armour class is the
sum over what it has readied, 3 less with object status bit 0x40, 3 more when protected (actor status bit 0x01).
Creatures wear nothing else: the table's armour column (§4) counts only in the experience award.

**Swung by strength:** 24 club, 29 rock hammer, 33 obsidian sword, 34 two handed hammer, 94 fire
extinguisher, 118 fire axe, 119 metal hammer; bare hands by strength when the creature's flag 0x20 is
set. Everything else hits on dexterity.

## 3. A blow (0x1b47d, 0x1af13, 0x1b234)

### 3a. Before the blow

**The player's A)ttack** (0x1b9e7) strikes once with each readied weapon in turn, or once bare-handed.
Before each, with nothing printed yet:
- the bow (31) without an arrow (45) in the attacker's pack: `Out of arrows!`; the modern rifle (40)
  without a rifle bullet (41): `Out of ammunition!`; the blowgun (32) without a poisoned dart (36):
  `Out of darts!`; the atl atl (37) without a spear (26): `Out of spears!`. A readied one doesn't count
  (0xa5d:0x80a passes over status 0x18).
- the bamboo flintlock (50) unloaded (frame 0): `Flintlock not loaded!`.

Each of these ends that weapon's blow; no time passes for it. Then the target is named as for U6, and the
blow (0x1b47d) is struck.

**The blow (0x1b47d; weapon, target tile, attacker):**
1. A rider on a turtle (307) fights bare-handed, whatever it has readied.
2. The black staff (21) is charged (quality 1) when the target is a Myrmidex drone (323) or the queen
   (328), and uncharged otherwise.
3. Past the weapon's range (§2; the charged staff 7): `Out of range!`.
4. **What can be struck:** a web (17) only with the lit torch (209) or a grenade (137, 138); a tree
   (263) only with a grenade or the fire axe (118). Otherwise `Not possible.`
5. A fighter other than the player whose weapon lacks its ammunition (as above) does nothing, silently. An
   unloaded flintlock is loaded instead (0x21e67, se_use.md §5k: one gunpowder from the fighter's pack,
   `<Name>: Flintlock loaded.` or `<Name> doesn't have any gunpowder.`).
6. **Shot, not swung:** the blow flies (3b) when the target is more than one tile away, or always with the
   bow, the bamboo flintlock, the modern rifle, the atl atl, the blowgun, the grenades, the boomerang, the
   devices (240, 241) and the charged staff.
7. **The hit** (0x1af13): a thing that isn't an actor is always hit. The spear of shamap on the
   tyrannosaurus (294) and a lit torch on a web (17) always hit; the charged staff on a drone or the queen
   hits 7 times in 8. Otherwise the roll 1-30 hits when it is at least (the defender's dexterity + 30 - the
   attacker's dexterity or strength) / 2 (a drone or the queen defends at 20). U6 needs the roll to be over
   that. **DOS slip:** 0x1b47d tests the result only against -1, which by then it never is, so in DOS a
   failed roll strikes anyway (se_deviations.md 13).
8. Nobody on the tile where it ends, or the attacker itself: nothing more.

### 3b. The flight (0x19b5a)

What flies: the bow an arrow (tile 566), the atl atl a spear (547), the blowgun a dart (557), the rifle
and the flintlock a bullet (tile 263), a grenade the lit grenade (701), a device the activated device
(1029), the charged staff a bolt (tile 266); a thrown weapon (1, 26, 27, 39, 44) flies as itself.
It goes as far as the weapon's range toward the target, and stops early on what blocks it. Whoever stands
where it ends is the one struck: the same roll, or a new one if it stopped short of the target.
Sounds: 2 for the spears, axes, knives, bow and devices; 5 the blowgun; 3 the boomerang; 4 the rifle and
the flintlock; 0x1e the grenades; 0x12 and then 0x1e the charged staff.

What it uses (the attacker's pack):
- **The bow, the rifle, the blowgun, the atl atl:** one arrow, bullet, dart or spear. A spear from the atl
  atl that was the last of its stack lands where the flight ended, unless the tile loses it (deep water,
  0x1e61c, 0xf71, and nothing there to land on: 0x1f4cd); the others are spent.
- **The bamboo flintlock** unloads (frame 0).
- **A thrown weapon** (1, 26, 27, 44) lands where the flight ended (unless that tile loses it and nobody
  stood there), and the next of its kind in the pack is readied in its place.
- **A grenade or a device** is gone; it bursts where the flight ended (a grenade 0x19a41, a device
  0x19ac1: the blasts of se_use.md, a grenade 1-20 to each actor in it that isn't of a stackable kind, a
  device 1-35 to drones and the queen only; then the grenades in the 3x3 go off, its webs go, its boulders
  roll and its trees take 30). A grenade (137) or device (240) in the pack is readied in its place.
- **The boomerang** flies back to the attacker afterwards (0x276b:0x390), as a picture only.

### 3c. The blow lands

- **The spear of shamap** (1) becomes a plain spear (26), wherever it is. The tyrannosaurus falls asleep
  (status 0x04); anyone else takes damage equal to its hit points.
- **The blowgun** (32): 1 time in 2, unless the defender is protected (status 0x01) or immune (the
  0x1810d list below), the defender is poisoned instead of hurt: `<Name> poisoned!`. Otherwise the blow does
  its damage.
- **A lit torch on a web** makes a fire (242) on the web's tile (0xa5d:0xeb6, status 0x20).
- **Otherwise** (0x1b234): a weapon of damage d from 2 to 254 rolls 1-d; the defender's armour class a,
  when over 0, takes 1-a off. Nothing left: `<Name> grazed.`. Otherwise the blow lands.
- A creature with flag 0x40 poisons with a blow that lands, 1 time in 4, unless the defender is
  protected (actor status 0x01, as U6's protection) or immune (0x1810d: an actor whose own kind is
  the tyrannosaurus 294 or an apatosaurus 286/287, and actors 11, 25, 31, 33, 35, 47 and 55):
  `<Name> poisoned!` (the name with a capital).

**The wound** (0x1a99b; any damage, a blow's or a blast's): only on the party's level. A hit on the
queen (328) is taken by actor 54. The 0x1810d actors (above) take no harm at all: their hit points are set
to 255. A hit on the raft (237) is taken by a party member: a random one when the party is more than
one, else the leader. A blow that wakes (0x1a181): the struck one flashes (tile 257, sound 7, or 8 for a
party member) and a sleeper stands up, unless its own kind is the tyrannosaurus (294), which sleeps on.
The wound comes off the hit points; at 0 or below the actor dies (§3d).

### 3d. A death (0x1a6ad, 0x18764)

- **The words**: `<Name> killed!`, or `<Name> falls!` for actors 0-15 (the named people), whatever killed
  it; a carnivorous orchid (282) dies in silence (§3f; its wounds are se_eggs.md §5), and so does anyone while a scene runs (DS:0xa39).
- **The story**: actor 23's death sets talk flag 5 of actor 53; actor 180's sets talk flag 7 of actor 48
  (the talk flags of converse.md §6c, DS:0x6efc by actor).
- **The experience**: only a blow (0x1b47d) pays, to the striker, and only for an evil or chaotic victim
  (status bit 0x20). From the creature table (§4), by the victim's kind: (A × damage + (HP + armour) ×
  dexterity) / 16, where A is the strength for a creature with flag 0x20, else the dexterity. A kind
  not in the table pays nothing. Experience stops at 9999 (0x186f7). The spear of shamap's kill (§3c)
  and a blast (§3b) pay nothing.
- **The party**: a companion who dies leaves the party (the leader stays, and falls, §3e).
- **A rider** (307) gets off its turtle first, as U)se does (se_use.md §5w); with no shore beside it,
  `Only next to land!` and it leaves nothing.
- **The remains** (0x18764), by the victim's kind:

  | kind | remains |
  |---|---|
  | 310-316, 318, 320-326, 317 | a body (249) in frames 0-15, in the order 310 311 312 313 314 315 316 318 320 321 322 323 324 325 326 317 |
  | 283, 284, 288, 289, 294, 296, 297, 301, 303, 305, 327, 330-337 | a dead creature (280) in frame 1, 3, 6, 8, 13, 21, 23, 25, 27, 30, 33, 35, 37, 39, 41, 43, 45, 46, 47 |
  | 299 baby pteranodon | a roast pteranodon (107) |
  | anything else | nothing |

  Actors 11, 31, 47 and 55 leave nothing, nor does a kind with flag 0x800 or 0x1000. The remains stand on
  the victim's tile with its quality = the victim and hold what it carried (0xbf8f); they are temporary
  for actors 192 and up. The big creatures add a second piece, the next frame of the same remains, with
  blood around it, each blood a 1-in-2 chance:

  | kind | second piece | blood (each tile) |
  |---|---|---|
  | 284, 289, 305 | at (x+1, y) | (x+1, y), (x, y), (x-1, y) |
  | 303 | at (x, y+1) | (x, y+1), (x, y), (x, y-1) |
  | 294 | frame 17 at (x, y+2) | the 2 × 4 tiles from (x-1, y-1) to (x, y+2) |
  | 297 | at (x+1, y) | none |

  Any other kind with flag 0x2000, for actors 16 and up, leaves one blood (248, a random frame 0-2,
  temporary) on its tile. A temporary actor's slot is freed.
- **The leader** falling is §3e.

### 3e. The leader falls (0x17a33, 0x27566, 0x8cb:0xc73)

When the Avatar (actor 1) is dead: `Overcome by your wounds, you fall unconscious..... `, and then, a
week later (the day moves on 7, the hour stays), `....You awaken in Intanya's hut, feeling restored.`:

- every dead companion of the party stands up at its body with what the body held, and joins again;
- the party is at (412, 291) on the surface;
- each member gets off its turtle, wakes, is cured, has all its hit points and turns good (status bits
  0x60 = 0x40), and keeps three quarters of its experience;
- whoever was attacking the party (worktype 0x9b) goes back to its schedule (0x81).

### 3f. Things broken (0x1a99b)

A blow on an object, or a blast's 30 on a tree (0x218f5, se_use.md §5b): a stackable thing (the 48 kinds at DS:0x806) or one whose count is 0 takes
nothing. Otherwise it flashes; a count above the damage drops by the damage, else the thing breaks,
without a word:

- **the bamboo plant** (127) leaves a bamboo pole (128, temporary) on its tile; the plants on the map have
  a count of 0, so a blow does not cut them;
- **a tree** (263, its parts 264 at (x-2, y-1), 265 at (x, y-2) and 266 at (x-2, y-3)) falls northward
  (sound 31): fallen-tree pieces 260 frame 1 at (x, y-2), 261 frame 1 at (x-1, y-2), 261 frame 5 at
  (x+1, y-2), 261 frame 9 at (x+1, y-4) and 262 frame 3 at (x+1, y-5), all lasting. Each actor on the tiles
  it falls across (the 2 × 5 mask at DS:0x10af: x..x+1, y-6..y-2, all but (x, y-2)) is pushed to the first free tile around it (by column) and takes 2-20 (two rolls of
  1-10); with no free tile it takes all its hit points;
- **the control panel** (222) and **the dark moonstone** (147) start scenes of the story (§3g);
- **anything else** is gone.

The carnivorous orchids (282, a head; 292, its stalk, whose quality is the head) take their wounds by
0x1a3f0: a stalk's wound goes to its head (a stalk that is an actor gives half to every head); a head
that dies takes its stalk with it, step by step along its direction, and when its root (the head's
quality) has no heads left the root dies too and leaves an orchid bulb (78). The whole rule is se_eggs.md §5; they come with SE's eggs and
creatures' doings (item 6, later).

### 3g. The control panel and the dark moonstone (0x1a99b, 0x1ded4, 0x1e34e)

The new game has one control panel, at (215,197) on level 3 with a count of 15, among the large crystals
(256), and one dark moonstone, at (67,67) on level 1 with a count of 20.

**The panel breaks:**
1. sound 0;
2. the burst of the grenade's kind (0x276b:0xe85, tile 0x106): nine rays from the panel to the 3x3 around it.
   Each actor it reaches, and one on the panel's own tile, takes 1-30 through the damage routine. The
   blow's attacker usually stands there too;
3. the panel goes;
4. every large crystal (256) among the objects held (0x100-0xbff: the current level in the engine) becomes
   the dark one (223), keeping its frame;
5. if talk flag 1 lacks bit 0x04, the scene.

**The panel's scene (0x1ded4).** DS:0xa39 is set for it, so its deaths say nothing. In order:
- song 16 plays (0x2701:0x190); afterwards the place's song comes back (0x2701:0x1f1);
- the message area is cleared; the eyes in the top border turn blue (map tiles 373 and 374, as in the gem
  scene, se_use.md §5d);
- Yunapotli (actor 11) takes frame 11. Everything it holds is put down where it stands (0xa5d:0x739 gives an
  object's first content, 0xa5d:0x1c76 moves it). An object of its own look (the automaton 319, frame 11) is
  made there;
- Spector (47), Huitlapacti (31), Zipactriotl (55), Katalkotl (35) and Yunapotli (11) die (0x1a6ad, §3d).
  Only Katalkotl leaves remains, since 11, 31, 47 and 55 are on the no-remains list. A companion among them
  leaves the party;
- Johann (actor 5) is moved to Yunapotli's tile. His status loses bit 0x10 (the dead flag), his HP become
  the maximum (0x1810:0x62a) and he joins the party (0xf8d:0x290, se_use.md §4);
- Moctapotl (40) and Oaxtepac (44) become nobles (322, frame 0), in both the live look and the saved look
  (the far table at DS:-0x4ac2). Bit 0x80 of their bytes in the far table at DS:0x3298 is cleared;
- talk flags: Moctapotl's gets 0x80, flag 1 gets 0x04 (the scene never comes again), Tlapatla's (49) gets 0x20;
- a day passes: 24 passes of the world, an hour each (0x359:0x11b6);
- every actor that is a robosaurus (297) or an argent sergeant (301) dies (0x1a6ad), with its remains;
- among the objects: each floor plate (68) becomes the inert one (38, frame 0). Each egg (230) whose first
  content is a robosaurus or an argent sergeant goes, with it;
- the light object (11) takes map tile 285's picture in place of its own (522). As in the gem scene, no
  light object is in the game's data, so nothing shows it.

**The moonstone breaks:**
- three rounds, each a sound 0 and the same burst twice: tile 0x104, then 0x106, then 0x102. What the
  bursts reach is not used, so nobody is hurt;
- the stone goes;
- the scene (0x1e34e): song 12, the message area cleared, the blue eyes.

Then DOS ends the game. It shuts down its sound and video and runs `savage.exe` (DS:0x127b), which plays
the ending (item 9), with these arguments:
- the game directory;
- `D`, `B` and `Y` followed by the day, month and year, the digits made by writing day+99 (month+99,
  year+100) and putting the letter over the first digit;
- `LA` when Aiela's talk flag (actor 2) has 0x20, else `LT` when Tristia's (51) has, else nothing: the
  companion the ending shows.

**The engine** builds all of the above except:
- the sounds, songs and eyes (items 8 and 9);
- the far table at DS:0x3298 (unknown);
- the light's picture.

It also:
- silences the robots' eggs (chance 0, which the save keeps) rather than deleting them, on the whole level
  rather than DOS's loaded area (se_eggs.md);
- leaves an actor that is already dead alone;
- kills an actor on another level without remains;
- puts Johann beside the Avatar if Yunapotli is not on the current level.

The moonstone ends play (the game is over). Item 9 adds the ending itself.

## 4. The creature table (segment 0x2f43)

47 creatures, by obj_n (0x2f43:2, ended by 0), then a byte column each: strength (0x63), dexterity
(0x93), intelligence (0xc3), armour (0xf3), damage (0x123), HP (0x153), the status byte a new one gets
(0x183: its alignment, bits 0x60 as U6's: 0 neutral, 0x20 evil, 0x40 good, 0x60 chaotic), and a word of
flags (0x1b4): 0x20 bare hands by strength, 0x40 poisons, 0x100 flies, 0x200 swims (se_use.md §5w).
Strength, dexterity, intelligence and HP are rolled as U6's are (v - v/2 + two rolls of 0 to v/2). A
thing not in the table reads the word before the flags (0x1000) and no stats.

| creature | str | dex | int | armour | damage | HP | status | flags |
|---|---|---|---|---|---|---|---|---|
| 310 modern man | 20 | 20 | 20 | 0 | 3 | 10 | 0x40 | 0x6000 |
| 311 professor | 20 | 20 | 20 | 0 | 3 | 10 | 0x40 | 0x6000 |
| 312 reporter | 20 | 20 | 20 | 0 | 3 | 10 | 0x40 | 0x6000 |
| 313 woman | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 314 shaman | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 315 chieftain | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 316 man | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 318 Sakkhra | 20 | 20 | 20 | 3 | 5 | 10 | 0x00 | 0x6000 |
| 319 automaton | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x4000 |
| 320 man | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 321 woman | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 322 noble | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 323 myrmidex drone | 10 | 45 | 1 | 8 | 10 | 10 | 0x20 | 0x6000 |
| 324 woman | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 325 neanderthal | 25 | 15 | 10 | 0 | 10 | 15 | 0x00 | 0x6000 |
| 326 man | 20 | 20 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 317 parrot | 1 | 20 | 1 | 0 | 2 | 3 | 0x00 | 0x2100 |
| 294 tyrannosaurus | 60 | 5 | 1 | 255 | 150 | 255 | 0x60 | 0x2024 |
| 296 gorilla | 35 | 15 | 10 | 3 | 25 | 40 | 0x60 | 0x2020 |
| 284 baby apatosaurus | 20 | 1 | 1 | 4 | 5 | 20 | 0x00 | 0x2025 |
| 289 allosaurus | 40 | 11 | 2 | 8 | 35 | 50 | 0x60 | 0x2025 |
| 297 robosaurus | 50 | 9 | 60 | 15 | 40 | 60 | 0x60 | 0x002d |
| 303 stegosaurus | 30 | 3 | 1 | 8 | 20 | 25 | 0x00 | 0x2025 |
| 305 triceratops | 40 | 8 | 2 | 10 | 30 | 50 | 0x00 | 0x2025 |
| 288 pteranodon | 10 | 35 | 2 | 3 | 20 | 10 | 0x60 | 0x2105 |
| 283 serpent woman | 17 | 25 | 12 | 2 | 15 | 20 | 0x20 | 0x2044 |
| 300 plesiosaur | 25 | 20 | 2 | 5 | 15 | 20 | 0x60 | 0x020d |
| 327 ankylosaurus | 25 | 2 | 1 | 15 | 15 | 25 | 0x00 | 0x2025 |
| 330 deinonychus | 7 | 20 | 2 | 3 | 10 | 20 | 0x60 | 0x2001 |
| 331 sabretooth tiger | 30 | 25 | 7 | 3 | 30 | 35 | 0x60 | 0x2000 |
| 332 sloth | 20 | 5 | 3 | 1 | 5 | 20 | 0x00 | 0x2020 |
| 333 jaguar | 22 | 30 | 8 | 2 | 25 | 25 | 0x60 | 0x2000 |
| 334 dimetrodon | 30 | 5 | 2 | 6 | 25 | 20 | 0x60 | 0x2025 |
| 335 bear | 30 | 15 | 6 | 3 | 27 | 40 | 0x60 | 0x2020 |
| 291 wisp | 1 | 60 | 60 | 0 | 1 | 30 | 0x00 | 0x1429 |
| 299 baby pteranodon | 3 | 2 | 1 | 0 | 3 | 3 | 0x00 | 0x0011 |
| 301 argent sergeant | 20 | 15 | 60 | 7 | 15 | 30 | 0x60 | 0x002d |
| 302 turtle | 15 | 5 | 4 | 10 | 10 | 25 | 0x00 | 0x0a28 |
| 307 (no name) | 15 | 5 | 4 | 5 | 10 | 25 | 0x00 | 0x4a28 |
| 308 snake | 3 | 7 | 6 | 1 | 2 | 5 | 0x60 | 0x2840 |
| 309 alphadon | 5 | 13 | 4 | 0 | 6 | 8 | 0x60 | 0x2840 |
| 336 giant spider | 9 | 11 | 4 | 0 | 8 | 10 | 0x60 | 0x0051 |
| 337 (no name) | 5 | 14 | 6 | 0 | 2 | 7 | 0x00 | 0x2020 |
| 328 Myrmidex queen | 0 | 0 | 30 | 10 | 0 | 20 | 0x20 | 0x3000 |
| 329 dancer | 20 | 30 | 20 | 0 | 3 | 10 | 0x00 | 0x6000 |
| 282 carnivorous orchid | 50 | 30 | 0 | 5 | 10 | 10 | 0x60 | 0x1011 |
| 292 carnivorous orchid | 0 | 0 | 0 | 5 | 0 | 10 | 0x60 | 0x1011 |

## 5. Open questions
- What flags 0x08, 0x400 and 0x4000 mean; 0x800 and 0x1000 mean "no remains", 0x2000 "bleeds" (§3d).
- 0x1a6ad would pay 500 for a victim whose saved look (-0x4ac2) tests against 0 (`test $0, …`), which never
  holds, so DOS never pays it; what was meant is unknown, and nothing is built.
- 0x1a3f0 hands back as "experience" whatever is in a register: a head's remaining hit points when it
  lives, 10 when the root dies, 0 for a stalk. To be decided with the orchids.
- What bit 0x80 of the bytes in the far table at DS:0x3298 means; the panel's scene clears it for actors 40 and
  44 (§3g).
- Where DOS puts the remains of an actor on another level when a scene kills it (the engine leaves none).
- The flash when a creature with flag 0x1000 dies near the party (0x359:0xd58).
- A readied flintlock keeps its load in the engine per actor; the save keeps it with item 10.
