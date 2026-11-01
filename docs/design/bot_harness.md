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
  cast / mantra / inventory / status / peer panels, combat mode, vehicle, `over` (the ending), `gold`, `karma`, `rest` (the camp prompt: 1 hours, 2 guard),
  `foes` (hostile actors within 8 tiles), the party `id:hp/max:mp:level`, then the free text:
  `msg` (the message box), `conv` (the conversation transcript), `typed`.
- **Dumps** on function keys (no game effect): F2 actors on this level (with each NPC's live worktype `wt`), F3 objects within 24
  tiles plus the pack and every member's readied things (`@E id slot obj`), F4 the passability file `BOTPASS.BIN` (one byte per tile: bit0 walkable as
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

Two reports were the bot's own mistakes, not the game's: Chuckles's goose riddle repeats until it
is answered "n" (the bot always said "y"; it now switches keys when a question repeats), and
Wilbur's "Which of you?" ends on 0 with no reply (the script jumps back to the keyword prompt).
The castle, bridge and Britain segments now run with no issues (3,632 keys, 111 s).

## 6b. Segments added 2026-10-31: combat, a cave, the orb

- **Combat** (`Player.fight`): combat mode on, A)ttack the nearest reachable foe through the
  crosshair when adjacent, else step toward it; until no reachable foe is within 8 tiles. A foe
  behind rock or water is not a fight (`foes()` keeps only foes with a walking route). `walk()`
  stops to fight whenever a new foe appears. The castle segment fights the opening gargoyles first.
- **cave**: walk from Britain to the cave at (185,436) (the one at (234,409) is walled in by
  mountains on the Britain side), light a torch from the pack, explore level 1, take the nearest
  way down to level 2, explore it, then climb back by the remembered arrival points.
- **orb**: U)se the Orb of the Moons, walk its crosshair to positions 3, 19 and 8, step into each
  red gate and check the destination against moongates.md §3.
- A walk ends on a level change or a jump of more than 2 tiles (a moongate, a resurrection).

Found and fixed (game):

| finding | where |
|---|---|
| No party death: melee and foe spells took the Avatar and companions to 0 HP and play went on | combat.md §3d |
| A finished conversation's portrait stayed up until a walking step; U)se, the pack and messages after a talk were hidden behind it | usecode_vm.md |
| The pack list ran through the message box and showed only 15 rows: the torches (row 17) could be selected but not seen | gamedemo draw_inventory |
| The orb's "Where:" took only the eight neighbours from the keyboard, so 18 of its 26 destinations needed the mouse | moongates.md §3 |

Bot-side mistakes found on the way: answering "y" forever to Chuckles's riddle, reading a
moongate jump as a failed step, chasing foes it could not reach, and resuming a surface walk on a
dungeon level. A full run (castle, bridge, Britain, cave, orb) now takes about 200 s with no issues.

## 6c. Segment added 2026-10-31: a shrine

- **shrine** (Compassion): Ariana (13) shows the rune and sends the party to her mother; Anya (15)
  grants permission; Ariana hands it over. U)se the rune with the right mantra away from the
  shrine ("No effect!"), the orb to position 4 (beside the shrine), fight the guardians, a wrong
  mantra at the force field ("Wrong mantra!", the field stays), the right one ("Done!", the field
  goes), then T)alk to the shrine: meditate, who (the Avatar), the mantra; the level shown in
  the party report must follow a "gained a level".

Found and fixed (game, all in the conversation VM; usecode_vm.md game state and §6c):

| finding | effect |
|---|---|
| GIVE / NEW / DELETE / HASOBJ treated only actor 0 as the player | Ariana said "here you are" and gave nothing; Lord British's castle key never arrived |
| WEIGHT took one argument instead of (object, quantity); CANCARRY was a constant | "you're carrying too much right now" at Ariana with a near-empty pack |
| The gold object was 104 (the shovel), not 88 | every merchant said "you don't have enough gold" whatever the party carried |
| Pack counts were indexed by the low byte of the object number | the quest items 394-417 seven scripts give would have arrived as other objects |
| GIVE / NEW dropped the quality argument | a given key opened no lock |

Bot-side: the order of the rune talks (Anya only grants permission once Ariana has sent you), and
the y/n / number answers per word raised from 8 to 16 (Wilbur's horse sale asks each member now
that gold works). A full run (castle, bridge, Britain, cave, orb, shrine) takes about 230 s with
no issues; the guardians leave the Avatar near death, so keeping the party alive is next.

## 6d. Shopping and staying alive (2026-10-31)

