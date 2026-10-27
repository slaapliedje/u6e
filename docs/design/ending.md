# The ending cutscene (U6) — the Codex, the two kings and the lenses

Games: Ultima VI only (SE/MD have their own endings — later).
Implemented by: /render gamedemo.c (`run_ending` + the ES_ sprite compositor, `fe_bind_end`),
/tools mkworld.c (the FES_END front-end section: assets + text extracted from the user's files),
dataformats/fepack.h (section id). Triggered by the vortex cube (docs/rules/usecode_use.md).

> Provenance
> - Derived from : READING the U6 script in Nuvie's `ultima.dat` (`u6/ending.lua`: the sprite
>   list, placements, clips, beats, music cues, fades and the elapsed-time wording) and the
>   reference's canvas/sprite primitives — to understand the choreography; re-expressed. The
>   user's own END.SHP (13 shapes, sizes via tools/shapedump), ENDPAL.LBM (ILBM CMAP) and
>   END.EXE (the narration as C strings) inspected for the asset facts below.
> - References   : docs/formats/shapes.md (SHP decoding, transparency), fonts.md (U6.SET),
>   docs/design/chargen_authentic.md (the same input-advanced scene model + U.EXE text rule),
>   memory_streaming.md (U6FE.PAK sections).
> - Tools        : build/shapedump, python on ENDPAL.LBM / END.EXE (string survey).
> - Validated by  : ENDING.REQ headless captures on emulated TT + the stock 4 MB Falcon (§6).
> - Confidence   : confirmed for the sequence and placements (from the reference); the text's
>   exact line layout and the elapsed-time arithmetic are probable (END.EXE not disassembled).
> - Last updated : 2026-10-27

## 0. Clean-room rules for this scene

- **The narration is Origin's text.** It lives ONLY in the user's own `END.EXE` as NUL-terminated
  C strings. mkworld extracts it at build time into the never-committed pack; nothing of it is in
  this repo. Below each page is identified by its opening words + length only (as
  chargen_authentic.md does). A closing quote is stored as `%c` (glyph 0x7F at display).
- **Structural location** (no copyrighted anchor): END.EXE holds a block of **thirty number
  words** ("zero" … "twenty-nine": a run of ≥20 consecutive short lowercase fragments with no
  spaces); the narration is the contiguous run of printable prose fragments that FOLLOWS it —
  **42 pre-wrapped lines** (15 pages, line counts 1,4,2,1,5,3,1,2,6,1,1,3,4,4,4) then the **3
  congratulation lines**, then a DOS quip we do not use. The number words are generic English
  (an engine fallback table exists); the 45 lines are the extracted text.
- The art (END.SHP) and palette (ENDPAL.LBM) are the user's files, packed into U6FE.PAK.

## 1. Assets

| asset | what | size |
|---|---|---|
| END.SHP 7 | the chamber of the Codex (background; its window hole at (106,0) is uncovered) | 320×200 |
| END.SHP 4 | the solid wall filling the window | 113×94 at (106,0) |
| END.SHP 0 | the see-through wall (its colour-0 pixels are transparent: the stars show) | 113×94 at (106,0) |
| END.SHP 1 / 2 | the Codex closed / open | 22×27 at (151,29) / 57×41 at (133,23) |
| END.SHP 3 | the red moongate | 82×138, rises from y=125 to −12 at x=9, later x=230; clipped to y<126 |
| END.SHP 8 | tableau A: Lord British + Nystul (left half), the gargoyles (right half) | 320×200 |
| END.SHP 9 | tableau B: the lenses raised (left half, later full width) | 320×200 |
| END.SHP 5 / 6 | the blue (concave) lens / the red (convex) lens on the floor | 36×52 at (73,87) / 24×54 at (218,87) |
| END.SHP 10 / 11 / 12 | the big / small / medium parchment scrolls | 318×65 / 318×35 / 318×48 |
| ENDPAL.LBM | the scene palette (ILBM CMAP, 256 × 8-bit RGB → 6-bit in the pack); entries 16..31 are the gate's red ramp that rotates | 768 B |
| star window | OUR OWN: 48 twinkling stars in the 107×84 window at (106,0), three brightness tiers picked from the palette (the reference draws its own procedural star field too) | — |

Music: `brit.m` as the scene opens, `gargoyle.m` when the second gate rises, `end.m` when the
book opens (the music manager's BRIT / GARGOYLE / END songs).

## 2. Pack section FES_END (U6FE.PAK, dataformats/fepack.h)

`[768 pal (6-bit)][48+256 ST 16 + LUT][u8 nsh=13] + nsh × [u16 w][u16 h][w×h]` (shape 0 with
its index-0 pixels remapped to 255 = transparent) `+ [u8 nlines] + nlines × [u16 len][bytes]`
(the 45 END.EXE lines, `%c` kept) `+ [u8 nnum] + nnum × [u16 len][bytes]` (the 30 number words).
Resident machines bind it with the rest of the front-end; streamed machines (4 MB) load it on
demand when the ending starts, together with the front-end font and scroll sections. Absent or
unloadable → the engine's plain notice ("The Codex has vanished!" — our own words).

## 3. Draw order and clips (per frame, back to front)

bg 7 → stars (clip 106..213 × 0..84) → Codex 1 → Codex open 2 → see-through wall 0 → wall 4 →
gate 3 (clip y<126) → tableau A 8 (clip x 0..160, later 160..320) → tableau B 9 (clip 0..160,
later 0..320) → blue lens 5 → red lens 6 → the scroll + its text lines.

Text: the proportional front-end font (U6.SET; a page runs to ~320 characters inside a 65-px
scroll, so the original cannot be using the 8×8 font), colour 0x3e, 8-px line pitch, left margin
7 px inside the scroll; single-line pages centred. END.EXE stores each page as source-code
fragments (the splits are NOT display lines): the page is the JOINED paragraph, re-wrapped to the
scroll's 296 px at display — as the reference prints it. Scroll placement per page (from the
reference): big at (1,133), medium at (1,151), small at (0,160) (or (0,152) for pages 8 and 11);
first-line offsets 13/12/8/9 px as listed in the code's page table.

