# Format: Conversation container (CONVERSE.A / CONVERSE.B)

Games: U6 (CONVERSE.A/.B) / Savage (TALK.LZC, §6b–6c) / both: page waits §6d / Martian (not yet read)
Implemented by: /dataformats (u6conv.c/.h, u6lzw.c), /tools (convspike)
Tracked in: /docs/INDEX.md  ·  Milestone: M4 (spike) / M6 (full VM)

> Provenance
> - Derived from : reading the user's own DOS U6 file CONVERSE.A — decompressing
>                  the first entry and reading the byte stream of Dupre (NPC 2)
>                  and several other NPCs (Shamino, Iolo, Lord British, ...).
> - References   : community knowledge that Nuvie/U6Edit decode these (read for
>                  orientation; the byte meanings below were derived from our own
>                  decompressed scripts, not transcribed).
> - Tools        : our u6lzw decoder + /tools/convspike.
> - Validated by  : convspike runs Dupre's conversation end-to-end (greeting +
>                  name/job/quest/leave/default/bye) producing the real game
>                  text; name+greeting+responses parse for many other NPCs.
> - Confidence   : confirmed (container + readable layer); the usecode
>                  EXPRESSION/flag/branch opcodes are NOT yet decoded (probable).
> - Last updated : 2026-11-07 (Savage Empire's driver, §6c)

## 1. Container layout
CONVERSE.A is a **library** (see library.md) of per-NPC conversation scripts:
- An index of **4-byte little-endian, file-relative offsets**, one per NPC
  number. Offset 0 = that NPC has no conversation in this file.
- Index size = the first non-zero offset (U6 CONVERSE.A: 396 → 99 entries).
- Each entry is **independently LZW-compressed** (lzw.md): a 4-byte LE
  uncompressed-size header then the bit stream. (NPC 2's entry: 852 → 1238 B.)
- CONVERSE.A holds the low NPC numbers, CONVERSE.B the rest.

So: read offset[npc] → entry bytes → u6lzw_decompress → conversation script.

## 2. Conversation script (decompressed) — readable layer
A script is text interleaved with single-byte control opcodes. Observed:

| Opcode | Meaning                                                           |
|--------|------------------------------------------------------------------|
| `0xFF` | start of conversation; **next byte = NPC number**                |
| (text) | NPC **name** (bare ASCII) follows the NPC number                  |
| `0xF1` | end name → **look description** (bare text) follows               |
| `0xF2` | → **greeting**: a usecode EXPRESSION (high bytes) then quoted text|
| `0xF7` | begin the **keyword input loop**                                  |
| `0xEF` | **keyword section**: keyword list (bare ASCII) then `0xF6`        |
| `0xF6` | end keyword list → **response** (quoted text) follows             |
| `0xB0` | end of a response (+4 arg bytes), return to the input prompt      |
| `0xB6` | **end conversation** (follows the "bye" response)                 |
| `0xEE` | end of conversation data (follows the `*` default section)        |
| `0x0A` | line break within text                                            |

Text conventions:
- **All spoken text is double-quoted**; the look description is bare. (This is
  what lets the readable layer be parsed without an expression evaluator.)
- `$P` = player name substitution; `$G` = a title/gender token (not yet handled).
- `@word` = a highlighted keyword the player can ask about (strip the `@`).
- Keywords are matched on the **first 4 characters**, case-insensitive; a
  section may list several comma-separated keywords; `*` is the catch-all.
- `bye` ends the conversation.

## 3. Usecode opcode set (DECODED — executed by engine/converse_vm.c)
The conversation is bytecode. Bytes are classified: **print** (0x0a, 0x20–0x7a,
0x7b, 0x7e) = display text; **data-push** 0xD3/0xD2/0xD4 = next 1/4/2 LE bytes
are a literal; **valop** = an operator/query used inside expressions; **ctrl** =
a statement opcode (high byte, not a valop/data-push). `0xA7` (EVAL) reduces the
collected value-expression. Execution model: see /docs/rules/usecode_vm.md.

Value operators / queries (RPN, via EVAL):
| Op | Meaning | | Op | Meaning |
|----|---------|-|----|---------|
| 0x81–0x86 | GT GE LT LE NE EQ | | 0xa0 | RAND(min,max) |
| 0x90–0x95 | ADD SUB MUL DIV LOR LAND | | 0xab | FLAG(npc,bit) |
| 0x9a/9b/9d/9f | CANCARRY/WEIGHT/HORSED/HASOBJ | | 0xb2/b3 | VAR/SVAR |
| 0xc6/c7 | INPARTY/OBJINPARTY | | 0xb4/b7/bb | DATA/INDEXOF/OBJCOUNT |
| 0xca/cc | JOIN/LEAVE | | 0xe0–0xe4 | EXP/LVL/STR/INT/DEX |

Control statements:
| Op | Meaning | | Op | Meaning |
|----|---------|-|----|---------|
| 0xa1/a2/a3 | IF / ENDIF / ELSE | | 0xb0 | JUMP(4-byte loc) |
| 0xa4/a5 | SETF / CLEARF (npc,flag) | | 0xb5 | DPRINT(db,idx) |
| 0xa6/a8 | DECL / ASSIGN (input var) | | 0xb6 | BYE (end) |
| 0xc9 | GIVE | 0xca…| 0xc4/c5 | ADDKARMA/SUBKARMA |
| 0xee/ef | ENDANSWER / KEYWORDS | | 0xf1/f2/f3 | SLOOK/SCONVERSE/SPREFIX |
| 0xf6/f7/f8 | ANSWER / ASK / ASKC | | 0xf9/fb/fc | INPUTSTR/INPUT/INPUTNUM |
| 0xb8 | ENDDATA | | 0xff | SIDENT (+npc, name) |

Text substitutions in display runs: `$P` player, `$G` gender title, `$N` NPC
name, `#n` value of var n (`$n` = string var). `0x0a` = line break. The earlier
"readable layer" (u6conv.c) text-scans the same scripts WITHOUT executing the
opcodes; it remains for bulk transcript coverage. The VM is the real executor.

## 3b. Transcript coverage (M6)
`make convtest` decompresses + parses EVERY conversation in CONVERSE.A and .B
(200 total, incl. the one STORED entry), drives name/job/bye, and golden-checks
known NPCs. Result: 200/200 parse + named, 197 with keyword sections, all golden
texts correct. Conditional-gated NPCs show their content via the flow layer
(branch selection awaits the expression VM).

## 4. Per-game deltas
Savage Empire: its own container (TALK.LZC, §6b) and a few opcode differences (§6c). Martian Dreams:
not yet read.

## 5. Validation method
`make convspike` runs NPC 2 (Dupre) end-to-end with scripted keywords and
asserts the real responses; `build/convspike <CONVERSE.A> <npc> -i` runs any
NPC interactively.

## 6. Open questions
- [ ] The `0xF2` expression encoding and the full opcode set (flags, branches,
      arithmetic, item/gold ops, asking for input strings/numbers).
- [ ] `$G` and any other substitution tokens; the `0xB0`/arg semantics.
- [x] Savage Empire opcode deltas (§6c). [ ] Martian Dreams.

## 6b. Savage / Martian: `TALK.LZC` (per-actor conversations)

SE/MD do **not** use CONVERSE.A/.B. Conversations live in **`TALK.LZC`** — an
[.LZC](lzc.md) multi-item container (SE 77 slots / 68 present, LZW items). Each
present item is ONE actor's conversation and **self-describes its actor number**:

| Offset | Field | Notes |
|--------|-------|-------|
| 0      | `0xFF` | item marker |
| 1      | `u8 actor` | the OBJLIST actor number this conversation belongs to |
| 2..    | name | printable ASCII (e.g. `Jimmy`) |
| —      | `0xF1` | name / description separator |
| ..     | description | the "look" text (`a lanky, sharp-eyed man in his mid-20s.`) |
| —      | `0x0A 0x2A` | end of the plain header |
| —      | `0xF2` … | the **usecode** conversation script (opcodes, keyword loop) |

VERIFIED (2026-07-19): byte 1 gives the actor directly — TALK item0 `actor=2`
(Aiela), item7 `actor=9` (Triolo), item2 `actor=4` (Jimmy) — matching the captured
objlist and the FACES map `face(actor)=slot actor−1` (portraits.md). Companions
occupy actors 2–11 (TALK items 0–9); general NPCs follow, actor-numbered so their
TALK items land alphabetically (Aloron…Darden…).

Host pipeline: `tools/mkworld.c::convert_talk` ships the plain **name + look
description** per actor in the world pack's **`CTLK`** section (after everything else; SE/MD only)

`[4 "CTLK"][u16 nact=256][u16 blob_len][256 × u16 off][blob of "name\0desc\0"]`,
`off[a]=0xFFFF` when actor `a` has no conversation. Beside it, **`CTSC`** carries
the whole scripts, repackaged without change so the conversation VM runs them:

`[4 "CTSC"][u16 nact=256][u32 blob_len][256 × u32 off][blob]`, all big-endian;
`off[a]=0xFFFFFFFF` when actor `a` has none. At `off[a]`: `[u8 kind][u32 len][len bytes]`,
kind 1 = the TALK item's own [lzw.md] container (SE), kind 0 = stored bytes.

## 6c. Savage Empire's talk driver (GAME.EXE)

> Provenance
> - Derived from : reading the user's own DOS Savage Empire GAME.EXE (plain Turbo C image,
>                  header 0x3400; disassembled in the RE workspace, outside this repo): the
>                  statement switch (ops 0x9C–0xDF, a jump table), the value-op switch (41
>                  cases), the event routine behind 0xD1, the text-step loop, the character
>                  output routine and the talk entry; plus a byte census of all 68 TALK items.
> - Validated by : every SE script parses with no unknown statement; the census below counts
>                  every statement and value op the scripts use. Behaviour in play: NOT YET
>                  compared against DOS running.
> - Confidence   : probable (read from the code; not yet watched in DOSBox)
> - Last updated : 2026-11-07

SE's interpreter is U6's: the same byte classes, data pushes (0xD2/D3/D4), EVAL (0xA7), frames
(IF/ELSE/ENDIF, KEYWORDS/ANSWER), keyword loop, data block, string variables and text tokens.
What differs:

| Op | U6 | SE |
|----|----|----|
| 0xA4 SETF / 0xA5 CLEARF / 0xAB FLAG | flags 0–7 | flags **0–15**: 0–7 in the talk-flag byte (objlist 0x18F1 in SE), 8–15 in a second per-actor byte (objlist **0x12F1**); a flag above 15 is ignored, FLAG reads 0 |
| 0xC4 ADDKARMA / 0xC5 SUBKARMA | karma | **not in SE**: "Unknown command." (no SE script uses them) |
| 0xD1 EVENT(n) | — | runs scripted event *n* (table below) |
| 0xDF (n) | — | $Y := "you" if n is 1, else actor n's name (no SE script uses it) |
| 0xD0 (n) | — | steps the game clock n × 20 times (no SE script uses it) |
| 0xC8 (npc, x) | — | an actor routine with (npc, x, 16) (no SE script uses it; not read further) |
| 0xCB WAIT | waits for a key | the same |
| 0xCD WORKTYPE(npc, w) | sets the worktype | sets it; w = 0x91 also puts the actor to sleep |
| value ops 0xBC 0xBD 0xC1 0xC2 0xC3 | — | present in SE's value switch; no SE script uses them |
| 0x9B WEIGHT(obj, qty) | | weight × qty, × 10 more unless the object is weighed per ten (the same in U6) |

Text: a byte 0x20–0x7F or 0x0A prints; any other byte below 0x80 in a text run is an error
("Try to print out a control char"). `$x` reads string variable x (0–9, then A–Z = 10–35), so `$P`,
`$N`, `$Y`, `$Z` are variables 25, 23, 34, 35 as in U6; `#`, `/`, `\`, `@`, `^` take the next
character, `*` waits for a key, `` ` `` capitalises the next letter. SE's font draws `&` as the
closing quote mark: scripts open speech with `"` and close it with `&`.

**EVENT (0xD1)**, by number (all SE uses):

| n | Used by | What happens |
|---|---------|--------------|
| 1, 3, 4 | "Guards! Kill this man!" (Huitlapacti, Kipotli, the Nahuatla queen) | warriors (obj 320, "man") come for the party: count = random 2–10; for each try, pick a spot on the edge of the box round the party (x ± 5 by a random side at y ± 4, or the other way round), and where a man may stand create one there, hostile (alignment 3, chaotic), worktype 6. A try that places one counts twice |
| 2, 5 | the Myrmidex queen and soldiers | the same with Myrmidex drones (obj 323), count random 5–10, alignment 1 (hostile) — unless actor 40's (Moctapotl's) talk flag 7 is set |
| 10 | Intanya, when the Avatar fails his questions on waking | the game ends: DOS shuts down and returns to DOS |
| 149 | (none) | adds 1 to a game counter |
| 150 | the drum-maker: "you can beat it now" | a jungle drum (obj 148, frame 3) appears at (533, 453, level 0), beside Tuomaxx (actor 52) |
| 175 | Fabozz fades away | actor 25 (Fabozz) moves to (997, 339, level 0); actor 179 gets worktype 0 and alignment 0 |
| 222 | Yunapotli, "There! It is done." | the door (obj 161) at (146, 137) on the party's level opens (the data has it on level 3) |

