# Rules: Usecode / conversation VM

Games: U6 / Savage / Martian — default = U6; verify deltas
Implemented by: /engine (VM execution) + /dataformats (u6conv parser; decoder)
Tracked in: /docs/INDEX.md  ·  Milestone: M4 (spike) / M6 (full VM)

> Provenance
> - Derived from : our own decompressed CONVERSE.A scripts (Dupre, Iolo, Lord
>                  British, and a 2..40 sweep) — see /docs/formats/converse.md.
> - References   : Nuvie ConverseInterpret(.h/.cpp) READ to understand the opcode
>                  set + the collect/exec execution model — implemented from this
>                  spec, not transcribed. (The control bytes are the documented
>                  wall that stalled prior recreations.)
> - Tools        : /tools/convvm (drives a real NPC through engine/converse_vm).
> - Validated by  : the VM runs NPCs 2..40 without desync/crash; Dupre, Iolo,
>                  Lord British (incl. his Compendium copy-protection gate)
>                  produce the correct text + keyword branching; it executes
>                  on-target on the TT (build/game_screen.png).
> - Confidence   : confirmed (core: tokenise, expression eval, IF/ELSE/ENDIF,
>                  keyword loop, flags/vars/party/RNG); partial (game-mutation
>                  opcodes give/join/heal/karma are recognised but stubbed).
> - Last updated : 2026-06-24

## 1. System
NPC dialog is driven by a per-NPC bytecode script (usecode). The conversation
is a loop: show the look-description + a greeting, then repeatedly accept a
player **keyword** and run the matching response section until the player says
`bye` (or the script ends). Parser/decoder lives in /dataformats; execution
semantics (this VM) live in /engine. This is the HIGHEST-RISK subsystem.

## 2. Observed behavior (the readable loop — confirmed)
| Input / situation         | Result                                              |
|---------------------------|-----------------------------------------------------|
| start talking             | print look description, then the greeting           |
| keyword matches a section | print that section's response, re-prompt            |
| keyword matches several   | sections list comma-separated keywords (4-char match)|
| unknown keyword           | print the `*` (default) section response            |
| `name` / `job`            | conventionally present for every NPC                 |
| `bye`                     | print response and END the conversation             |

Spoken text is double-quoted; `$P` = player name; `@word` = a keyword hint.
(See converse.md for the opcode table.)

## 3. Execution model (IMPLEMENTED — engine/converse_vm.c)
A two-phase bytecode interpreter (mirrors the original; see converse.md for the
opcode table):

1. **collect_input** — read ONE instruction = an opcode plus its value args.
   Values are literals (data-push 0xD2/0xD3/0xD4 = 4/1/2 LE bytes) or value-
   opcodes (operators + state queries). An `0xA7` (EVAL) marker reduces the
   collected sub-expression on a value stack (**eval/evop**, RPN): operators
   (0x81–0x86 compare, 0x90–0x95 arith/logic) and queries (FLAG, VAR, INPARTY,
   RAND, …) pop operands and push results. A few opcodes have fixed layouts
   (JUMP=opcode+4-byte loc; SIDENT/KEYWORDS/SLOOK=opcode+text).
2. **do_frame + do_ctrl** — a frame stack + a `run` flag implement control flow
   WITHOUT jump targets: `IF` evaluates its condition and sets run on/off (with
   a break that `ELSE` toggles); `ENDIF` leaves the frame; the keyword loop is a
   KEYWORDS frame. Opcodes (and text) execute only while the top frame runs.

**Keyword loop**: `ASK` (0xF7) pauses for a player keyword; each `KEYWORDS`
(0xEF) section compares its list to the input (comma list, `*`=any, input-prefix
match); the matching `ANSWER` (0xF6) body runs (others skipped); `JUMP` (0xB0)
loops back to ASK; `BYE` (0xB6) ends. Text prints with $P/$G/$N/#var substitution.

**Game state** (cvm_game_t) — the VM both READS and MUTATES it:
- talk flags (SETF/CLEARF set; FLAG reads), numbered vars (DECL/ASSIGN/VAR),
  party membership (JOIN/LEAVE/INPARTY), deterministic xorshift RNG (RAND);
