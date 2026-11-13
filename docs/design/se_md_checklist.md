# Savage Empire and Martian Dreams: the road to the U6 standard

Started 2026-11-06, after U6's deviations register came down to the user's choices and the
machine rows. The bar is the same as U6's: each game plays as its DOS GAME.EXE has it, and the
differences left are listed in its own register. Rules come from that game's GAME.EXE
(`~/src/u6e-re/dos/{se,md}.bin`, Turbo C++, not EXEPACKed), read the way U6's were
(docs/design/multigame.md has the history).

Each item names what it is done by. Savage Empire goes first; Martian Dreams follows the same
list once SE's seams exist.

## Shared groundwork
1. **Full packs.** `mkworld` builds SE and MD in the full pack layout from each game's own
   files: actors and schedule (LZDNGBLK roster), basetile, animation, tile flags 1-3, indoor and
   roof data, darkening tables, weights, stackables, cursor, readied gear, gold. Sections a game
   lacks are written empty, not stubbed in the engine. The core-pack path in gamedemo.c goes.
   Done when SE and MD boot from full packs with NPCs walking their schedules, and the U6 pack is
   byte-identical.
2. **Game layer seams.** gamedemo.c holds U6's game layer: about 120 literal obj_n tests plus
   the use switch, spells, food, light sources, the paper doll. These move behind a per-game
   table (use, look/search, ready, combat numbers, rest, the casting verb, new game, panel art),
   with U6's code moved, not changed. Done when U6 plays byte-identical through the bot run and the
   test suite, and SE/MD no longer reach U6 rules by accident.

## Savage Empire
3. Conversation: TALK.LZC scripts on the conversation VM, with SE's opcode differences read from
   its talk driver (`talkdr`).
4. Objects: SE's ready table (modern weapons, "can't use modern weapons"), weapons and armour,
   look and search texts.
5. U)se: SE's whole use switch (grinding, gunpowder, pots and clay, tar, torches, fishing, the
   bucket, skinning, the camera, the raft and turtles, flintlocks, grenades), read case by case.
6. Combat and the party's powers: Triolo's and Intanya's invocations (Heluzz, Aphazz, Motazz),
   offerings, ranged weapons, monsters; the totems (rules/se_use.md §5n); fallen creatures as bodies in their actor slots (status 0x10)
   and the bell on a fallen tyrannosaurus (rules/se_use.md §5i); the hazard routine (fire and other
   harmful tiles, 0x359:0xda8, used by the bucket's fires §5q); trees in a blast (§5b); a readied
   flintlock's frame (loading, firing; §5k).
7. Rest and food: SE's rest rules and the dream at the first rest.
8. Panel and screens: SCREEN.LZC and BKGRND.LZC, SE's portraits, cursor and font as DOS draws them.
9. Front end and scenes: intro, character creation, MIDGAME1-5, ENDGAME, MUSIC.LZC.
10. Saves: SE's objlist layout, the 8 slots.
11. Levels: the underground levels, the trap door, level transitions (SE's level change 0x8cb:0x855:
    the rope, the hole in the ceiling, the trap door 160, the stairs 259 and the gem's trap door sealing
    behind the party, caves 163 and 227; the jungle drum's scene, rules/se_use.md §5r).
12. Register: SE's own deviations list, worked to zero like U6's.

## Martian Dreams
13. Items 3-12 again against MD's GAME.EXE (its overlay after the MZ image included), plus what
    MD alone has: the era technology, berries, the Lowell interview at creation, the many scenes.

## Status
- 2026-11-06: list written. Item 1 done: SE and MD boot from full packs (Kurak hut with the
  Avatar on the bed, MD inside the capsule); SE's NPCs walk their schedules (NPCWALK hook); U6.PAK
  and U6FE.PAK byte-identical to the old builder; SEUSE self-test, make check and the logic tests
  pass. Found on the way: SE's party carries 23 items but none read as readied (item 4), and the
  border and command bar still use U6's tile numbers (item 8). Item 2 next.
- 2026-11-06: item 2 done (design/game_layer.md): gamedemo.c's U6 rules sit behind a per-game
  table of 25 entries (23 hooks, 2 flags) and 75 named objects; SE and MD reach only their own entries (talk,
  their use, the gold pool) and otherwise none. U6 checked unchanged: the bot run (castle, bridge,
  Britain, shop, cave) gives the same log and transcripts as before, every U6 self-test hook and
  mode gives the same console output, make check and the logic tests pass. SE and MD boot; SEUSE
  passes. Item 3 next.
- 2026-11-07: item 3 done (converse.md §6c): SE's talk driver read from GAME.EXE. Its scripts ship in the
  pack (CTSC) and run on the shared VM in SE's dialect: flags 0-15, EVENT (0xD1: guards, Myrmidex,
  the drum, Fabozz, Yunapotli's door, Intanya's quit), 0xDF, no karma; SE's own refusals; the face
  follows PORTRAIT; talk flags load and save at SE's objlist places. All 68 scripts run on the host
  (convvm TALK.LZC); `make check-se-talk` passes in the game. Found for U6 too: DOS caps STR/DEX/INT
  at 30 and EXP at 9999 in conversations (now so). Open: '*' page waits and long replies (both games
  show '*' and cut a reply at 8 lines; DOS waits for a key); SE's other objlist fields still sit at
  U6's places (item 10); the spawned men and drones use U6's creature table (item 6). Item 4 next.
- 2026-11-07: the page waits built for U6 and SE (converse.md §6d, read from both GAME.EXEs): the line
  count, the page wait (Space/Enter/Esc/click, the arrow), `*` waits, the cursor glyphs (arrow; ankh in
  U6, skull in SE); the message scroll pages the same way. MD's GAME.EXE pages differently (item 13).
- 2026-11-10: SE bot runs (bot_harness.md §5b): `make bot-smoke` checks U6 against a baseline and
  runs an SE smoke; `tools/bot/se.py OUT talk` sweeps SE's conversations. Fixed: who answers (the
  dead and asleep status bits, not HP; converse.md §6c) and SE/MD's look line ("You see "). Open:
  SE's doors (item 5): the party cannot leave the hut it wakes in; on a 4 MB Falcon SE leaves 19 KB
  free and restore fails ("Not enough memory to restore.").
