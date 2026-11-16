# Savage Empire: the game screen (frame, map window, panels)

Games: Savage Empire. Where U6 builds its screen from PAPER.BMP and border tiles, SE draws one
picture, SCREEN.LZC, and puts the map, the panels and the text inside its boxes.
Implemented by: /tools mkworld.c (SCREEN.LZC as SE's background in the pack), /render gamedemo.c
(`draw_border_tiles` SE branch, `g_mo`/`g_mx0..g_my1` map window).

> Provenance
> - Derived from : the user's own DOS Savage Empire, run in DOSBox-X (installed copy in the scratch area,
>   never the original folder): screenshots of the game screen, the party list, the inventory (F1, F2;
>   F10 back), a conversation, and the Look crosshair; SCREEN.LZC and BKGRND.LZC decoded with our own
>   LZC and shape decoders and compared with the screenshots pixel by pixel.
> - References   : ScummVM's map window (read to see that every game keeps the 11x11 window; nothing copied).
> - Tools        : DOSBox-X + Xvfb + xdotool; our u6lzc/u6shape decoders; scratch pixel-diff scripts.
> - Validated by  : DOS screenshot vs our TT frame at the start position (412,294): the map's visible part
>   differs in 357 of 23,040 pixels (animated tiles and walking NPCs), the frame not at all.
> - Confidence   : confirmed for §1-§2 and §5's layout (observed); §3 and §5a read from GAME.EXE (probable);
>   §4's inventory and §6 observed, not yet built
> - Last updated : 2026-11-15

## 1. The pictures
- **SCREEN.LZC**: one item, a shape library holding one 320x200 shape (no transparent pixels): the whole
  frame. The map's border with the eyes and the sky strip, the two boxes on the right, the command bar.
- **BKGRND.LZC**: 13 items, one shape each. Item 0 (132x113) is the stone frame round a portrait with its
  name plate; items 1-12 (77x83) are scenery drawn behind a face in conversations (§5).
- **The game's text** (panels, scroll, conversations) is SYSTEM.LZC's item 3: 1024 bytes, 128 glyphs of
  8x8, one bit a pixel, the leftmost pixel in bit 7 (U6.CH's layout, half as many glyphs), each drawn in a
  fixed 8-px cell. The DOS screenshots match it pixel for pixel; SAVAGE.FNT (proportional, fonts.md) is the
  cutscenes' font (item 9) and its glyphs sit 1 px further right.
- **SECURSOR.PTR**: the pointers (skull among them, converse.md §6d).

Colours are SE's own palette (SEPAL).

## 2. The map window
The map is drawn exactly as U6's view: 11x11 tiles, the party leader on the centre tile. Its tiles sit on a
plain 16-px grid from screen (0,0) (U6's sits 8 px in), so the centre tile is x 80-95, y 80-95: the Look
crosshair is drawn there.

SCREEN.LZC's frame covers the window's outer half: the visible part is **x 8-167, y 16-159** (half of
column 0 and of column 10 show; row 0 and row 10 do not). The frame stays drawn over the rest every frame.

## 3. The top border: sky and eyes
Read from GAME.EXE (0x28e06), which builds the border's top strip (x 0-175, y 0-15) from tiles each time and
copies it to the screen; t is the minute of the day.

1. Tile 0x189 in each of the 11 cells (x = 16i).
2. The sky over them, 11 cells: below the surface 0x17a (and no sun or moon); else by day (359 < t < 1081)
   0x17b, by night 0x179.
3. By day the sun 0x171 at x = ((1080 - t) / 34) x 8 (whole division): it rises on the right and sets on the
   left. By night the moon 0x172 the same way from t' = (t + 721) mod 1440.
4. The border's own tiles over the sky: 0x160+i at x = 16i, for i = 0-3 and 6-10.
5. The eyes in cells 4 and 5 (x 64 and 80): in combat red 0x177/0x178; else if the Avatar's object is 249
   (lying as a body) shut, 0x164/0x165; else green 0x173/0x174. A conversation shows them blue (0x175/0x176,
   seen in DOS; drawn by the talk code).

The sky's own pixels are only in the strip x 2-170, y 2-8: the border tiles cover the rest.

## 4. The right panels
Two boxes inside the frame: **x 183-314, y 6-118** (top) and **x 183-314, y 126-194** (bottom).

- **Party list** (top box, the normal view): one row per member, 16 px apart from y 8: the member's
  figure at x 186, the name from x 209, the HP right-aligned to x 310.
- **Inventory** (F1-F8 for a member, F10 back to the list): the name centred at y 9; the figure with the
  readied places as gold rings round it (x 186-, y 17-77), the 4x4 pack grid of rings to its right; the
  weights `E:0/16s` and `I:0/32s` at y 80-94; the buttons (previous member, portrait, next member) and the
  combat mode box (`CMND`, `RANGE`, ...) at y 102-117.
  Read from GAME.EXE: the readied places are U6's moved 8 px right (head 0xd0,0x10; neck 0xb8,0x18; the
  arm places 0xe8,0x18 and 0xb8,0x38; body 0xe8,0x28; the hands 0xb8,0x28 and 0xe8,0x38; feet 0xd0,0x40), an
  empty one ring 0x188, one a two-handed weapon blocks ring 0x187, a readied thing its tile on 0x189. The
  figure (0x29437) is 2x2 tiles like U6's: 0x194-0x197 for actors 1, 4 and 7, 0x198-0x19b for actors 6, 37
  and 48, 0x190-0x193 for everyone else. The buttons are tiles 0x180 (party), 0x181 (portrait), 0x183/0x184
  (previous, next).
- **Messages** (bottom box): the scroll, rows 8 px apart from y 128, text from x 185; the prompt (the
  leader's name and `>`, then the skull cursor) after the last line.
- The party's names are the names their scripts give (Triolo's script names him), the Avatar's the
  name chosen at creation.
- Open: how many rows the list shows before it scrolls; which frame a member's figure shows (DOS showed
  the Avatar standing while the map showed him lying on the bed); the `name>` prompt.