- karma (ADDKARMA/SUBKARMA, clamped 0..99);
- player inventory + gold (GIVE/NEW/DELETE move objects to/from the player,
  HASOBJ/OBJCOUNT read it; the gold object updates `gold`). "The player" is any actor whose
  things live in the party's pooled pack: the Avatar (actor 1), any current party member, and actor 0 (as
  before). Until 2026-10-31 only actor 0 counted, so a script that gave to actor 1 by
  number (Ariana's rune: GIVE(rune, 0, self, 1)) printed the hand-over and gave nothing (found by
  the bot harness). The QUALITY argument of GIVE / NEW goes onto the
  item put in the pack (Lord British's castle key is NEW(Avatar, key, quality 1, 1): the key's
  quality is the lock it opens); a stack only merges with one of the same quality. Objects are
  counted by their full number 0-1023 (until 2026-10-31 by the low byte, so the quest items
  394-417 that seven scripts give arrived as other objects). **Gold is object 88**, counted in the
  party's gold total: OBJCOUNT / HASOBJ of 88 read it and GIVE / NEW / DELETE of 88 change it (the
  gold object number was wrong before 2026-10-31, so every merchant answered "you don't have
  enough gold" whatever the party carried);
- per-NPC poison/dead status (HEAL/CURE clear; RESURRECT raises the actor a carried body names, §6f; WOUNDED/POISONED read);
- NPC schedule worktype (WORKTYPE); the active PORTRAIT (host re-renders).
A `mutations` counter records state-changing ops (used by tests). Defaults make
every conditional take its "flag clear / not in party" branch; the host presets
state to drive branches.

These act on cvm_game_t — the VM's self-contained world view. Wiring it to the
broader engine's real party/inventory once those exist is a later integration
step; the conversation semantics themselves are complete.

**Coroutine API**: `cvm_run()` executes until CVM_ASK (needs a keyword) or
CVM_DONE; the caller shows `cvm_text()`, calls `cvm_input(keyword)`, repeats.
Portable C — the SAME VM runs on the host (tests) and the 68000.

### Data block + input (IMPLEMENTED)
- A conversation can carry a **data block** (a region of the script at a byte
  offset, e.g. a shop's item table): a run of non-print-terminated **strings**
  (indexed 0,1,2,...) and/or 2-byte LE **integers** (at loc + i*2).
  - **DPRINT** (0xb5) prints the i-th data-block string.
  - **DATA** (0xb4) reads it: integer (→ value) or string (→ a `$n` string var,
    `svar[]`, separate from the numeric `var[]`).
  - **INDEXOF** (0xb7) — find the data-block item whose name (clamped to 4 chars,
    keyword-style) matches a string var; returns its index (or the count if not
    found). Lets the player pick a shop item / topic by NAME. Unit-tested
    (`make cvmtest`: Bow→0, Crossbow→1, Sling→2, "Sword"→count).
- **INPUT / INPUTSTR / INPUTNUM** (0xfb/f9/fc) PAUSE for input like ASK (return
  CVM_ASK); `cvm_input()` is parsed into the declared var (number → `var[]`,
  string → `svar[]`) and always into `$Z` (svar 0x23, the previous input — what
  INDEXOF searches). Verified: merchants list their wares from the data block,
  take an item number, and quote the DATA-integer price (Gwenneth's bow shop:
  "1) Bow 2) Crossbow ..." → pick 1 → "30 gold").
- Text tokens resolved: `$P` player, `$G` gender title, `$N` NPC name, `$n`
  string var, `$Y` Y-string (svar 0x22), `$Z` previous input (svar 0x23),
  `#n` numeric var.

### System numeric vars (IMPLEMENTED)
The script reads game state through fixed VAR indices (read-only). Resolved from
cvm_game_t so karma/party/quest-gated conversations branch correctly:
`0x10` sex, `0x14` karma, `0x15` knows-Gargish, `0x17` party-living, `0x18`
party-total, `0x19` avatar hp, `0x1A` on-quest, `0x20` this NPC's worktype.
(Indices ≥0x10 are system; lower indices stay ordinary local vars. Unit-tested
in `make cvmtest`.) The host presets cvm_game_t (real party/karma/...) before a
conversation; with the zeroed defaults these match the previous behaviour.

### Text token $T (time-of-day greeting) — DONE
`$T` resolves to the greeting word by the game-clock hour: `<12` → "morning",
`<18` → "afternoon", else "evening" (the 3-word table confirmed in U6 GAME.EXE;
the hour bands are the standard U6 division). The host sets `cvm_game_t.hour`
before each conversation. Unit-tested across all three bands in `make cvmtest`.

### Still stubbed (recognised so the stream never desyncs)
- INVENTORY (UI), WAIT (emits a `*` page-break).
- The system STRING vars NPC_NAME(0x17)/PLAYER_NAME(0x19) are reached in text via
  `$N`/`$P` (which work); the SVAR-op form isn't separately wired.
- Per-game (SE/MD) opcode deltas: behind a RuleSet hook later.

## 4. Determinism / RNG
Some responses may branch on RNG or game state; the VM must use the engine's
deterministic RNG seam so golden-transcript tests are reproducible.

## 5. Test hooks
- `make convvm` drives a real NPC (default Lord British) through the VM.
- `build/convvm CONVERSE.A <npc> [kw...]` runs any NPC with scripted keywords.
- `make cvmtest` unit-tests the data-block INDEXOF (name→index) lookup.
- `make convtest` still validates the readable layer across all 200 NPCs.
- On-target: `make m5` runs the VM on the TT (greeting + keyword response).

## 6. Open questions
- [x] Conversation STATE PERSISTS (2026-09-30): the per-NPC talk flags (objlist 0x17f1+npc),
      the quest flag (0x1bf1) and knows-Gargish (0x1c5f) are read from the save template at boot
      and by an in-game load, and written by S)ave (engine/save.c sav_set/get_talk_state;
      LOAD.REQ '4' round-trips them). Numbered vars are script locals and stay unsaved.