- 2026-11-11: item 5 started (rules/se_use.md): SE's use dispatcher read from its GAME.EXE (the usable
  list of 84 objects, "Not possible.", "nothing!", what an actor on the tile yields) and its 81-case
  switch mapped object by object; the three doors built as DOS has them (234 turns along the wall and
  can be blocked, Jimmy's door wakes him and he joins and talks, Yunapotli's door, 275 swings; Aiela's
  scene waits for item 9). `make check-se-use`. The party now leaves the waking hut. SE's faces and
  scripts stream on 4 MB machines (memory_streaming.md, the library tail): 777 KB free on the stock
  Falcon, restore works, `make bot-smoke` is clean. Next in item 5: the empty-tile uses (trees, lava,
  yucca), food, the crate, then the other handlers in §3's order.
- 2026-11-11: item 5 continued: the bare-tile uses (a tree's branch, the yucca's flax, lava), the
  "On " prompt for a second thing (map or pack, verb X), the fire (pots, tortillas, charcoal,
  incinerating), food and the crate as DOS has them. SE's stackables now come from its GAME.EXE's list
  (LOOK's plurals missed 18, gold and corn meal among them). Gunpowder in the fire waits for the
  grenades' explosion.
- 2026-11-11: item 5 continued (rules/se_use.md §5b): grenades and the anti-Myrmidex device (lighting, the
  one 3-turn fuse, the 3x3 blast, chained grenades, webs cleared), gunpowder in the fire, and U)se of
  carried and readied things (SE had none). Left in the blast: boulders and trees (with item 6's
  object damage); the device going off when moved and thrown breakables are SE's M)ove rules.
- 2026-11-11: item 5 continued (rules/se_use.md §5c): the boulder in a blast (the push, the roll down
  slopes, crushing, and damming the waterfall at (292,66) to open the cave behind it). The boulders
  hatch from SE's eggs (object 230, e.g. at (295,64)); **SE's eggs are not built and no item lists
  them** — they belong with item 11 (levels) or a new item before the register.
- 2026-11-11: item 5 continued (rules/se_use.md §5d): the giant gem set on the gem stand at (170,483):
  the first time the clock runs on to noon and a trap door opens at (192,460). Its song waits for item 9,
  the border's blue eyes for item 8, the trap door's way down for item 11; moving the gem onto the
  stand waits for SE's M)ove rules. The magnesium ribbon and the camera (§5e): the flash, and waking the giant stone head.
  Knives and scissors (§5f): cloth strips, butchering, skinning the tiger, plucking.
  The hammers (§5g): blue stone, charcoal. The vine (§5h): a rope over a hole. The bell (§5i) waits
  for item 6. Jimmy's notebook (§5j). The flintlock loads (§5k; readied ones with item 6). The turtle bait goes with the raft and the turtles. The rope and the hole in the ceiling go to item 11 (they are SE's level change).
