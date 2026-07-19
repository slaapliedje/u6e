# Format: Conversation container (CONVERSE.A / CONVERSE.B)

Games: U6 / Savage / Martian — verify per game (U6 decoded here)
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
> - Last updated : 2026-06-23

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
Verify — Savage/Martian likely share the container; opcode set may differ.

## 5. Validation method
`make convspike` runs NPC 2 (Dupre) end-to-end with scripted keywords and
asserts the real responses; `build/convspike <CONVERSE.A> <npc> -i` runs any
NPC interactively.

## 6. Open questions
- [ ] The `0xF2` expression encoding and the full opcode set (flags, branches,
      arithmetic, item/gold ops, asking for input strings/numbers).
- [ ] `$G` and any other substitution tokens; the `0xB0`/arg semantics.
- [ ] Confirm CONVERSE.B + per-game (Savage/Martian) opcode deltas.

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
description** per actor in the core pack's optional **`CTLK`** section
`[4 "CTLK"][u16 nact=256][u16 blob_len][256 × u16 off][blob of "name\0desc\0"]`,
`off[a]=0xFFFF` when actor `a` has no conversation. The engine (`gamedemo.c::
se_talk`) shows the NPC's portrait (CFAC) + name + look description on talk. The
**usecode script after `0xF2` is NOT yet wired** — the SE/MD conversation VM
(branching dialogue, keywords, JOIN/give) is a later milestone; the opcode set
likely differs from U6's (§4). This section documents identity only.

## 7. Status
VERIFIED — container + readable layer + the full usecode VM
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