- [x] HEAL/CURE/RESURRECT wired (2026-09-30): before each conversation turn the party's real
      health goes into the VM (dead = 0 HP, poisoned; WOUNDED/POISONED read it); after the turn a
      cured member loses the poison, a resurrected one stands at 1 HP, a HEALed one is at full HP
      (`healed_full` records the op). INVSYNC.BIN prints the HEALSYNC round trip.
- [x] WORKTYPE acts on the LIVE NPC (2026-10-26) — §6b.
- [x] NPCNEARBY (0xd7) answers from the live world (2026-10-27): 1 when the asked NPC stands
      within 5 tiles of the speaker on the same level (the reference's is_nearby threshold); the
      speaker's neighbourhood is pushed into the VM before each conversation (Culham's brawl now
      reaches the two guards it checks for).
- [ ] DATA/DPRINT data-block strings (some answers reference indexed strings).
- [ ] Per-game opcode deltas (Savage/Martian) behind a RuleSet hook.
- [ ] Interactive keyword TYPING on-target (line input); INPUT/INPUTNUM vars.

## 6b. WORKTYPE → the live NPC (2026-10-26)

> Derived from: READING Nuvie `core/converse_interpret.cpp` (U6OP_WORKTYPE 0xcd takes npc +
> worktype and calls the actor's set_worktype), `actors/actor.cpp` set_worktype / updateSchedule
> (a worktype holds until the actor's schedule moves to a NEW entry, which re-applies the
> scheduled code; set_worktype anchors the actor's work location at its current tile),
> `actors/u6_actor.cpp` will_not_talk (combat-retreat / attack-party worktypes refuse to talk),
> `core/events.cpp` talk ("No response"), `actors/actor_manager.cpp` (objlist 0x11f1 = each
> actor's current worktype, read on load / written on save), and a scan of the user's own
> CONVERSE.A/.B for the opcode (which NPCs, which codes, the surrounding text). No code
> transcribed. Nuvie names 0x99 "Brawl" and 0x9a "Mousing" in a debug string only and gives
> them no behaviour; the script texts define them for us (probable).

- The opcode takes (npc, worktype). The real scripts use it 20 times: 0x99 on the speaker with
  "...the gargoyle attacks!" / "...must die!" / "leaps at you, screaming" texts and Culham on five
  other NPCs (a brawl), 0x9a with "...and flees!" / "must flee!", 0x91 with "faints dead away!".
  One script passes both operands from script variables.
- Effect: the NPC's worktype is FORCED to the code, anchored at the tile where it stands, until
  its schedule changes to another entry (the original's rule); then the scheduled code and place
  take over again and whatever hostility the force caused is dropped. Codes:
  - 0x99 brawl/attack, 0x9b attack-party, 0x08 assault, 0x03-0x06 combat stances: the NPC turns
    on the party (the assault AI, alert at once);
  - 0x9a flee, 0x07 combat-retreat: the NPC flees the party on sight (the coward AI);
  - 0x91 sleep (a faint), 0x90 work, 0x8f wander, 0x0e immobile, 0x00 motionless: the NPC stays
    at / wanders around the tile where it stands; a hostile NPC given one of these calms down;
  - 0x87-0x8a: face N/E/S/W;
  - 0x01 in-party / 0x02 player: no effect (JOIN/LEAVE carry the party).
- T)alk to an NPC under a combat or flee code: "No response." (the original refuses too).
- The VM sees the live codes: before each conversation turn every NPC's current worktype (the
  forced one, else the schedule's; party members 0x01, the Avatar 0x02) goes into the VM's view —
  var 0x20 reads it; after the turn every entry the script changed is applied as above.
- Persistence: S)ave writes each scheduled or forced NPC's current worktype into objlist 0x11f1
  (the original's meaning of the byte); a load re-applies a saved combat / flee code (0x03-0x08,
  0x99, 0x9a, 0x9b) that differs from the schedule's current one. Other saved codes come back
  from the schedule (a saved faint is not restored — v1 simplification).
- Test hook `WORKTYPE.REQ`: '1' forces the codes on the nearest scheduled NPC through the VM
  path and watches it approach/strike (0x99), flee (0x9a), stand (0x91) and resume its schedule
  at the next entry; '2' the real scripts (NPCs 170 / 171 / 185 greet — prints the force the
  script set, if its condition held); '3' save → the objlist byte, load → the force is back.
- Status: VERIFIED 2026-10-26 — WORKTYPE.REQ on emulated TT and the stock 4 MB Falcon (identical):
  '1' NPC 6 (3 tiles away) forced 0x99 turns hostile and closes to 1 tile, 0x9a makes it flee back to
  3, 0x91 keeps it on its tile, the next schedule entry clears the force and restores its calm;
  '2' the real greetings set 0x91 on the Lensmaker (170, "faints dead away!") and 0x9a on the Snake
  Charmer (171, "and flees!"); '3' "No response." while hostile, objlist byte 0x99 after S)ave, the
  force and hostility back after load. Host: `make wtrun NPC=49 KEYWORDS="name job gyps"` runs
  Culham's real bytecode — the gypsy story ends in WORKTYPE 0x99 on NPCs 45/49/50 (44/46 are behind
  the NPCNEARBY query, still stubbed to 0).

