# The Atari ST release's data, and how it becomes DOS-shaped input

Games: Ultima VI (the ST floppies). `tools/st2dos.c` turns it into the files `mksave` and `mkworld` read, so the rest of the
conversion is one path.

> Provenance
> - Derived from : refdata/st/u6 inspected and compared with refdata/dos/u6; U.PRG and the depacked GAME.PRG disassembled
>   (notes in ~/src/u6e-re/REVERSE.md): the tile loader `$32404`, `tile_blit $325de`, `shape_draw $10d7c`, `pal_set_idx $1137a`.
> - Validated by : every art file rendered to PNG; a pack built from ST files alone boots and plays (see Status).
> - Last updated : 2026-11-05

## The same as DOS
CHUNKS, MAP, TILEFLAG, LOOK.LZD, MASKTYPE.EGA, ANIMMASK.EGA, PORTRAIT.A/B/Z, PAPER.BMP, NEWMAGIC.BMP, WORLDMAP.BMP, STARPOS.DAT,
U6.CH, U6.SET. CONVERSE.A/B: the same library layout (little-endian), different text in places.

## Byte-swapped
Big-endian where DOS is little: BASETILE, ANIMDATA and SCHEDULE (every word); BOOK.DAT (its offset table, offset[0]/2 words).

## Saved game: SAVEGAME\
OBJBLKaa..hi: one file per block, uncompressed, the layout of the DOS blocks but for byte order: the count, and the words at
record bytes 4 and 6, are big-endian. Column letter x, row letter y; DOS's block i is x = i0 + 2*i2 + 4*i3, y = i1 + 2*(i>>4). The
64 surface blocks are A-H by A-H, the five dungeon ones AI..EI. OBJLIST: as DOS but the words at 0x400 (256), 0xC00 (256), 0x15F1 (256), 0x1BF7 (the year), 0x1BFD (2) and 0x1C19 (24, the moonstones) are big-endian, and it is
longer, by 256 bytes at the end (ignored).

## Tiles
`TILEINDX.EGA`: 2048 LE words, stored. v >= 0x6000: at byte v-0x6000 in the MAPTILES buffer; else at byte v*16 in OBJTILES.
`MAPTILES.EGA`: u16 LE plane stride, then 5 strides of data. `OBJTILES.EGA`: raw. `MASKTYPE.EGA`: LZW, 2048 bytes, low two bits:
- 0: `[y0][rows]`, then for row r word r of plane p at offset 2 + p*stride + r*2 from the record (the plain tiles' stride holds
  for the OBJTILES ones as well).
- 3 (16 wide), 1 (8 wide, right half), 2 (8 wide, left half): `[y0][rows][pad]`, rows x width mask bytes (a set bit keeps what
  is behind), then planes `[p]` + rows x width bytes until `[0xFF]`; a plane left out is zero.
Pixels are big-endian bit order, plane p is bit p of the colour. Rows above y0 and below are empty.

## Palettes
No 256-colour art: 16 colours, 12-bit `0RGB` words. U.PRG holds 13 palettes in a row, 32 bytes each: located as the 13 entries
before the `FFFF FFFF` that follows them, at text offset 0x178C6 in the known release. In-game (GAME.PRG): 16 words at image
offset 0x3DA1E. GAME.PRG is Pack-Ice: stub at the text start; `lea` at 0x0A gives the base, the immediates at 0x12 and 0x1A
the packed and unpacked ends; decrunch backward (tables at 0x176, 0x18A, 0x194).

Which palette a screen uses (U.PRG's Setpalette calls): the menu and title 5, the gypsy's wagon 4, the woods 3, the character's
vellum and faces 6, the stones 1, the gargoyles' scene 2; the living room 0 and the storm 9 by eye.

GAME.PRG also holds the map from DOS colours to ST colours: 256 bytes at image offset 0x3D65A (low 4 bits; one entry has a stray
0x10). It is exact: over the 97,280 pixels of the plain map tiles every DOS pixel goes through it to the ST pixel. 0x3DA1E is the
palette it was made for, the best fit of any 16 words in the program against the DOS U6PAL. The portraits, PAPER.BMP and
NEWMAGIC.BMP are still DOS-indexed: this is how the ST draws them.

So the normaliser's palettes are: U6PAL = each DOS index through the map to the 16 colours, tiles rewritten to the first DOS index
of their colour (48 or above, below the fire range 224..231, so nothing on the map cycles: the ST has no colour cycling); FEPAL
(the front end's own, optional for mkworld) = U6PAL with the vellum's colours at 16-31 and the menu's at 32-47, the art moved
there; PALETTES.INT = the scene's at 0-15 and the same two bands.

## Text in the programs
U.PRG and END.PRG hold the DOS sentences in the DOS order, with pad bytes after odd-length strings. The two intro tables, gypsy
and TV (the same layouts, ST values), are at text offsets 94580 and 93764. U.PRG's narration starts 0x9B14. END.PRG's number words
are at 0xC398 up to 0xC51D, its narration from 0x97A; its date line is split differently from DOS's (`%s, ` not `%s` + `, `).

## Art: *.NEW
The U6 LZW, then BE u32 offsets (offset[0]/4 entries), then shapes: `hotX, w-1, hotY, h-1, planes` (BE words), then for each set
bit of `planes` a whole plane of ((w-1)>>4)+1 words a row. Colour 0 is transparent. The numbering matches the DOS `.SHP` of the
same name but for: TITLE.NEW = TITLES.SHP, INTRO_1 one more at the end, INTRO_2 one fewer, MONTAGE 12 of 15, END two more.

## How close the result is
The object lists built from ST files are byte-identical to those built from DOS files (surface objects, object list, inventory);
the whole template save differs in one word, at 0x22B44.

## Not on the ST disks
*.M, MIDI.DAT, U6MCGA.PTR, LZOBJBLK, LZDNGBLK, *.VGA, PALETTES.INT, U.EXE, END.EXE. Music is U.PRG's YM tunes.
