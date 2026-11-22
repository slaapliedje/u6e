# Savage Empire's front end: plan and findings

Checklist item 9 (docs/design/se_md_checklist.md). DOS SE's front end is its own program, SAVAGE.EXE (Turbo C++,
not packed; image = the file from 0xc00, DGROUP at image 0xdaa0; unpacked copy outside the repo at
`~/src/u6e-re/dos/savage.bin`). GAME.EXE runs it again at the end (se_combat.md §3g: `savage.exe` with the
directory, `D`/`B`/`Y` date arguments and `LA`/`LT` for the companion). It plays the intro, the menu, the story,
character creation, "About", the ending, and runs GAME.EXE for "Journey Onward".

## Files it reads
| File | What (decoded with our LZC + shape readers) |
|---|---|
| SAVAGE.PAL | palettes (2051 bytes; loaded at image 0x3fdd into DS:0x2be4) |
| SAVAGE.FNT | the cutscene font (formats/fonts.md, the WOU font) |
| SAVAGE.TIM | 5510 bytes, timing (not yet read) |
| CONFIG.SE | 6 bytes read at 0x3f4b (the sound setup) |
| INTRO.LZC | 3 items: (0) 2 pictures 320x200 (Lord British presents; Worlds of Ultima); (1) 13 shapes (the jungle pan 320x156, trees, the title's parts); (2) 8 shapes (pan foreground, credits art) |
| TITLE.LZC | 13 items: the menu screen and its animations (item 0: 320x128 background + 3 parts; items 1-11: animation frames; item 12: 182x128) |
| CREATE.LZC | 8 items: (0) the creation screen 320x200 + 15 word plates; (1) 2 pictures 160x145 + a 320x200; (2) 6 pictures 160x145 (the questions' scenes?); (3) 20 frames 95x110 (Intanya talking?); (4) 10 small parts; (5) a 320x200 + 10 faces 79x85 (the Avatar's face to choose); (6) 13 figures; (7) 2 parts 122x45 |
| ENDGAME.LZC | 1 item: 5 pictures 320x200 with a text box (the ending's scenes) |
| MUSIC.LZC | the songs (formats/se_music.md, being written) |

**SAVAGE.PAL** is itself a one-item LZC (size 2051; one LZW item) holding 2304 bytes: three palettes of 256 RGB
colours, 8 bits a component (0x00-0xff). By eye: palette 0 fits the intro's pictures (Lord British presents, the dusk
jungle) and the menu screen; palette 1 the creation screen's stone frame and the ending's pictures; palette 2 the
starry Origin FX opening. Correction to the table: TITLE.LZC is the **Origin FX opening** (the conductor's frames, the
globe 182x128), not the menu; the **menu screen** is CREATE.LZC item 0's first picture (the title over the jungle
temple) with the word plates after it. Which code sets which palette, and the fades, are still to be read. The strings in SAVAGE.EXE include the six creation questions (each `(a)` or `(b)`), Intanya's opening
words, the story so far (10 paragraphs, DS:0x10a7-0x157b, reached through a pointer table) and the ending's texts
(DS:0x15e0-0x1c9e, with the Aiela / Tristia branches), `CONGRATULATIONS. You have completed THE SAVAGE EMPIRE in `
and the date words (`month`, ` and `), and the title's animation scripts (strings of frame letters at
DS:0x1e3e-0x1fe2).

## The flow seen in DOS (DOSBox, 2026-11-22; scratch captures in /var/tmp/claude-1000/u6e/sedos/intro)
1. The Origin FX opening: a conductor's silhouette before a globe, then the ORIGIN FX logo with sparks.
2. `Lord British presents...` (INTRO 0.0).
3. `a Worlds of Ultima game` with the copyright (INTRO 0.1), fading.
4. A dinosaur jungle at dusk; THE SAVAGE EMPIRE title drops in, letter blocks bouncing; it fades.
5. The credits over a panning jungle (story and screenplay, software, music and sound effects, art, the rest).
6. The menu: the title over a jungle temple with a waterfall (animated), four items: The Story So Far...,
   Create New Character, About The Savage Empire, Journey Onward. Keys: first letters, arrows, Enter. Esc in the
   intro skips to the menu.
7. Create New Character (seen 2026-11-22): Intanya in an oval vignette over grey noise, the text box on the right
   (`"In order to heal your spirit..."`, `"First, though, Intanya must know the warrior's name..."`), the name typed;
   then the questions; the DOS game then starts with Intanya's talk and the copy-protection question.

