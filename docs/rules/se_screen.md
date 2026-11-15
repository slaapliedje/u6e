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
> - Confidence   : confirmed for §1-§2 (observed); §3-§6 observed, not yet built
> - Last updated : 2026-11-15

## 1. The pictures
- **SCREEN.LZC**: one item, a shape library holding one 320x200 shape (no transparent pixels): the whole
  frame. The map's border with the eyes and the sky strip, the two boxes on the right, the command bar.
- **BKGRND.LZC**: 13 items, one shape each. Item 0 (132x113) is the stone frame round a portrait with its
  name plate; items 1-12 (77x83) are scenery drawn behind a face in conversations (§5).
- **SAVAGE.FNT**: proportional, glyphs up to 9 px wide (fonts.md). DOS draws it proportionally; our
  converter clips it to 8x8 (open, §6).
- **SECURSOR.PTR**: the pointers (skull among them, converse.md §6d).

Colours are SE's own palette (SEPAL).

## 2. The map window
The map is drawn exactly as U6's view: 11x11 tiles, the party leader on the centre tile. Its tiles sit on a
plain 16-px grid from screen (0,0) (U6's sits 8 px in), so the centre tile is x 80-95, y 80-95: the Look
crosshair is drawn there.

SCREEN.LZC's frame covers the window's outer half: the visible part is **x 8-167, y 16-159** (half of
column 0 and of column 10 show; row 0 and row 10 do not). The frame stays drawn over the rest every frame.

## 3. The top border: sky and eyes (not yet built)
- The strip x 2-170, y 2-8 inside the top border shows the sky (light blue by day in the screenshots) with
  the sun in it (seen at x 143, y 3-8 at 9 in the morning). How it moves and what night shows: open.
- Two eyes in the top border's centre: green on the map, blue during a conversation. The gem's blue eyes
  (se_use.md §5d) are this.

## 4. The right panels
Two boxes inside the frame: **x 183-314, y 6-118** (top) and **x 183-314, y 126-194** (bottom).

- **Party list** (top box, the normal view): one row per member, 16 px apart from y 8: the member's
  figure at x 186, the name from x 209, the HP right-aligned to x 310.
- **Inventory** (F1-F8 for a member, F10 back to the list): the name centred at y 9; the figure with the
  readied places as gold rings round it (x 186-, y 17-77), the 4x4 pack grid of rings to its right; the
  weights `E:0/16s` and `I:0/32s` at y 80-94; the buttons (previous member, portrait, next member) and the
  combat mode box (`CMND`, `RANGE`, ...) at y 102-117.
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

## 6. The command bar
Nine icons in SCREEN.LZC, each in an 18-px cell from x 8, y 178-196: move, get, drop, use, talk, look,
attack, rest, combat (`x = 8 + 18k`). Clicking and highlighting: open.

## Test hooks
Our TT frame against the DOS screenshot (scratch pixel diff); none in `make check` yet.

## Status
IN PROGRESS: §1 (SCREEN.LZC in the pack), §2, and §4's party list and message box built; U6's status line,
command bar tiles and sky are not drawn on SE. Next: §5 conversations, §4's inventory, §3, the font, §6's
clicks.