## 5. Conversations
The talk text fills the map window (the frame stays): x 16, rows 8 px apart from y 24, 16 rows (to y 151),
about 144 px wide; the top box holds the face: BKGRND item 0's stone
frame, the scenery behind the face, the face (FACES.LZC), and the name on the plate below it. BKGRND item 0 is drawn at (183,6), the top box's
corner. The eyes
turn blue. The bottom box is empty.

### 5a. The backdrop
Read from GAME.EXE's face routine (0x29003) and the backdrop chooser it calls (0x291ad):
- the party's level is 3: backdrop **10**; any other level below the surface: **9**;
- on the surface: the first of these boxes that holds the **speaker** strictly inside (x0 < x < x1 and
  y0 < y < y1) gives the backdrop; the last box is the whole map;
- by night (the minute of the day 1081 or more, or 359 or less) a backdrop under 5 takes 4 more (1-4 day,
  5-8 their night pictures).

| # | x0 | y0 | x1 | y1 | backdrop |
|---|---|---|---|---|---|
| 0 | 488 | 520 | 647 | 607 | 4 |
| 1-8 | (224,608)-(263,647), (320,192)-(391,239), (360,536)-(415,575), (656,792)-(711,847), (392,264)-(439,319), (976,304)-(1015,359), (544,288)-(607,351), (552,96)-(607,143) | | | | 12 |
| 9-17 | (240,168)-(775,303), (504,64)-(599,143), (472,320)-(519,359), (832,232)-(1023,479), (304,304)-(359,423), (360,464)-(521,551), (88,616)-(144,680), (280,64)-(319,119), (640,784)-(671,807) | | | | 1 |
| 18-25 | (352,248)-(543,423), (560,352)-(711,487), (256,72)-(351,167), (376,256)-(456,303), (288,176)-(319,223), (424,120)-(495,183), (168,568)-(256,639), (328,552)-(447,647) | | | | 11 |
| 26 | 0 | 0 | 1023 | 1023 | 2 |

DOS slip: on the map's very edge (x or y 0 or 1023) no box holds the speaker and the result is whatever a
register held; we give the last box's 2.

The face: BKGRND item 0 at (183,6), the backdrop at (211,8), the face (79x85, its transparent pixels showing
the backdrop) at (210,7), the name centred on x 249 at y 104.

## 6. The command bar
Nine icons in SCREEN.LZC, each in an 18-px cell from x 8, y 178-196: move, get, drop, use, talk, look,
attack, rest, combat (`x = 8 + 18k`). Clicking and highlighting: open.

## Test hooks
Our TT frame against the DOS screenshot (scratch pixel diff); none in `make check` yet.

## Status
IN PROGRESS: §1 (SCREEN.LZC in the pack), §2, §4's party list and message box, and §5 (the pack's CBKG
section holds BKGRND's 13 pictures) built; U6's status line, command bar tiles and sky are not drawn on SE.
Next: §6's clicks.
