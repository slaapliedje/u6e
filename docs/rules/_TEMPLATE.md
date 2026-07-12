# Rules: <SYSTEM> (e.g. combat / schedules / lighting / usecode-op X)

Games: U6 / Savage / Martian — default = U6; deltas below
Implemented by: /engine <+ which RuleSet hook(s)>
Tracked in: /docs/INDEX.md  ·  Milestone: <Mx>

> Provenance
> - Derived from : <game observed running | original files | refs read>
> - References   : <Nuvie / U6Edit / Codex page — READ, not copied>
> - Tools        : <Hatari debugger / save-state diffing / our test harness>
> - Validated by  : <golden-transcript test | observed behavior | NOT YET validated>
> - Confidence   : guessed | probable | confirmed
> - Last updated : YYYY-MM-DD

## 1. System
<What this rule governs and where it sits in the main loop (engine/CLAUDE.md).>

## 2. Observed behavior (inputs -> outputs)
<Concrete examples. Prefer "given X, the game does Y" over prose theory.>
| Input / situation | Result | Notes |
|-------------------|--------|-------|
|                   |        |       |

## 3. Per-game deltas -> RuleSet hooks
- U6 (default): <…>
- Savage Empire: <delta> -> hook `on_<…>`
- Martian Dreams: <delta> -> hook `on_<…>`
(Deltas live in RuleSet overrides, NOT engine forks — engine/CLAUDE.md.)

## 4. Determinism / RNG
<If RNG is involved, note the seam so tests are reproducible (fixed seed).>

## 5. Test hooks
<Which headless logic test or golden-transcript test verifies this rule.>

## 6. Open questions
- [ ] <…>

## 7. Status
TODO | IN PROGRESS | VERIFIED   (mirror this into /docs/INDEX.md)
