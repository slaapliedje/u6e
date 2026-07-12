# /routines — Disassembly Analysis (Clean-Room Notebook)

PURPOSE: human-readable ANALYSIS of the original Ultima VI ST binary, used to
DERIVE specs in /docs/formats and /docs/rules. We implement from those specs,
not from this notebook and not from anyone else's source.

This is the per-routine reverse-engineering notebook. Each note documents one
identified routine; findings flow OUTWARD into the specs under /docs. See
/routines/INDEX.md for the live status table.

## CLEAN-ROOM RULES (mandatory)
- This directory contains OUR ANALYSIS, not the copyrighted binary.
- Do NOT commit the original executable or disk images (they live in
  gitignored /refdata — see refdata/README.md).
- Do NOT paste long verbatim disassembly listings. Quote only SHORT snippets
  strictly necessary to explain a finding (fair-use-sized), and prefer
  describing behavior in prose/pseudocode over reproducing instructions.
- Pseudocode and English descriptions are PREFERRED over raw m68k dumps.
- Every routine note states HOW it was derived (which tool, what addresses,
  what observation confirmed it) + confidence.
- Each note records which spec(s) in /docs the finding feeds.

## TOOLS
- Disassembler/analyzer: <<FILL IN: Ghidra (has m68k support) / IDA / etc.>>
- Live debugging: Hatari debugger (breakpoints, memdump, trace).
- The binary is analyzed locally from /refdata; never committed.

## Note format
Use an existing note (e.g. palette-setup.md) as the pattern:
1. Routine name + approximate address(es) + caller(s).
2. Status (GUESSED / ANALYZING / UNDERSTOOD / SPEC-WRITTEN) + confidence.
3. What it does, in prose (preferred over asm).
4. Pseudocode reconstruction (ours — not transcribed).
5. Evidence / how derived (tool, addresses, observation).
6. Open questions.
7. Feeds: which /docs spec(s) and milestone this informs.

Keep INDEX.md in sync when a note's status changes.