## 6c. Party stats, levels and experience in scripts; talking to objects (2026-10-28)

> Provenance: READING ScummVM/Nuvie `conversation/converse_interpret.cpp` (U6OP_NPC 0xdd, U6OP_EXP 0xe0,
> U6OP_LVL 0xe1, U6OP_STR 0xe2, U6OP_INT 0xe3, U6OP_DEX 0xe4), `actors/actor.h` set_level / set_exp,
> `actors/u6_actor.h` get_maxhp, `core/events.cpp` talk(Obj) — to understand; CONFIRMED by a scan of
> the user's own scripts (the shrine entries 192-199). Re-expressed, no code transcribed.

- **0xdd NPC takes TWO args**: `(party_index, ignored)` and yields the ACTOR NUMBER of that party
  member (index 0 = the Avatar). We used to pop one arg and treat it as an actor number — every
  script that picked "Who will …?" from a numbered party list was off.
- **0xe0 EXP, 0xe1 LVL, 0xe2 STR, 0xe3 INT, 0xe4 DEX take TWO args** `(npc, delta)`: the result is
  the actor's current value PLUS delta, which is also STORED (so delta 0 reads, delta ±n changes).
  Exp is clamped to 9999, the others to a byte. We used to pop one arg and answer 1 / 10 (stubs).
- The VM sees every actor's str / dex / int / level / exp (`cvm_game_t` stats, synced in before a
  conversation) and the party ORDER (`party_actor[]`); after each VM run the host applies what
  changed (`sync_stats_out`): a new level sets max HP = level × 30 (HP itself untouched, as the
  reference), stats and exp are copied back, and the status line's "Lv" follows.
- Found and fixed while driving the shrine script (2026-10-28): **0xf8 ASKC** now PAUSES for one of its
  listed characters like ASK (it used to be skipped, so a "y/n" prompt fell straight through);
  as in the reference it is a **single keypress**: only a listed character is taken, at once, without
  Enter, and other keys are ignored; an **INPUTNUM** prompt takes digits only (2026-10-30: found by
  the bot harness — a typed word at Lazeena's "Wouldst thou like to hear the news?" ended her talk);
  **0xd8 SETNAME** sets `$Y` to the named actor's name (a party member's from the roster, the speaker's
  from its script; it used to be ignored, printing empty names in "Who will meditate?" lists); the
  system vars **PARTYLIVE (0x17) and PARTYALL (0x18) are the party size MINUS the leader**, as the
  reference sets them (we counted everyone, so party lists ran one entry long).
