# Rules: the message scroll and its prompt

Games: U6 / Savage — Martian keeps its own panel for now (checklist item 13)
Implemented by: render/gamedemo.c `msg_box` (drawing only; the message text itself is unchanged)
Tracked in: /docs/INDEX.md  ·  SE checklist item 8

> Provenance
> - Derived from : the DOS games observed running (DOSBox-X: U6 GAME.EXE 4.5, Savage Empire), the ST U6 original observed in Hatari
> - References   : none
> - Tools        : DOSBox-X + Xvfb screenshots, Hatari screenshots, our bot harness (keys + screenshots)
> - Validated by  : side-by-side screenshots of the same key sequence (Look, an unknown key, Get) in DOS and in our TT and ST builds, 2026-11-17
> - Confidence   : confirmed for the layout below; probable for when the prompt appears relative to a turn's NPC moves
> - Last updated : 2026-11-17

## 1. System
The message box (U6: the bottom of the right panel; SE: the bottom box) is a running scroll. Each command's
words follow a prompt with the leader's name; when the command is done the scroll ends with a fresh prompt and
the typing cursor (U6 an ankh, SE a skull, stepping like a wait's cursor). Older lines scroll off the top.
The box is drawn the same way under the party list, the status display and the inventory.

## 2. Observed behaviour
| Situation | U6 (DOS and ST) | Savage Empire (DOS) |
|---|---|---|
| The prompt | a blank row, then `Name:` on its own row, then `>` | `name>` on one row, no blank row |
| A command | its words follow `>`: `>Look-what?`, `>Get-a carpet` | `vatar>Look-what?`, `vatar>Get-a skin` |
| A verb waiting for a target | `>Look-` and the cursor | `vatar>Look-` and the cursor |
| Its result | follows on the same row: `>Look-Thou dost see Test` | the same |
| Later lines of the same turn | under the command (`Dupre grazed.`), above the next prompt | the same |
| A letter with no command | `>what?` | `>what?` |
| Rows | 10, 8 px apart, from y 112 (17 columns) | 8, 8 px apart, from y 128 |

The first prompt of a game has no blank row above it. The name is the leader's (in Solo, the member in control).

## 3. Our build
- The words of the current command are kept as before (the bot and the tests read them); the box adds the
  commands before it, each under its prompt, and the prompt after a finished one.
- A command is seen as new when its words stop growing from what was shown. A verb's second step (M)ove's
  `To `, the count of a stack) stays on the same command, and a verb's prompt (`Look-`) stays in front of
  a result that does not repeat it (`Look-` then `what?`).
- A command's own words longer than the box still page as before (converse.md §6d): the arrow, then a key.
  The limit is unchanged (9 rows, 7 with a verb waiting, counted on the words alone).
- The words that are ours, not DOS's (the pack picker's `(L=next, Enter=look)`), show as they are.

## 4. Open
- DOS U6 sometimes shows a finished command without the fresh prompt for a moment (the prompt comes once the
  turn's moves are done); ours shows it at once.
- With a verb's crosshair up, DOS answers an unknown key with `what?` and drops the verb; ours ignores it.
- U6 DOS puts conversation text 8 px apart too; our U6 conversation keeps 9 (TPITCH).

## 5. Test hooks
Screenshots only (the bot's key sequence on ST and TT); no `make check` hook. `make bot-smoke` proves the
message text is unchanged.

## 6. Status
IN PROGRESS: §1-3 built for U6 and SE; §4 open.