Other numbers do nothing.

**Who answers** (the talk entry, before the script runs): a party member who isn't near prints
`No response.`; so do the dead, the asleep, the paralysed and an actor whose worktype is 0x9B,
0x13 or 0x07; talking to yourself prints `Talking to yourself?`. In solo mode the game prints
`Not in solo mode!`, except to actor 25 (Fabozz).

Census (68 scripts): statements a1 616, a2 619, a3 343, a4 208, a5 18, a6/a8 121, b0 1780, b6 191,
b9 23, ba 15, bf 265, c9 2, cb 987, cd 28, d1 18, d6 3, d8 12, d9 3, db 2, ee 55, ef/f6 1185, f1/f2/ff
68, f3 8, f7 83, f8 59, fb 11, fc 2. Value ops: only ones U6 has.

## 6d. Page waits (U6 and SE GAME.EXE)
Read from the text routine both games share. In U6 GAME.EXE 4.5: the token switch at image 0x7a13
(table at 0x7a54, cs base 0x4640), the character routine at 0x753a, the key wait at 0x7099, the
cursor at 0x54ae. SE has the same code: tokens at 0x8858, `*` at 0x8891, the page loop at 0x8466.
Martian Dreams' GAME.EXE has no such loop; it is read with the rest of MD (se_md_checklist.md item 13).