- **After the conversation ends** the portrait and the last reply stay in the panel until the next
  key; that key closes them and then still acts (U)se, a command letter, a pass...). Until
  2026-10-31 only a walking step closed them, so the pack, the "Where:" prompt and messages after
  a talk stayed hidden behind the portrait (found by the bot harness).
- **0x9a CANCARRY and 0x9b WEIGHT** (2026-10-31; READING Nuvie `core/converse_interpret.cpp`
  U6OP_CANCARRY / U6OP_WEIGHT, `core/obj_manager.cpp` get_obj_weight / has_reduced_weight,
  `actors/actor.h` inventory_get_max_weight — to understand; re-expressed). Weights are in the
  TILEFLAG weight table's raw unit (a tenth of a stone):
  - `WEIGHT(obj, qty)` takes TWO args: the type's raw weight × qty; for gold (88) and the eight
    reagents (65-72) divided by 10 (they weigh a hundredth of a stone each), whole-number result.
  - `CANCARRY(actor)` (the actor number as given, no party-index mapping): the room left, in the
    same unit = (strength × 2 stones − the weight carried) × 10.
  - The scripts compare them (`CANCARRY(npc) < WEIGHT(obj, n)` → "carrying too much"). Ariana's
    rune hand-over is one (found by the bot harness: WEIGHT popped one arg, so the comparison
    read the wrong stack slots and she always refused).
  - u6e pools the party's pack, so for a party member CANCARRY counts the WHOLE party: the sum of
    every member's strength × 2 stones, minus the pooled pack (each stack's weight as WEIGHT
    computes it) and everything readied by any member, never below 0 (deviations.md). For an NPC
    outside the party: strength × 2 stones (their own possessions are not weighed).
- **Talking to an object**: T)alk at a tile with no actor but a **shrine (393)** or one of the three
  statues (Mondain 397, Minax 398, Exodus 399) starts the conversation whose number is the object's
  **quality** (the shrines 192-199, the statues 189-191; the Codex altar's 200 exists too), exactly the
  reference's talk(Obj). Such a script has no portrait; its name comes from the script.
- Data: the eight shrines in the user's save carry qualities 192-199 (Honesty 192 at (935,263),
  Compassion 193 (503,359), Valor 194 (159,943), Justice 195 (295,39), Sacrifice 196 (831,167), Honor
  197 (327,823), Spirituality 198 (23,23) level 1, Humility 199 (919,935)); the shrine script asks to
  meditate, lists the party ("Who will meditate?"), asks the mantra, and then levels the chosen member
  if exp suffices (combat.md §3).
- Hook: SHRINE.REQ '1' kill awards, '2' the Honesty shrine conversation driven headlessly (meditate →
  member → mantra: the Avatar at level 3 / exp 400 gains level 4, max HP 120, int +3; a second visit
  at exp 400 says to return later), '3' resurrection exp loss.