- 2026-11-11: item 5 continued (rules/se_move.md): SE's M)ove read from its GAME.EXE and built: pushing (its
  words, the strength test, things lost in deep water, lava or down a slope, into a container on the map),
  giving (16 things or the weight), its nine containers (what each takes, the jar's picture), and the
  mortar's grinding (se_use.md §5l); and SE's D)rop (§6: things lost, breakables, the squished pot, into a
  container, the device going off). The stalks, the bones and the jug of plachta (se_use.md §5m); the totems go to item 6
  (§5n). The folded skin, the fire extinguisher and the liquor (§5o). The digging stick and the cloth strips (§5p). The metal bucket and fire (§5q; the burning
  of people waits for item 6's hazard). The machine, the loom and the wire screen (§5r); the jungle drum's
  scene goes to item 11. The crate as DOS opens it (it was wrong), the brazier, the burnt logs, the metal head and the
  tarred strips (§5s). The clay, the gunpowder and the torches (§5t). The fishing pole and the crystal garden (§5u). The tar pit, the headless statue and the crystals (§5v, §5s). Every
  case of §3 is now built but the raft, the turtles and 307 (one vehicle slice, next) and those
  waiting on items 6 (the totems, the bell, fire's harm, the readied flintlock) and 11 (the rope, the
  hole in the ceiling, the drum).
- 2026-11-12: SE's register started (rules/se_deviations.md, item 12). The user takes the developers'
  intent over DOS's slips: eleven fixed (the gem on the hour, the charcoal message, the mortar, the fishing
  pole, the thrown lit grenade, the folded skin, the bucket's water, tar on webs and eggs, the gem stand,
  thrown containers spilling, the fire waves). ScummVM's SE U)se, looked at first, is mostly unwritten.
- 2026-11-12: item 5: the vehicles (rules/se_use.md §5w), read from SE's GAME.EXE and built: the raft
  (four paddlers, a 2x2 vessel on water, N/E/S/W, landing by the shore), the turtles and their riders
  (307, swimmers on deep water, a turtle left behind on landing), the turtle bait. Every case of SE's
  U)se switch is now built but those waiting on items 6 and 11. Three engine choices went to
  se_deviations.md for the user.
- 2026-11-12: item 4's weapons and readying, and item 6 begun (rules/se_combat.md): SE's ready classes and
  the modern weapons, weapon damage and range, armour, the hit (on the threshold, U6 needs over it) and
  damage rolls, poison from a blow, and SE's 47-creature table, read from its GAME.EXE. The engine's
  combat, equip and monster tables answer for SE when it is loaded (rules_select). Left in item 4: SE's
  look and search texts. Next in item 6: the special blows, ammunition and shooting, death and experience.
- 2026-11-12: item 6's blows (rules/se_combat.md §3): ammunition and its words, the flintlock's load (kept
  while readied, loaded by a fighter in combat), shooting and the flight's tiles, thrown weapons landing with
  a spare readied, grenades and devices thrown into their blasts, what can be struck (webs, trees), the
  spear of shamap, the blowgun's poison, a torch on a web, the charged black staff, the queen's wound and
  the unharmed actors. DOS slip 13 (a failed hit roll strikes anyway) built as the intent. Next in item 6:
  death, experience and remains, and the breaking of things (bamboo, trees, the control panel).
- 2026-11-12: item 6's deaths and broken things (rules/se_combat.md §3c-3f): `killed!` / `falls!` for any death,
  SE's experience from its creature table (blows only, to 9999), the remains by kind (bodies 249, dead creatures
  280, the roast pteranodon, the big creatures' second piece and blood), actor 23's and 180's story flags, a
  rider off its turtle, the tyrannosaurus sleeping through blows, the Avatar's fall (Intanya's hut, a week on,
  three quarters of the experience), things with a count breaking (bamboo to a pole, trees felled onto
  whoever stands there, by a blow or a grenade's 30). Next in item 6: the
  control panel's and the dark moonstone's scenes, the orchids with SE's creatures and eggs.
- 2026-11-12: item 6's control panel and dark moonstone (rules/se_combat.md §3g). The panel's burst darkens the
  large crystals. Its scene, once: Yunapotli left a statue, five of the Kotl's people dead, Johann back in the
  party, two nobles made, a day gone, the robots dead, the floor plates stopped. The moonstone ends the game:
  play stops, and item 9 adds the ending. Waiting on other items: the robot eggs (SE eggs), the songs and eyes
  (items 8 and 9). Next in item 6: the orchids, with SE's creatures and eggs.
