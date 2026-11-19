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
> - Last updated : 2026-11-18 (conversations, a letter at the crosshair, the prompt after the turn)

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
| A letter or digit at a verb's crosshair | `>Look-what?`, the verb dropped (g, k, l, 5 tried); Space is ignored | (not tried) |
| Rows | 10, 8 px apart, from y 112 (17 columns) | 8, 8 px apart, from y 128 |

The first prompt of a game has no blank row above it. The name is the leader's (in Solo, the member in control).

## 2b. Conversations (U6, DOS observed 2026-11-18)
The talk runs on in the message scroll, under the lines before it: `Test:`, `>Talk-Dupre`, a blank row, the
look line (`You see` and the speaker's description, Dupre's ending on his armour), a blank row, the greeting, a
blank row, `you say:` and the ankh. What is typed follows `you say:`; then a blank row and the answer, and so on.
After `bye` the talk stays in the scroll and the next prompt follows it.
- The panel above: the sky strip stays; the face (56x64) has no frame, at y 24, centred (x 216), the name on
  the row under it (y 88). For a party member the face moves right (x 248) and the figure with its readied
  places stands left of it, the inventory's doll 8 px lower (figure at 0xc0,0x28).
- The name is the one the script gives once the Avatar has met the speaker (talk flag 0) or for a party
  member; a stranger goes by its kind (`mage`, and `>Talk-mage`).
- Page waits as converse.md §6d, the window's count 8 (two of it gone on `Talk-name` and the blank row, so
  the first page shows 5 rows of the look line before the arrow), the box showing 10 rows.
- A `*` inside the look line is not a wait (Dupre's `armor. *`: DOS's arrow is the page wait on the next row).
  A `*` right after a line break shows the arrow at the end of the line before (Nystul's `mage.` then `*`):
  DOS has not moved down yet.

## 3. Our build
- The words of the current command are kept as before (the bot and the tests read them); the box adds the
  commands before it, each under its prompt, and the prompt after a finished one.
- A command is seen as new when its words stop growing from what was shown. A verb's second step (M)ove's
  `To `, the count of a stack) stays on the same command, and a verb's prompt (`Look-`) stays in front of
  a result that does not repeat it (`Look-` then `what?`).
- A command's own words longer than the box still page as before (converse.md §6d): the arrow, then a key.
  The limit is unchanged (9 rows, 7 with a verb waiting, counted on the words alone).

- The fresh prompt waits while the turn's moves are still to come (the command's time not yet spent, a rest,
  a moongate walk, a click-travel), as DOS shows it once they are done.
- A letter or digit at a verb's crosshair: `what?` and the verb dropped, as DOS, the verb's own letter too (at
  Drop- as well). A carried thing is picked in the inventory display (equipment.md §5); the pack pickers that
  stepped through the pack on the verb's letter were removed 2026-11-18 at the user's call.
- U6 conversations: §2b, all of it (the scroll, `you say:`, the unframed face, the member's figure, the
  stranger's kind, rows 8 px apart). The bot reads `you say:word` where SE keeps `> word`.

## 4. Open
- SE's answer to a letter at the crosshair was not tried in DOS SE (ours: as U6).
- On the ST the member's figure in a conversation shows in that portrait's 16 colours.

## 5. Test hooks
Screenshots only (the bot's key sequence on ST and TT); no `make check` hook. `make bot-smoke` proves the
message text is unchanged.

## 6. Status
BUILT for U6 (§1-3, 2b); SE's scroll built, its conversation keeps its own screen (se_screen.md §5); §4 open.
