# Bot harness: playing u6e through the keyboard

> Provenance
> - Derived from : our own design; the game is driven only through emulated key presses.
> - References   : Hatari's command FIFO (`hatari-event keypress`) and NatFeats (`NF_STDERR`);
>                  the reference's T)alk targeting and ASKC input (usecode_vm.md) where the bot
>                  exposed differences.
> - Tools        : tools/bot (u6bot.py, nav.py, play.py, run.py), tools/convkw.c, Hatari 2.6.1.
> - Validated by  : the first milestone run (castle, drawbridge, Britain) on an emulated stock
>                   4 MB Falcon, 2026-10-30: 3,600 keys, 22 NPCs talked through every keyword.
> - Confidence   : confirmed (the harness works); the walkthrough itself is in progress.
> - Last updated : 2026-10-30

## 1. Purpose

Play the whole game the way a player does, so every system gets exercised in order, and collect
everything that looks wrong. The bot presses keys; it never teleports, never pokes memory and
never calls game functions. Whatever it finds, a player would find.

## 2. How it fits together

```
run.py (segments of the walkthrough)
  play.py  Player: talk_to / sweep / say / issue log
  nav.py   Nav: A* over the game's own passability dump, walking key by key
  u6bot.py Game: Hatari process, one key -> one report, dumps
      |  hatari-event keypress NN  (command FIFO)          ^  @S / @A / @O / @P lines
      v                                                    |  (NatFeats NF_STDERR -> stderr)
  Hatari: stock Falcon, 4 MB, no Fast RAM, TOS 4.04, fast-forward, --natfeats on
      GAMEF.PRG with BOT.REQ in its folder
```

## 3. The game side (render/gamedemo.c, only with BOT.REQ)

- **The report** `@S`: after every key the main loop has handled, once nothing runs on its own
  (no camp, moongate walk or click-travel in progress). Fields: `k` keys received (the lock-step
  counter), `busy`, `L X Y`, the clock and date, the armed verb, `talk` (actor id) and `ask`
  (text entry open), `askc` (the keys a y/n prompt accepts) and `num` (a number prompt), the
  cast / mantra / inventory / status / peer panels, combat mode, vehicle, `over` (the ending),
  `foes` (hostile actors within 8 tiles), the party `id:hp/max:mp:level`, then the free text:
  `msg` (the message box), `conv` (the conversation transcript), `typed`.
- **Dumps** on function keys (no game effect): F2 actors on this level, F3 objects within 24
  tiles plus the pack, F4 the passability file `BOTPASS.BIN` (one byte per tile: bit0 walkable as
  the player's step decides, bit3 water) and `BOTDOOR.TXT` (every door: x y frame).
- **Output channel**: `BOT.REQ` holding `n` sends the lines through Hatari's NatFeats `NF_STDERR`
  (opcodes $7300 / $7301), which never touches the screen; otherwise GEMDOS `Cconws` (the VT-52
  would draw over the game).

## 4. The host side

- **Lock-step**: `Game.key()` sends one scancode and waits for the report whose `k` covers it, so
  the game's "drain the backlog to the newest key" rule never merges keys. Scancodes go as two
  digits: Hatari reads a one-character argument as that character (`1` would type a "1", not Esc).
  The FIFO is opened per command: Hatari polls it non-blocking and complains on every empty read
  while a writer holds it open.
- **Esc** is sent only in text entry or with a verb armed. In plain play u6e's Esc quits.
- **Walking**: A* on the dump, 4 directions. A closed door on the route is opened with U)se and
  trusted only if the game's message says so; a refused step first re-dumps the map, then waits a
  turn, then routes around that tile. A level change ends the walk.
- **Talking**: get within 3 tiles, T)alk, walk the crosshair onto the NPC, Enter. The bot answers
  a question the script itself answers (the Compendium questions: the keyword list after the
  question text, from `convkw <dir> <npc>`), declines y/n prompts with one key, picks each party
  member in turn at "Which of you?", then sweeps the NPC's keywords (`convkw <dir>` lists them
  from the user's CONVERSE files at run time; nothing derived from game text is stored) except
  join/leave, and says bye.
- **Issues** (`issues.jsonl`, with a screenshot): "not implemented" text, console output that is
  not ours, a T)alk that did not open, a keyword with no reply, an empty reply, a conversation that
  will not end, a crash of a segment. `transcripts.json` keeps every conversation.

## 5. Running it

```
make game-falcon convkw build/mksave
mkdir hd; cp build/gamef.prg hd/GAMEF.PRG; cp refdata/dos/u6/{CONVERSE,PORTRAIT}.? hd/
cp build/U6.PAK build/U6FE.PAK hd/; build/mksave refdata/dos/u6 hd/SAVE.000; echo 3 > hd/MIDI.CFG
python3 tools/bot/run.py hd out [castle bridge britain]
```

The first milestone (Lord British, the party, the castle, the crank and the bridge, every NPC in
Britain) takes about 110 s of wall time for ~3,600 keys.

## 6. Found by the bot so far (2026-10-30, all fixed)

| finding | where |
|---|---|
| The starting reagents and spellbook named "Nothing": seeded before BASETILE was loaded | gamedemo.c boot |
| The opened castle drawbridge could not be crossed: forced passability (tileflags3 bit2) was never applied | world.c + build_blocked |
| The clock ran ~40× too fast: a step cost a flat 10 minutes | movement_turns.md §3 |
| U)se on a door hung in a doorway worked the arch: "Nothing happens with the doorway." | usecode_use.md Doors |
| Keyboard T)alk only took an adjacent direction; the reference's Talk- cursor reaches anyone in view (shopkeepers behind counters) | usecode_vm.md |
| A y/n prompt accepted a typed word, which ended the conversation; the reference takes one listed key | usecode_vm.md §6c |
| A backtick before a substitution was printed literally ("\`Jim") | usecode_vm.md §6d |

## 7. Next

Segments beyond Britain: the shrines with their runes and mantras, dungeons (ladders, darkness,
combat), ships and moongates, the gargoyle lands and the ending. Combat handling (A)ttack with
the crosshair while `foes` > 0), shopping with gold, resting, and spells come with them.