Status: VERIFIED 2026-10-28 — SHRINE.REQ 1-3 identical on emulated TT and the stock 4 MB Falcon. '2': T)alk
north at the Shrine of Honesty (935,263) opens entry 192 without a portrait; "y" → "1. Avatar 2. Dupre
3. Shamino 4. Iolo / Who will meditate?"; "1" → "Speak the Mantra of Honesty."; "ahm" → the meditation,
"Avatar has gained a level... and intelligence!": level 3 → 4, max HP 90 → 120 (HP stays 90), int 20 → 23,
exp 400 untouched, companions untouched, 4 VM mutations; the same visit again at exp 400: no gain ("Return
when thy journey has progressed further."); at level 7 / exp 6400 → level 8, max HP 240; at 8 nothing more.
The script's closing lines ("There is naught else…", "Return when…") follow the level-up too — that is the
bytecode's fall-through, not a VM fault. The host runner replays the same script with identical results
(scratchpad vmrun). '1': a gargoyle whose live strength and HP were cut to 1 still pays 26; a deer 0; a
giant rat 9; a daemon 55; the Avatar's level unchanged. '3': Lord British slain with exp 400, resurrected
at karma 50 → exp 200.

## 6d. The backtick in script text (2026-10-30)

A backtick (\`) appears 20 times in the U6 scripts, always right before a `$` substitution
(`$Z`, `$G`, `$0`), several at the start of a sentence (`"\`$G, ...`). We print the substituted
word with its first letter capitalised and drop the backtick (Fyodor: "Pleased to meet thee, Jim."
for a typed "jim"; a sentence opening with the title reads "Milord, ..."). Confidence: **probable**
— inferred from where it occurs; not yet compared with the original's screen. Found by the bot
harness (docs/design/bot_harness.md): the backtick was printed literally.

## 6e. Plural and singular markers (2026-10-31)

> Provenance: READING Nuvie `core/converse_interpret.cpp` get_formatted_text (the `/` and `\`
> cases) — to understand; re-expressed. Found by the bot harness's shop checks: "4 Torch\s".

In a printed text (script text, a data-block string printed by DPRINT, and what `$` tokens
bring in), a backslash `\` followed by a run of lowercase letters shows those letters only when
the number most recently printed by a `#n` variable in the same text is NOT 1; a slash `/`
followed by lowercase letters shows them only when that number IS 1. The marker itself is never
printed. Before any `#n` in the text the number counts as 0 (plural). So "#3 Torch\s" reads
"4 Torchs" for 4 and "1 Torch" for 1 (the original's own wording). (The reference repeats the
first letter of the run instead of copying it, which only matters for runs longer than one
letter; we copy the run as written.)

## 6f. Corpses in the pack, RESURRECT, and per-member counts (2026-10-31)

> Provenance: READING Nuvie `core/converse_interpret.cpp` (the OBJINPARTY and RESURRECT cases) to
> understand; the healers' bytecode inspected with our own host dump (Tiberius, 26). Re-expressed.
> Found by the bot harness's healer segment: Tiberius greeted a party with nobody dead with "I see
> that carries a parted friend" and asked 400 gold, and a party of four with 2 gold was "392 short".

- **OBJINPARTY(obj, quality)** (0xc7) answers WHICH party member carries an object of type obj: that
  member's actor number, or **0x8001** when nobody does. The quality is ignored (any quality
  matches). For the dead body (339) a carried mouse (354, Sherry's remains) also counts. With the
  pooled pack the carrier is always the party leader (the Avatar). Before 2026-10-31 it always
  answered 0 ("actor 0 has it"), so every healer found a corpse.
- **RESURRECT(npc)** (0xd6) does not name the dead: it names the member CARRYING the remains (the
  healers pass OBJINPARTY's answer; 0 means the party). The first dead body (339) in that member's
  inventory, else the first mouse (354), is taken out of the pack; the actor its quality names stands
  up on the Avatar's tile at 1 HP with every condition gone (experience × karma / 100 below karma
  98, as the Resurrect spell, magic_skills.md §5f). A body whose quality names nobody, or an actor
  who is not dead, is still used up (the reference deletes the body only when the actor rose; u6e
  keeps it in that case). After Armageddon nothing rises. A healer resurrects only a companion whose
  body the party carries; without one the script says nobody is dead.
- **A companion whose body is gone** (u6e does not save a companion's body, combat.md §3d, so after
  a load, or with the body left on another level, there is none to carry) counts as carried:
  OBJINPARTY(339) answers the leader, and RESURRECT with no body in the pack raises the first such
  companion beside the Avatar. Without this a companion who died before a save could only come
  back through the Avatar's own death (deviations.md).
- **Per-member counts read the pooled pack once.** HASOBJ / OBJCOUNT asked about the party leader
  (or actor 0) see the whole pooled pack and the gold; asked about another party member they see
  nothing. The healers total the party's gold member by member ("Your party takes up a
  collection"), and the merchants check the gold of the member chosen at "Which of you?"; counting
  the shared pack for each member made a party of four look four times as rich. GIVE / NEW / DELETE
  for any member still change the pooled pack. Consequence (deviations.md): at "Which of you?" the
  member who pays or sells is the Avatar (1); a companion has nothing of its own.
- Test hooks: cvmtest (OBJINPARTY with and without a body; RESURRECT asks for the pack; a second
  member counts 0 gold); the bot's healer segment (resurrect with nobody dead, heal for 30 or free).

## 7. Status
VERIFIED — the full conversation VM runs the real bytecode (greeting, expression
eval, IF/ELSE/ENDIF branching, keyword loop, BYE) for NPCs across CONVERSE.A/.B,
host AND on the TT/Falcon. Game-mutation opcodes JOIN/LEAVE/GIVE/NEW/DELETE/HEAL/CURE/
RESURRECT/WORKTYPE act on the live game (§6, §6b); NPCNEARBY stays stubbed. See also
/docs/design/m4-usecode-gate.md.
