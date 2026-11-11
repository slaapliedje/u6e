# CLAUDE.md — Documentation & Clean-Room Knowledge Base

## Scope & purpose
/docs is the project's CLEAN-ROOM KNOWLEDGE BASE, not code. It holds:
- /docs/formats/ : one spec per original data format (byte layout, etc.)
- /docs/rules/   : observed game-rule/behavior specs (combat, schedules,
                   usecode semantics, palette/cycling behavior)
- /docs/design/  : our architecture decisions (renderer, memory, toolchain,
                   milestones)

Reverse-engineering notes on the original binaries (ST and DOS) are kept
OUTSIDE this repository, in a separate workspace. Only their findings flow
OUTWARD into the specs here; no disassembly is committed.

These documents are the SPECIFICATIONS that /engine, /dataformats, /render,
/tools, and /platform are implemented FROM. Code is written from the spec,
NOT from anyone else's source. This separation IS the clean-room boundary.

## Clean-room discipline (THE central rule of this directory)
- Specs may be DERIVED FROM: reading original files the user owns, observing
  the original game running, and READING reference projects/docs (Nuvie,
  ScummVM, U6Edit, Ultima Codex/Fandom format pages, community decoders) to
  UNDERSTAND a format/behavior.
- Specs MUST be written in our own words/structures. Do NOT paste source code
  from reference projects into /docs. Describe the format; don't transcribe
  the implementation.
- Every spec MUST cite HOW it was derived (see provenance block). This
  protects the project and lets us re-verify later.
- Implementation code (elsewhere in the repo) is written from /docs specs
  only. If a coder needs a detail not in the spec, the fix is to ADD IT TO
  THE SPEC first, then implement.
- Ship NO copyrighted assets or original text/data dumps in /docs. Use small,
  synthetic, or schematic examples. Hex examples must be short and
  illustrative, not bulk extractions of game content.

## Required structure for a format spec (/docs/formats/NAME.md)
Use /docs/formats/_TEMPLATE.md as the starting point. Sections:
1. Title + which games use it (U6 / Savage / Martian) and any per-game deltas.
2. Provenance: how derived (files inspected, references read, tools used),
   and confidence level (confirmed / probable / guessed).
3. Container facts: compression (LZW? bit-shuffled?), endianness (source is
   little-endian; note target is big-endian 68000), alignment.
4. Byte layout: offsets, field sizes, types, meaning. Tables preferred.
5. Validation method: what known-good output we diff against.
6. Open questions / unknown fields.
7. Status: TODO / IN PROGRESS / VERIFIED.

## Required structure for a rules spec (/docs/rules/NAME.md)
Use /docs/rules/_TEMPLATE.md as the starting point. Sections:
1. System (combat, schedules, magic/skills, lighting, usecode-op X...).
2. Per-game deltas (default = U6; note Savage/Martian overrides -> map to a
   RuleSet hook name in /engine).
3. Observed behavior: inputs -> outputs, with concrete examples.
4. How derived (observation vs reference reading) + confidence.
5. Test hooks: what golden/transcript test verifies this.
6. Status.

## Provenance block (paste at the top of EVERY spec)
```
> Provenance
> - Derived from : <original files inspected | game observed running | refs read>
> - References   : <Nuvie / U6Edit / Codex page / etc. — what was READ, not copied>
> - Tools        : <Ghidra / Hatari debugger / hex editor / our /tools dumper>
> - Validated by  : <golden diff against X | observed behavior Y | NOT YET validated>
> - Confidence   : guessed | probable | confirmed
> - Last updated : YYYY-MM-DD
```
Never mark Confidence "confirmed" without a stated "Validated by" method.
A wrong "confirmed" is worse than an honest "guessed".

## Index / status tracking
- /docs/INDEX.md lists every spec with its Status and Confidence so we can see
  at a glance what's VERIFIED vs guessed. Update it in the SAME change that
  changes a spec's status.
- HIGH-RISK items to track prominently:
  - Conversation/usecode control-byte semantics (the documented wall that
    stalled prior recreations) — its own /docs/formats + /docs/rules pair.
  - Palette regions + color-cycling ranges (feed the /tools quantizer and
    /render present path).
  - Per-game map-level semantics (U6 nested 3D dungeons; Savage 4-of-6 +
    gargoyle-world level 6; Martian toroidal world).

## Rules for Claude when working in /docs
- When you decode/observe something, WRITE OR UPDATE THE SPEC HERE before (or
  alongside) writing code that depends on it.
- Always fill the provenance block; never assert "confirmed" without a stated
  validation method.
- Keep specs implementation-neutral (describe data/behavior, not our C
  structs) so they stay valid if internal representations change.
- Mark anything uncertain explicitly as "probable"/"guessed" — do NOT present
  guesses as facts.
- Cross-link: each spec names which subsystem(s) implement it.

## Status
- /docs/INDEX.md is seeded with the known formats/rules (all TODO) — see it for
  the live tracker. Toolchain decisions are resolved in
  /docs/design/toolchain.md; milestones in /docs/design/milestone.md.