## Order of work
1. Palettes and the pack: a SEFE section (or file) with the pictures decoded and each screen's palette; the
   ST's 16-colour palettes per screen (palette_lowcolor.md).
2. The menu (static, then its animations) and "Journey Onward" into the game.
3. Character creation: name, the six questions and what they set (stats; DOS writes `savegame\objlist`), the face.
4. The story so far; About.
5. The intro film.
6. The ending (from the moonstone's scene; the date arguments, the companion).
7. The songs, once formats/se_music.md exists.

## The menu (SAVAGE.EXE 0x4a37, drawn by 0x4978)
CREATE.LZC item 0's shape 0 at (0,0) under palette 0, then a plate per item at x 85, y 116, 134, 152, 170: The Story
So Far (shape 5), Create New Character (1), About The Savage Empire (9), Journey Onward (3). The highlighted plate is
shape + 1; the one chosen shows (shape - 1)/2 + 11 for a moment. Journey Onward is offered only when a saved game
exists (DS:0x5da = 3, else 2). Keys (0x4af3): T or S the story, C create, A about, J journey (when offered),
Enter or Space the highlighted one, Up/Down move with wrap, Ctrl-Q or Alt-X quit to DOS.

## Character creation (SAVAGE.EXE 0x50ac)
Read so far (2026-11-22):
- Three stats start at 16-18 each (random 0-2 + 16): DS:0x2bcc strength, 0x2bcd dexterity, 0x2bce intelligence (the
  order the objlist writer stores them).
- The screen: CREATE item 1 shape 2 at (0,0) (320x200), item 1 shapes 0 and 1 at (8,28) (160x145, Intanya), item 4
  shape 2 at (175,30) through a second blitter (a shadow?) and again at (170,25) (the text box, 143x146); the text
  window is x 177-306, y 32-162. Without an install (no `savegame\objlist`): `You must first run the INSTALL
  program.`; with a game in progress: `A saved game is already in progress...` and back to the menu.
- 0x54bd: Intanya's opening (the strings at DS:0xd7c-0x1021, with `%P` page waits) and the name (up to 13 letters,
  into DS:0x5ec); Esc goes back to the menu.
- Three rounds of questions (0x55a0, called with the round 0, 1, 2): the first question is random among 0-2. The table
  at DS:0x564 has 12 bytes a question: up to four text parts (printed with page waits between) and the stat an answer
  (a) or (b) raises: q0 a=2 b=0; q1 a=1 b=0; q2 a=1 b=2; q3 a=0 b=2; q4 a=0 b=1; q5 a=2 b=1 (0 strength, 1 dexterity,
  2 intelligence). Keys A, B, Esc (Esc goes back to the menu). The answer raises its stat by 3-5 (random 0-2 + 3) and
  shows a picture (0x5818: strength 1, dexterity 2, intelligence 0, CREATE item 2's 160x145 pictures, being read), and
  picks the next question: after round 0, stat 0 -> q2, 1 -> q0, 2 -> q1; after rounds 1 and 2, stat 0 -> q4 when this
  was q0 else q3, stat 1 -> q4 when this was q2 else q5, stat 2 -> q5 when this was q0 else q3.
- 0x5a7c then writes `savegame\objlist` (the installed one, 0x1d73 bytes) with: strength 0x901, dexterity 0xa01 and
  0x14f2, intelligence 0xb01, HP 0xe01 = 2 x strength + intelligence (GAME.EXE's load step then makes it its own
  maximum, objlist.md §4a), the name at 0xf00 (13 letters, 0xf0d = 0), 0x1d13-0x1d18 = 0, 8, 1, 1, 0, 0 (a second date,
  probably the start for the ending's "completed in"), 0x1d71 = 0, 0x1d72 = 1, and bit 0x01 ("met") in the talk flags
  (0x18f1 + actor) of actors 2, 4, 5, 7, 8, 9 and 23. Journey Onward is then offered.

## The pack
mkworld writes a CSFE block in the library tail, just before the scenes' CMID block: `"CSFE"`, SAVAGE.PAL's three
palettes as 6-bit (3 x 768), a count, a list with room for 64 entries (file 0 TITLE, 1 INTRO, 2 CREATE,
3 ENDGAME; item; length), then the items as stored (shape libraries). Its trailer `[u32 length]["CFEE"]` sits just
before CMID's. The engine reads one item when a screen needs it and decodes the shapes then.

## Status
IN PROGRESS: the menu built (2026-11-22; TT/Falcon and graphics cards; the ST needs its 16-colour palettes): the
story and About are not built yet, and Create New Character starts a new game with the starting objlist (no
questions yet). Journey Onward resumes the last slot.