- **The count.** A window that pages keeps a count of lines left. Every key read and every line read
  sets it to the window's height. Each line the routine puts out (at a line break, or where a line
  wraps) takes one off first. When that leaves 0, it waits before putting the line out.
- **The page wait.** The cursor is glyph 1 (below), on the empty row the next line will go to. Only
  Space, Enter, Esc or a click go on; other keys do nothing. The row is cleared and the count set to
  the height less one, so each later page brings a window less one of new lines.
- **`*` in text** (and the WAIT op, which puts out a `*`): the part of the line so far is drawn, the
  cursor is shown just after it, and any one key goes on. That key is a key read, so the count starts
  again at the height. The text goes on from the same place on the line.
- **The cursor.** Glyph `base + frame`, frame 0–3, one step for each pass of the wait loop: base 1
  waiting for a key, base 5 for typed input. In U6.CH glyphs 1, 3, 4 are a down arrow and 2 is
  blank (it blinks); 5–8 are an ankh turning. SAVAGE.FNT has an arrow with glyph 3 blank, and a
  skull whose eyes blink. u6e steps the frame every 33 ticks of the 200 Hz clock, the pace of its
  tile animation: DOS's pace is whatever its loop takes, so there is no number to copy.

The message scroll pages by the same rule (u6e: a page less one row, the arrow on the last row).

