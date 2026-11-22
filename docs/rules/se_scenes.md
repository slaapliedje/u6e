# Rules: Savage Empire's scenes in play (MIDGAME1-5)

> Provenance
> - Derived from : SE's GAME.EXE (`~/src/u6e-re/dos/se.bin`, Turbo C++): the scene player at image 0x29b0a and
>                  the five routines that call it (0x1dbfe, 0x1dd70, 0x1ded4, 0x1e1ac, 0x1e34e); the
>                  MIDGAMEn.LZC files read with our LZC and shape decoders (formats/lzc.md, shapes.md)
> - References   : none
> - Tools        : objdump -m i8086; tools/mkworld (CMID section); scratch LZC item dumps
> - Validated by  : the pictures decoded to 160x144 and the texts read as the scenes' words; `make check-se-use`
>                  (`SE scene ...`); NOT YET compared with DOS on screen
> - Confidence   : probable
> - Last updated : 2026-11-22

Implemented by: render/gamedemo.c (se_midgame and the five scene routines), tools/mkworld.c (CMID).
Per game: SE only. U6's cutscenes are its own (ending.md); MD's are checklist item 13.

## 1. The files
`MIDGAME1.LZC` .. `MIDGAME5.LZC`, each an LZC library (formats/lzc.md) of stored items, four per scene: items
4k, 4k+1, 4k+2 are the scene's text pages (plain text, NUL-ended; a page that begins `*E`, the `*END*` page,
ends the scene early) and item 4k+3 is its picture, a one-shape library (shapes.md) of 160 x 144.

| File | Scenes | Faces | Song | Disk |
|---|---|---|---|---|
| 1 | 3 | 2 (Aiela), none, none | 14 | 2 |
| 2 | 3 | none | 15 | 2 |
| 3 | 4 | none | 16 | 7 |
| 4 | 3 | none | 17 | 7 |
| 5 | 3 | none | 12 | 7 |

Each routine starts its song first (formats/se_music.md). The table at DS:0x27c6 (by file number) is the disk the
file is on: before each scene and each page the player calls 0x2a81:0x98, the "Please insert DISK n" check (corrected
2026-11-22: this doc first read it as a song).

## 2. The player (0x29b0a)
Arguments: the faces (one byte per scene, 0 = none), the number of scenes, the file number. It sets DS:0xa39
(scene mode: the face routine then takes actors 0xfd-0xff as faces 0x59-0x5b), hides the pointer (0x2abf:0x14e; 0x13d shows it), and for each
scene in turn:
1. a face, if the scene has one: drawn in the top box as a conversation draws it (rules/se_screen.md §5);
2. the picture at x 8, y 16 (the map window's visible area); a picture that cannot be read ends the play;
3. the message box (x 184-311, y 128-191) cleared to the panel's colour, the text cursor at its top left;
4. for each of the three text pages: a page beginning `*E` ends the scene; else the page is printed in the
   text colour (black), and the game's input routine (0x5d0:0x22c8) waits for a key or a click; then the box is
   cleared again.

The pointer comes back after the last scene.

## 3. The five scenes
Each routine stops the world's drawing (DS:0x3ae), starts its song, sets the screen for the scene, clears the
message box and turns the border's eyes blue (tiles 0x175/0x176 at x 0x40/0x50), plays its file, then does its
own work, redraws the party and the view, and lets the place's song come back.

- **1, Aiela's (0x1dbfe)**: from the swinging door (se_use.md, 275). After the play: Aiela's talk flag
  8-15 (DS:0x3298, objlist 0x12f1) loses 0x80; Aiela joins the party (0xf8d:0x290, the join with its party limit);
  actor 23 is put at (115,168) on the current level, loses 0x80 in flags 8-15, gets 0x01 in its talk flag and
  worktype 8; talk flag 16 gets 0x80; talk flag 1 gets 0x80 for an egg scan (0x26f50) and then loses it and
  gets 0x01.
- **2, the gem's (0x1dd70)**: se_use.md §5d (the clock runs on to noon before the play).
- **3, the control panel's (0x1ded4)**: se_combat.md §3g.
- **4, the drum's (0x1e1ac)**: se_use.md §5r (with the levels, checklist item 11).
- **5, the dark moonstone's (0x1e34e)**: se_combat.md §3g; then the game ends and SAVAGE.EXE plays the ending.

## 4. Engine notes
The pack carries the five files as a CMID block at the end of the library tail (formats/pack_cmid in
mkworld.c: pictures decoded to 160 x 144 indices, texts as they are), read from the disk when a scene plays, so
no machine keeps them in memory. The songs wait for SE's music (checklist item 9, MUSIC.LZC).

## Test hooks
`make check-se-use`: `SE scene ...` lines (the CMID block found, the first scene's text and picture read).

## Status
IN PROGRESS: §1-§3 built but the songs; the drum's scene comes with item 11.
