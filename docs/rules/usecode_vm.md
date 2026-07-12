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
  HASOBJ/OBJCOUNT read it; the gold object updates `gold`);
- per-NPC poison/dead status (HEAL/CURE/RESURRECT clear; WOUNDED/POISONED read);
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
- [ ] Wire game-mutation opcodes to real systems (inventory/party/karma/heal).
- [ ] DATA/DPRINT data-block strings (some answers reference indexed strings).
- [ ] Per-game opcode deltas (Savage/Martian) behind a RuleSet hook.
- [ ] Interactive keyword TYPING on-target (line input); INPUT/INPUTNUM vars.

## 7. Status
VERIFIED — the full conversation VM runs the real bytecode (greeting, expression
eval, IF/ELSE/ENDIF branching, keyword loop, BYE) for NPCs across CONVERSE.A,
host AND on the TT. Game-mutation opcodes stubbed (see §3). See also
/docs/design/m4-usecode-gate.md.