In u6e (render/gamedemo.c `talk_page_sync`/`talk_advance`/`talk_draw`): the panel's text area is 8
rows, the typing row counting as a line; the panel shows the newest 8 rows of what is out. The `*`s
stay in the transcript (the bot and the tests read it whole) and are not drawn.

## 7. Status
Savage Empire (2026-11-07): its scripts run on the same VM in SE's dialect (§6c), in the game and on
the host (`build/convvm TALK.LZC <actor> kw...`); `make check-se-talk` drives one in the game.

U6: VERIFIED — container + readable layer + the full usecode VM
(engine/converse_vm.c) which executes the bytecode (expression eval, IF/ELSE/
ENDIF, keyword loop, BYE) for NPCs across CONVERSE.A, on host and on the TT.
Game-mutation opcodes (SETF/CLEARF, GIVE/NEW/DELETE, ADDKARMA/SUBKARMA,
JOIN/LEAVE, HEAL/CURE/RESURRECT, PORTRAIT, WORKTYPE, WAIT) apply to the VM's
game-state model; the DATA BLOCK (DPRINT strings, DATA integers + $n string
vars) + INDEXOF (name→index lookup) + INPUT/INPUTNUM (pause + store) work —
merchants list/sell from their item tables, by number or name. Text tokens
$P/$G/$N/$n/$Y/$Z + #n resolve. System numeric vars (karma 0x14, party
0x17/0x18, sex 0x10, hp 0x19, quest 0x1A, worktype 0x20, ...) read from game
state so karma/party/quest-gated branches work. $T resolves to the time-of-day
greeting word (morning/afternoon/evening — the 3-word table in U6 GAME.EXE at file
offset 188628) by the game-clock hour. Still stubbed: the in-conversation INVENTORY UI — see
/docs/rules/usecode_vm.md §3.