## 4. The beats (input-advanced; every animation skippable, Esc aborts)

| # | beat | animation | page (opening words, chars) | music |
|---|---|---|---|---|
| 1 | fade in; the gate rises at x=9 | 1 px / 25 ms, palette 16..31 rotates every 5th step | "A glowing portal …" (40) small scroll | brit |
| 2 | Lord British and Nystul appear (tableau A, left) | rotate | "From its crimson depths …" (4 lines) big | |
| 3 | the gate sinks and vanishes | rotate | "\"Thou didst have just cause …" (2) medium | |
| 4 | | | "\"WHAT HAST THOU DONE …" (1) small | |
| 5 | | | "You pick up the concave lens …" (5) big | |
| 6 | the blue lens is gone; tableau A hidden (its clip moves to the right half); tableau B (left) shown | | (page 5 stays) | |
| 7 | the wall dissolves to the star window, the Codex dissolves in | dither dissolve, stars twinkle | "As Lord British holds the glass …" (3) medium | |
| 8 | | twinkle | "Yet the book remains closed." (1) small | |
| 9 | the second gate rises at x=230 | rotate + twinkle | "And waves of heat shimmer …" (2) small at y=152 | gargoyle |
| 10 | the gargoyles appear (tableau A, right half) | rotate + twinkle | "King Draxinusom of the Gargoyles …" (6) big | |
| 11 | the gate sinks and vanishes | rotate + twinkle | "\"Thy time hath come, Thief …" (1) small | |
| 12 | | twinkle | "Quickly you reach down …" (1) small at y=152 | |
| 13 | | twinkle | "…and you press it into the hand …" (3) medium | |
| 14 | tableau A hidden, tableau B full width, the red lens gone | twinkle | "At your urging, King Draxinusom …" (4) big | |
| 15 | the open Codex dissolves in over the closed one | twinkle | "The ancient book opens …" (4) big | end |
| 16 | | twinkle | "…and when its wisdom is gleaned …" (4) big | |
| 17 | the scroll goes; the open Codex dissolves out; the wall dissolves back; the stars go | | — | |
| 18 | fade to black, then the congratulations scroll fades in at (1,68) | | CONGRATULATIONS / "Thou hast completed Ultima VI: The False" / "Prophet, in <elapsed>." / "Report thy feat unto Lord British at Origin Systems!" (wrapped) | |

Afterwards a key ends the game: the engine returns to the desktop (the original's END.EXE
returned to DOS).

**Elapsed time**: from the game's start date 7-04-0161 (moongates.md §1) to the current date in
the U6 calendar (28-day months, 12-month years): years, months and days spelled with the number
words (≤29) or digits; "Prophet, in two months, three days." — one line, or two when it does not
fit the scroll. (The reference reckons with 365/30-day years/months; the original's arithmetic
is unknown — probable.)

## 5. u6e deviations

- The reference's alpha fades (wall ↔ stars, the Codex in/out, the final scroll) are 4×4
  ordered-dither dissolves of 16 levels over ~2 s (8-bit indexed targets have no alpha).
- The star window is our own (the reference's is procedural too, not an asset).
- First-line offsets follow the reference's page table; the wrapped line count per page is
  whatever the proportional font yields (the reference's own wrapping differs by a few pixels).
- On the ST (static 16-colour LUT) the gate's palette rotation is a no-op, as in the other scenes.

## 6. Test hook

`ENDING.REQ` byte = the beat to HOLD at ('1'..'9', 'a'..'i' = 10..18): the game jumps straight to
the ending (as if the cube had fired), auto-advances every earlier beat (animations run to their
end), then holds that beat for a capture. Console prints `END hold beat=`, `END assets=`
(13 shapes + 45 lines bound), `END streamed=` (a 4 MB machine's on-demand load succeeded).

## Status
VERIFIED 2026-10-27. ENDING.REQ holds on emulated TT (beats 1, 7, 10, 15, 18) and the stock 4 MB
Falcon (beats 1, 10, 18): the chamber with the risen gate, both lenses and the first line on the
small scroll; Lord British and Nystul with the concave lens, the wall dissolved to the star window
with the closed Codex; the second gate risen behind King Draxinusom and his attendants with the
six-fragment page re-wrapped to five lines of the big scroll; both lenses raised and the open Codex;
the congratulations scroll ("CONGRATULATIONS" / the completion sentence wrapped as the reference
lays it out / "Report thy feat …" on two lines). On the 4 MB Falcon the 280 KB section streams in
at game end (`END streamed=1`); mkworld extracts 45 lines + 30 number words from the user's END.EXE
(`ending text: extracted 45 lines`). Found on the way: END.EXE's fragments are source splits, not
display lines — pages must be re-wrapped (§3). `make check` ST+TT passes.
