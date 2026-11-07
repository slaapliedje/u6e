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
   offerings, ranged weapons, monsters.
7. Rest and food: SE's rest rules and the dream at the first rest.
8. Panel and screens: SCREEN.LZC and BKGRND.LZC, SE's portraits, cursor and font as DOS draws them.
9. Front end and scenes: intro, character creation, MIDGAME1-5, ENDGAME, MUSIC.LZC.
10. Saves: SE's objlist layout, the 8 slots.
11. Levels: the underground levels, the trap door, level transitions.
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