- **shop**: buy 4 torches from Efram (22) — which of you: 1, how many: 4 — and check the gold
  (−12) and the pack (+4); then ask for more than the purse covers: he sells what it pays for
  ("That's all you can afford").
- **Staying alive**: after every fight and at each segment start the bot C)asts Heal while a
  member is below 60% (it lands on the most wounded); a walk under fire from foes it cannot reach
  (cyclopes throwing boulders) heals and goes on, and gives up only when no heal works (`Danger`,
  which ends exploring so the cave segment climbs out). R)est is tried after a fight when a
  member is below half (wilderness only; it walks to a spot with the 3×3 open when "Not enough
  room!"). Walking up to someone to talk fights on the way.
- The Avatar's death and a companion's death are noticed on every report (dumps included) and
  recorded as issues.
- Report fields added: `gold`, `rest` (the camp prompt).

Found and fixed (game): plural/singular markers printed literally ("4 Torch\s"; usecode_vm.md
§6e); Detect Magic could not be aimed from the C)ast menu and Heal / Great Heal used placeholder
amounts (magic_skills.md §5h).

Bot-side: the sweep answered "y" after bye and bought Wilbur's horse and a night at the inn (now
"n" unless asked); scancode 8 sent as "08" (Hatari reads octal: now hex); the death checks missed
walking keys and dumps; `use_pack` counted rows and missed after a long message (now steps until
the prompt names the item).

## 6e. A healer (2026-10-31)

`healer` (after `shop`, at 13:30 while Tiberius, 26, is in his shop: the F2 dump's `wt` is 0x90)
asks him to resurrect with nobody dead and to heal: a wounded member must come back to full for 30
gold, or free when the purse is short, and with nobody hurt he must say all are well.

Found: every healer opened with "I see that carries a parted friend" and asked 400 gold, because
OBJINPARTY was a stub that answered "actor 0 has it"; RESURRECT raised the carrier instead of the
body it carries; and per-member gold counts saw the pooled purse once for each member ("392 short"
with 2 gold in a party of four). All three in usecode_vm.md §6f; the HEALSYNC lines of the
INVSYNC.BIN hook check a carried body and a lost one rising.

`sell` (after `healer`; waits with space until Max, 18, is at work at 14:00) sells Max a weapon:
the offered item must leave what the party carries (pack or readied, the F3 `@E` rows) and the gold
rise by the offered price. Found: WOUNDED read the dead flag, so Tiberius called a hurt party well
(§6f); readied things were invisible to scripts, so Max said nobody had anything he needed; and a
DECL into a data-block element was dropped, so his "which one?" list showed every line as the first
item (usecode_vm.md §6g). Run r26: 0 issues, 302 s, a Sword sold for 25 gold.

## 6f. A companion dies (2026-10-31)

`death` (after `sell`, before `healer`; the order is now shop, sell, death, healer) has the Avatar
walk up to Shamino (3) and A)ttack him until he is hurt, then Iolo (4) until "Iolo killed!". It checks
that a struck companion never turns hostile, that Iolo leaves the map but stays in the roster at
0 HP, that his body (339, quality 4) lies where he fell, that karma drops by 5 (the `@S` report now
carries `karma`), and that G)et picks the body up. `healer` then takes it to Tiberius: carrying a
body, he asks 400 gold at once, and with too little he must say how short, keep the body and leave
Iolo dead; with 400 Iolo must stand up on the map. Heal is asked for the first living wounded
member. `Player.meant` lists companions killed on purpose, so the watcher doesn't report them.

Found while building it (combat.md §3d, usecode_vm.md §6f):
- a companion killed by the Avatar's own blow, a field or poison was taken off the map at once, so
  it left no body (party_deaths never saw it);
- striking a companion turned it hostile and could award experience;
- a companion's death cost no karma (the reference: 5, "allowing your party to die is bad karma");
- HEAL cleared the dead flag, so Lord British's "heal", or a healer told to heal the dead member,
  raised it for free.
The crosshair now names a party member by its name ("Iolo."), not its type ("musician.").
After the death, the cave segment's 8-hour camp healed dead Iolo back to 51 HP and he stood up
again (rest.md §4); a yellow potion could do the same (usecode_use.md). Both now leave the dead
alone, and the rest helper reports any member who rises from 0 HP. Run r30: 0 issues, 301 s.

Still not run: a paid resurrection, since the party has 27 gold against Tiberius' 400.

## 7. Next

Segments still to write: the other seven shrines (their runes come from quests and NPCs), ships, the gargoyle lands and
the ending; a paid heal and a paid resurrection (the party needs 400 gold first), more spells.
