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
to 255. The breaking of things (the bamboo plant, trees, the control panel, the dark moonstone) and the
death are item 6's next slice.

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
- What flags 0x08, 0x400, 0x800, 0x1000, 0x2000 and 0x4000 mean.
- Death, experience and remains (0x1a181, 0x1a6ad), and the breaking of things in 0x1a99b (127 the bamboo
  plant to a pole, a tree felled into 260/261, the control panel 222, the dark moonstone 147).
- The carnivorous orchids (282, 292) take a wound by 0x1a3f0, not yet read.
- A readied flintlock keeps its load in the engine per actor; the save keeps it with item 10.
