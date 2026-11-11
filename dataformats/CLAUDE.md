# CLAUDE.md — Data Formats Subsystem

## Scope
Parsers/loaders for the original game data files of all three games:
Ultima VI, The Savage Empire, Martian Dreams. They share the same engine
lineage and largely the same formats.

## Clean-room discipline (MANDATORY)
- Reference docs and projects (Ultima Codex/Fandom format pages, Nuvie,
  U6Edit, rackne's decoder, Xenerkes' LZW tool, pu6e) may be READ to
  understand a format and to WRITE a spec in this directory.
- The engine's parsing code MUST be written from OUR spec, not transcribed.
- Workflow per format: (1) study refs -> (2) write /docs/formats/NAME.md
  spec -> (3) implement from spec -> (4) validate against known-good output.

## No shipped assets
Original data files come from the user's own DOS/ST copy at runtime (like
Nuvie/ScummVM). We ship none. Asset paths are user-configured.

## Common building blocks (verified)
### LZW-compressed data
- Header: 4 bytes, **little-endian** = decompressed body size.
  If size == 0, the body is already uncompressed.
- Body: the compressed stream. If compressed, first byte is 0x00 and the
  second byte's first bit is set (the LZW reset code).
- IMPORTANT: source files are little-endian; we run on big-endian 68000.
  All multi-byte reads MUST do explicit LE->host conversion.
- Files are often LZW-compressed AND bit-shuffled — expect both.

### Library files
- Optional 4-byte (LE) total file size at start.
- Index: list of offsets to data blocks. Offset value 0 = invalid, skip.
  All offsets in a file are uniformly either 2 or 4 bytes. The first offset
  reveals the index size (first block start), so index length is derivable.
- Data blocks: uniform format within a file, variable length.

## Map data (verified, shared across all three games)
- `MAP` + `CHUNKS`: world geography.
  - `chunks` = Template[1024], Template = uint8[8*8] tile indices,
    addressed [x + y*8]. (8x8 tile "superchunk" templates.)
  - World is topologically a torus (wraps both axes). Martian Dreams uses
    this to fake a spherical world.
- Object tiles (positions/types) live in the SAVEGAME folder files, separate
  from the geography tiles — both needed for a full-detail render.
- Per-game level notes:
  - U6: small maps scale into the large map; cave entrances/holes/ladders
    line up into a true 3D structure.
  - Savage Empire: uses 4 of 6 levels; level 5 empty; level 6 contains a
    variant of the U6 gargoyle world. "Level" is NOT tied to 3D config.
  - Martian Dreams: toroidal wrap used for cylindrical/spherical world.

## Tile graphics (U6; verify per game)
- `tileindx.vga` (stored, NOT compressed — verified: array of 16-bit LE
  offsets): tile graphics file offsets.
- `maptiles.vga` (**LZW** — verified from real DOS data: declares 117408,
  file 47199; earlier "stored" note was WRONG): map tile graphics.
- `masktype.vga` (LZW): tile graphics info.
- `objtiles.vga` (LZW): object tile graphics.
- `animdata`, `animmask.vga`, `basetile`, `tileflag`: animation/flags.
- Tiles use the `u6pal` palette (in-game). `palettes.int` = cutscene
  palettes; `convert.pal` involved in conversion.

## Conversations / USECODE (HARDEST PART — flag explicitly)
- `converse.a`, `converse.b`, `all.con`: NPC dialog + scripting.
- The dialog files contain "control bytes" (usecode) that have historically
  blocked recreations — multiple independent recreators stalled here.
  Budget real time; treat dialog scripting as its own milestone.
- Reference: Nuvie conversation parser + U6Edit (dreammaster) are the most
  complete decoders of the conversation scripts.

## Other notable files (U6; map equivalents per game)
- Portraits: `portrait.a/.b/.z`. Shapes: `*.shp` (mainmenu, intro, end,
  titles, montage, etc.). Fonts: `u6.set` (normal/runic/gargish chars).
- Books/signs text, item/ground descriptions, exe string tables.
- Ignore CGA/EGA/Tandy variants (`*.cga/.ega/.tga`, `*drv`) — we target the
  VGA art path.

## Validation strategy
- For each parser, dump decoded output (tiles->PNG, maps->image, dialog->
  text) and diff against known-good references (extracted assets from the
  community tools / our own prior extraction). Round-trip where feasible.

## Per-game spec tracking (/docs/formats/)
Maintain one markdown spec per format with: byte layout, endianness,
compression, bit-shuffle notes, per-game deltas (U6 / Savage / Martian),
and which reference was used to validate.
Status: every U6 format has its spec in /docs/formats/ (index: docs/INDEX.md). Per-game
differences are noted in each spec. The main SE/MD ones are the .LZC libraries (lzc.md),
TALK.LZC conversations (converse.md §6b), and object numbering (se_md_objn.md).
See docs/design/multigame.md.

