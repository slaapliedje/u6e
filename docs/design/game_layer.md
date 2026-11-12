# The game layer: one table per game

> Provenance
> - Derived from: our own gamedemo.c (U6's rules, each read from U6 GAME.EXE 4.5 and specified in
>   docs/rules/); se_md_checklist.md item 2.
> - Last updated: 2026-11-11 (SE's U)se entries; named use_takes)

## 1. Why
gamedemo.c is the engine's game layer and it was written for Ultima VI. Its shared code (the verbs,
drawing an actor, the clock, combat, the pack, conversations, the save) tests U6 object numbers
inline: a chair is 252, a torch 90, a body 339, the moongates 84 and 85. The Savage Empire and
Martian Dreams number their objects differently, so on SE a test for 252 finds whatever SE keeps
under 252, and a U6 rule runs on the wrong thing. Item 2 of se_md_checklist.md moves every such
rule behind a per-game table, so each game reaches only its own rules, and SE's and MD's own rules
(items 3-13) go into their own entries.

## 2. The table
`GameLayer` (gamedemo.c) holds two kinds of entry. The loader picks the game's table from the
GameDef the pack magic selected (`g_gl`), next to `g_gd` and `g_wko`.

**Named objects** (`g_gl->on.*`): the obj_n that shared code tests by identity. Shared code reads
the name, never the number. 0 means the game has no such object, and every test treats 0 as no
match (no object has obj_n 0 in a test). Ranges (the four door types, the eight reagents) are a
first number and a count. The U6 numbers are the ones the code held, unchanged.

**Hooks** (`g_gl->*`): whole U6 systems or U6 cases inside a shared verb. The U6 entry is the U6
code that stood at that call site, moved into a function without change. NULL means the game has
no such system yet; the call site then does what a game without it does (nothing, or the shared
"Not usable"/"Nothing happens" path). The SE and MD entries start NULL or as their existing
handlers, and fill in as items 3-13 read their GAME.EXEs.

Tables, not `if(game==U6)`: a rule that differs per game is an entry, so adding SE's version is a
new function in SE's table, not another branch in the shared code.

## 3. What stays U6's without an entry
- Functions reached only through a U6 hook (the use switch's cases, the spells, the moongates'
  helpers, the endgame) keep their literal numbers: the hook is the gate.
- The headless self-tests in main() (`*.REQ` sentinels) test U6 by U6 numbers. They run only on
  U6 (SEUSE is SE's own).
- Sound effect numbers and tile numbers are not obj_n. Tiles that differ per game are panel art
  (item 8), not part of this table.

## 4. Done when
U6 plays the same: `make check`, the logic tests and the bot run give the same results before and
after, with the U6 tables holding exactly the numbers the code held. SE and MD boot and the SEUSE
self-test passes, with SE reaching no U6 rule: every U6 hook is NULL or SE's own for SE, and every
named object is SE's surveyed number or 0.

## 5. The entries
The struct comments in gamedemo.c name the rule spec each one follows. In short:

**Hooks.** U6's entry is the code that stood at the call site; SE and MD have only the entries named.
| Group | Hooks | SE | MD |
|---|---|---|---|
| Verbs | talk, use_at (aboard, mounts, the cow, churn, well, passthroughs, ladders), use (the switch), usable, use_under (look under an unusable thing: U6), nothing and unusable (U)se's words; SE "nothing!" and "Not possible."), bot_door (the bot's door list), use_ground (a bare tile in reach: SE's trees, lava, yucca), use_pack, push_select and push_to (M)ove on the map), move_to and move_into (M)ove of a carried thing: to someone, into or out of a container), store (what a map container takes), drop_ok and dropped (D)rop: where it may land, what happens there), notposs, asks_count ("How many?"), rest, cast, see (the look line's opening: U6 "Thou dost see ", SE and MD "You see "), rides (the leader rides a creature: U)se on itself is the use) | use_at (se_use_at: aboard the raft, a rider, the raft, a turtle, rules/se_use.md §5w), rides, talk (se_talk: its scripts on the VM, converse.md §6c), use (se_use_object, rules/se_use.md), usable (its 84-object list), nothing, unusable, bot_door, use_ground, use_pack (se_use_pack: the same switch for a carried or readied thing), push_select, push_to, move_to, move_into, store, drop_ok, dropped, notposs (SE's M)ove, D)rop and containers, rules/se_move.md), asks_count, burn_tick (the grenade fuse) | talk (md_talk: name and look line only, until item 13) |
| Movement | veh_move (a step aboard the game's own vessel), lead_step (the leader's step by the game's rule), can_enter (a creature's step by the game's rule; else its movement type) | veh_move (the raft), lead_step (a rider), can_enter (the swimmers: the turtle, a rider) | |
| Saves | ol_talk, ol_talk_hi (where the objlist keeps the talk flags; 0 = U6's places) | 0x18F1, 0x12F1 | |
| The pack | gold_pool (one purse, not coins), ready_on/off/tick (rings, the cloak), theft | gold_pool | gold_pool |
| The clock | wind_tick, burn_tick (torches, keg fuses), npc_torches, moongates, eggs | | |
| Combat | max_mana, touch (drag, dissolve, rob, poison), shot_spend (ammunition), hazard (harmful tiles by tile number), foe_cast, avatar_death | | |
| The end | ending | | |

A game with no avatar_death ends play when the Avatar falls; with no ending, play stops. Both are
placeholders until SE's and MD's own rules are read (checklist items 6 and 9).

**Named objects** (`GameObjs`), U6's numbers in brackets where they are short:
- Drawing actors: throne (327), chair (252), bed (163) and its sleeper shape (146), musician
  shape (392), the sleep-shape table, small creatures who share a tile.
- Movement: burrower (360), mouse (354) and its holes, no_diag (413), cave (326), hole (308),
  the way to the Codex (416).
- Weight: weighed per ten (coins 88, reagents 65-72).
- Bodies: body (339), all remains (338-341), the mouse carried.
- The pack: torch (90), amulet (76), holders, containers, boxes that open by frame (98, 192,
  186), chest (98), charge (336), trap (337), spell (58), staff (78), keg (223), cube (62),
  invisibility ring (258).
- Look and use: doorway (301), doors (297-300), secret door (334), books read near or far,
  clock (159), sundial (235), holy flame (206), the faces of shrines and statues, moongates (84,
  85), moonstone (73), candles, what breaks thrown and what cracks instead.
- Combat: mana kin, halves (367, 352, 368) and the weapons that don't, glass sword (48),
  vessels and mounts (412, 414, 415, 423, 431), the one who does not die (409), bows, arrows,
  crossbows, bolts, triple crossbow, reach weapons, boomerang, sleep wand, slime (375), vine
  (366), mirror (123), corpser (357), cannon (221), the unmovable (411), weapons swung by
  strength, the people who yell for help.
- Eggs: egg (335).

`g_wko` (engine/ruleset.h) already named the Avatar, key, lockpick, spellbook, reagents and gold;
shared code now reads the spellbook and gold from there too, where it held 57 and 88.

## 6. What it left for later items
- engine/converse_vm.c (U6's conversation VM) still holds U6 numbers: SE talks through se_talk
  until item 3 reads SE's talk driver.
- engine/combat.c and engine/equip.c hold U6's weapon and ready tables (items 4 and 6).
- The panel, the status line (it shows the wind on MD) and the tile numbers of the border and
  the harmful tiles are panel art and tile data (items 8 and 6).
