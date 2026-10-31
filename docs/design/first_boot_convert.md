# First-boot conversion on the Atari (plan)

Goal: a player copies the original DOS Ultima VI files into the game folder, runs the game, and
the Atari builds everything it needs on the first start. No Linux PC step.

> Provenance
> - Derived from : our own build pipeline (Makefile `world`, `save-template`, `roof-extract`,
>   `gamedata`, `m2pcm`; tools/mkworld.c, mksave.c, nuviesave.c, m2pcm.c), surveyed 2026-10-30
>   with callgrind on the host build and a cross-compile of mkworld with m68k-atari-mint-gcc.
> - References   : distribution.md, memory_streaming.md, intro_sequence.md §1b.
> - Confidence   : plan. The 68000 run times are estimates from host instruction counts; nothing
>   has run on target yet.
> - Last updated : 2026-10-30

## 1. What the PC builds today

| step | inputs | output |
|---|---|---|
| save template (`mksave`) | LZOBJBLK, LZDNGBLK | SAVE.000 (142 KB) |
| object extract (`nuviesave`) | SAVE.000 | temporary objblk/objlist/inv |
| world packs (`mkworld`) | about 35 DOS files (MAP, CHUNKS, *.VGA, TILEFLAG, LOOK.LZD, *.SHP, PALETTES.INT, U.EXE, END.EXE, BOOK.DAT, U6.SET …), 1.65 MB | U6.PAK (1.08 MB) + U6FE.PAK (2.1 MB) |
| copies | CONVERSE.A/B, PORTRAIT.A/B, *.M, MIDI.DAT | used as they are |
| AdLib PCM (`m2pcm`, optional, exact render) | *.M | music/*.PCM (21 MB) |

The only input that is not a DOS file is the optional roof overlay from ScummVM's `ultima.dat`
(Nuvie's own add-on data). Without it `mkworld` uses its floor-tile fallback. It stays optional:
a player may drop `roof_map_00.dat` + `roof_tiles.bmp` into the folder, and mkworld already looks
for them in the data directory. It is never compiled in.

## 2. Can it run on a 68000?

- **mkworld cross-compiles cleanly.** All multi-byte reads are explicit little-endian byte
  assembly and all writes are explicit big-endian, so the code has no host-endianness assumptions.
- **CPU:** 87% of the host instruction count is `load_shape` re-decompressing the whole `.SHP`
  file for every shape. With one decoded `.SHP` cached, the work falls to about 0.3 G host
  instructions. That is an estimated **7-10 min on an 8 MHz ST, 2-3 min on a stock Falcon and
  1-1.5 min on a TT**.
- **Memory:**
  - Today it uses 3.2 MB of static buffers plus a 2.9 MB heap peak (a 1 MB indoor bitmap, a 672 KB
    page table, a 512 KB book buffer and others). These need to become heap blocks that are freed
    after each stage, and output must stream to disk with `Fseek` (GEMDOS 0x42) for the patch-backs.
    The target is a working set under about 1.2 MB.
  - mksave's 1.76 MB record table can shrink to the few KB one object block needs.
- **Floating point:** a few `double`s (one `sqrt`, colour centroids) are small loops; convert
  them to integer.
- **AdLib music: convert, don't render.** Rendering every song through an OPL2 emulator, as the
  PC's `m2pcm` does, would take hours on a TT. The Atari tool converts instead:
  - each `.M` song is already decoded into note events by our own decoder (the YM2149 and MIDI
    paths use it);
  - each distinct instrument (one OPL2 patch: the operator registers) is rendered once, for a
    second or so at a reference pitch, as an attack part plus a sustain loop. The cost depends on
    the number of instruments, not on the length of the music;
  - the output per song is the note list plus its instrument samples, and the game plays it
    with a real-time sample mixer through STE/TT/Falcon DMA sound.
  - Unknowns: the CPU cost of mixing up to 9 channels on a stock TT or Falcon (not measured;
    the Falcon's DSP could do the mixing), and how close it sounds to real FM, since pitch-
    dependent envelopes and vibrato are approximated. `m2pcm` stays as the exact PC path.

## 3. Design

- **CONVERT.TOS:** a separate program built from the same tool sources, with `#ifdef` hooks for
  progress and file I/O. It is separate because on a 4 MB ST the game's own BSS (1.4 MB) leaves no
  room for the converter inside the game. `installer/install.c` already does GEMDOS-only text UI
  and can be the shell.
- **Game boot:** if `U6.PAK` is missing but `MAP` and `LZOBJBLK` are present, the game runs
  CONVERT.TOS with `Pexec` (GEMDOS 0x4B) and starts again. If neither is present, it says which
  files to copy.
- **Order:**
  1. Check the DOS files are present and the right size (e.g. U6.CH is 2048 bytes), and list
     what is missing.
  2. Check free space with `Dfree` (GEMDOS 0x36). About 3.3 MB is needed.
  3. Write SAVE.000 (mksave and nuviesave merged; mkworld then reads SAVE.000 directly).
  4. Write U6.PAK.
  5. Write U6FE.PAK section by section.
  6. Write each output as `*.TMP` and `Frename` (GEMDOS 0x56) it when complete, so a reset half
     way just restarts the conversion.
- **Progress bar:** a full-screen text display through `Cconws` (GEMDOS 0x09), redrawn in place
  with VT-52 cursor positioning (`ESC Y`), so it works in every resolution and on a graphics card:
  - a bar for the current stage, `[#########.........]  47%  U6FE.PAK: portraits`;
  - an overall bar below it, with each stage weighted by its measured share of the run time,
    and a time-remaining estimate from the 200 Hz timer (`_hz_200` at $4BA, read in supervisor
    mode);
  - the stages report progress in work units (map chunks, shapes, pages), so the bar moves
    steadily instead of jumping per file;
  - errors stop the bar and print which file is missing or what failed.
- **Afterwards:** the DOS folder can be trimmed to CONVERSE.*, PORTRAIT.*, *.M and MIDI.DAT, which
  the game still reads.

## 4. Steps

1. Host: cache the decoded `.SHP` in mkworld; turn statics into freed heap blocks; stream output.
   Packs must stay byte-identical to today's (`cmp`).
2. Cross-build mkworld + mksave as CONVERT.TOS. Run it under Hatari on a 16 MB TT and `cmp` the
   output with the host packs.
3. Fit the 4 MB ST (working set) and time it on a stock ST, TT and Falcon in Hatari.
4. Add the boot check and `Pexec` to the game, the progress display and the restart logic.
5. AdLib converter: per-instrument sampling on the host first, compared by ear with `m2pcm`'s
   output; then the real-time DMA mixer in the game, timed on a stock TT and Falcon.
6. Update distribution.md: the floppy set ships the program and CONVERT.TOS only; the player
   supplies the DOS files.
