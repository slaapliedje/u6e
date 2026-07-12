# M4 Decision Gate — Usecode / Conversation: PASS

Date: 2026-06-23
Decision: **PROCEED.** The conversation system is solvable. The project's single
biggest risk (the usecode "control bytes" that stalled prior recreations) is
retired early, as planned.

## What was proven
A real U6 NPC conversation runs end to end, from the original data file, with no
copyrighted code transcribed:

```
CONVERSE.A (library, 4-byte offsets)
  → per-NPC entry (independently LZW-compressed)
  → u6lzw_decompress → conversation script (text + control opcodes)
  → u6conv parse → interactive keyword/response loop
```

`make convspike` (NPC 2, Dupre) passes all gate assertions: greeting with `$P`
→ player-name substitution, and correct responses for name / job / quest /
leave / unknown→default / bye (which ends the conversation). The parser also
produces correct name+greeting+responses for many other NPCs (Shamino, Iolo,
Lord British, Tholden, Sherry, …), so it is not a one-off.

## Why it was tractable (the key realizations)
1. CONVERSE.A is a plain 4-byte-offset **library**; each entry is just an
   ordinary **LZW** container — both already solved in M2.
2. Inside a decompressed script, **all spoken text is double-quoted** and the
   conversation control flow (name / description / greeting / keyword sections /
   bye) uses a small, regular set of single-byte opcodes (0xFF, 0xF1, 0xF2,
   0xF6, 0xF7, 0xEF, 0xB0, 0xB6, 0xEE). The readable layer can therefore be
   parsed and run WITHOUT yet implementing the usecode expression evaluator.

## Scope of the spike (honest boundaries)
- DONE: container + decode + the readable conversation loop (display + keyword
  branching + end). Confirmed against the real game text.
- NOT DONE (deferred to M6, the full VM): the usecode EXPRESSION/flag/branch
  language — conditionals, variables, arithmetic, give/take item & gold,
  join/leave, set-flag, ask-for-string/number, and the per-game opcode deltas.
  These are well-bounded follow-on work, not unknowns that threaten viability.

## Consequence
No rescope needed. Proceed with the engine plan; the full usecode VM remains a
tracked M6 task. Specs: /docs/formats/converse.md, /docs/rules/usecode_vm.md.
Spike code (throwaway-grade, but reused): dataformats/u6conv.*, tools/convspike.c.
