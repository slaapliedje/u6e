# First-boot conversion on the Atari (plan)

Goal: a player copies the original DOS Ultima VI files into the game folder, runs the game, and
the Atari builds everything it needs on the first start. No Linux PC step.

> Provenance
> - Derived from : our own build pipeline (Makefile `world`, `save-template`, `roof-extract`,
>   `gamedata`, `m2pcm`; tools/mkworld.c, mksave.c, nuviesave.c, m2pcm.c), surveyed 2026-10-30
>   with callgrind on the host build and a cross-compile of mkworld with m68k-atari-mint-gcc.
> - References   : distribution.md, memory_streaming.md, intro_sequence.md §1b.
> - Confidence   : built and tested in Hatari (§3), with measured 68000 times (§2a): the converter runs in Hatari on a stock
>   TT, Falcon and ST and its packs are byte-identical to the host build.
> - Last updated : 2026-11-02

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
  instructions. That was estimated at **7-10 min on an 8 MHz ST, 2-3 min on a stock Falcon and
  1-1.5 min on a TT**. Measured: §2a.
- **Memory:** done (§2b). The converter used 3.2 MB of static buffers plus a 5.3 MB heap peak;
  it now needs about 1.2 MB in all and runs on a 2 MB ST. Output already streamed to disk, with
  `Fseek` (GEMDOS 0x42) for the patch-backs.
- **Floating point:** done (§2a). No soft-float code is left in the conversion path except one
  `sqrt` in a log line.
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

## 2a. Measured (2026-11-02)

mksave + nuviesave + mkworld, built for the plain 68000 (`-m68000 -O2`), run in Hatari 2.6.1 on
the U6 DOS files, 14 MB ST-RAM, no Fast RAM (the ST on TOS 1.04). Times are emulated CPU time
(`clock()`). Every run's U6.PAK and U6FE.PAK were `cmp`-identical to the host build.

| step | stock TT (32 MHz 030) | stock Falcon (16 MHz 030) | ST (8 MHz 68000) |
|---|---|---|---|
| first cross-build | 613 s | not run | not run |
| decoded `.SHP` cached | 151 s | not run | not run |
| + the first round below | 52 s | 104 s | 503 s |
| + the second round below | 26.5 s | 51 s | 266 s |
| + the third round below (CONVERT.TOS, 2026-11-03) | **17 s** | **36 s** | **185 s** |

Hatari's CPU profiler (`profile on`, symbols mapped from `nm`, lines from `addr2line` on a `-g`
build) found the 68000-only costs. Note that a 68000 `muls.w` takes up to 70 cycles, so even the
16-bit multiply is worth avoiding in an inner loop, and that GCC can widen a cast-to-16-bit
product back into a `__mulsi3` call when it can see the values are small (`dataformats/mul16.h`
pins `mulu.w` / `muls.w` / `divu.w` with inline asm).

First round:
- **32-bit multiply and divide are library calls** on a 68000. They came from `qsort`'s merge
  sort (now a stable counting sort, `q_sort_axis`), `py*W` per pixel in `u6shape_decode`, the
  colour searches, and `% 65521` per byte in `find_ublk`'s rolling Adler-32 (now subtraction).
- **Soft-float `double`:** the colour centroids are now exact integer means with the same
  rounding; the conversation-palette weights an exact fraction.
- **The `.SHP` cache:** small files stay decoded and one slot holds the current big file, because
  the front-end comes back to BLOCKS and MONTAGE between the big files.
- **The map:** `u6map_surface_row` reads a whole row chunk by chunk for the indoor map.

Second round (aimed at the ST):
- **LZW in 68000 assembly** (`dataformats/u6lzw_68k.s`, linked by every Atari target; the C loop
  `u6lzw_run` stays the host and reference version). Both now copy each string forward from where
  it was first decoded instead of walking prefix chains. The asm reads a code as 3 bytes at a bit
  offset, adds the dictionary entry before the lookup (so KwKwK is an ordinary lookup), rejects
  code 257 by its zero length, and keeps the first-code-after-clear and dictionary-full states in
  their own loops. On the ST it decodes the game's files 3.6× faster than the C decoder it
  replaces (LZW share of the conversion: 138 s -> 50 s). Checked on the ST against the C loop:
  all 100 LZW files of the three games at full and clipped sizes, 60 synthetic streams (42 fill
  the dictionary, which no real file does), and 240 corrupted files; 0 differ.
- **The darkening LUTs** search the palette in order of red, outward from the target, and stop
  once the red difference alone is past the best distance (same entry, lowest index on a tie;
  checked on every game palette and 300 random ones). Squares come from a table.
- **The quantiser** scores each box once, when it is made (a split leaves the others alone),
  sorts small boxes by insertion, rounds centroids by an 8-step shift-and-subtract, and the
  nearest-colour search reads squares from tables and stops a candidate early.
- **The conversation palettes** add whole parts and decide the rounding from the fractions with
  16x64-bit multiplies and compares, instead of two 64-bit divides per portrait colour; the
  terrain histogram is counted once, while the tileset is written.
- **Pictures:** `u6shape_decode` and `compose_shapes` clip each pixel run once and fill or copy
  it whole; `w*h` loop bounds are hoisted (their addresses go to `load_shape`, so GCC reloads
  them after every byte store).
- **The indoor map** decides barrier and floor once per tile type with a table, and the
  wall-dilate pass skips every byte of 8 tiles with no lone barrier (§2b: it works on bit planes).

Third round (more assembly: `tools/cv_68k.s`, with the C forms in `tools/cvasm.h` and `quant.h`):
- **The indoor map:** a row of 8 surface tiles is one of 8 rows of one of 1024 chunks, so each is
  packed once (8192 rows, not a million tiles) and a map row is a lookup per chunk. This beats any
  assembly form of the old loop. Runs are found with `cv_run` (`cmp.b` / `dbne`). 44 s -> 19 s.
- **The nearest-colour LUTs:** `q_lut_68k` works out each candidate's table offsets once and drops
  it as soon as its partial sum reaches the best distance (20 s -> 12 s).
- **The quantiser:** `CCol` is 8 bytes instead of 16; `q_sort_68k` (insertion, or counting over
  just the box's own value range) and `q_box_scan_68k`; the palette levels come from tables instead
  of a library divide each.
- **Histograms:** `cv_hist`, 4 bytes per loop. The bounding box of the intro's frames scans in from
  each edge instead of reading every pixel (9 s -> under 3 s).
- **Pictures:** plain-literal blocks are copied whole, like the RLE ones.

Portrait palettes went from 92 s to 63 s. Checked on an emulated 68000 against the C forms: 4200
random cases (hist and run sizes around 64 K, palettes that force ties, sort stability through the
weights), 0 differ. All packs are `cmp`-identical on the host and on each machine; the 2 MB ST
still converts (186 s).

What is left on the ST, by the same profile (208 s in that run): LZW 44 s, the quantiser about
37 s, the histograms 12 s, TOS 12 s, the picture decoders up to 21 s (measured before plain-literal blocks were copied whole), and smaller items. LZW looks
close to its limit: read cycle by cycle, about 10% is left, at some risk, and it costs 400 cycles
for each of 891,000 codes.

**Game start on a 4 MB ST** (after the conversion, every time): from the program start to the
main loop took 11.8 s; 2.6 s was `npc_base_fc` scanning BASETILE once per actor (now a table),
3.9 s `build_blocked` (classifying each object's tile four times; now a per-tile cache and two
passes instead of eight), and 1 s writing the indoor map byte by byte (now `memset` per run).
Now 5.9 s, plus the disk reads (Hatari's GEMDOS drive is near-instant). `build_blocked` also
runs when a door opens or a torch is lit; on the ST that call fell from about 3.9 s to 1.3 s.

## 2b. Memory (2026-11-02)

Measured on the host with a malloc wrapper (heap high-water mark, the blocks live at the peak) and
on the 68000 with `m68k-atari-mint-size`. The packs stay byte-identical to the host build: U6 with
and without `roof_map_00.dat`, Savage Empire and Martian Dreams.

| | before | now |
|---|---|---|
| static buffers (68000 BSS, all three tools) | 3.3 MB | 190 KB |
| heap peak, U6 (mksave / nuviesave / mkworld) | 2.1 / 2.2 / 5.3 MB (each leaked its inputs) | 340 / 140 / 770 KB, all freed |
| heap peak, Savage Empire / Martian Dreams mkworld | not measured | 685 / 725 KB |
| program code + data (68000) | 190 KB | 190 KB |

What changed:
- **mksave** sized its record table for 40000 records (1.76 MB); it now grows to the biggest
  object block (under 2000 records, 80 KB). The three tools free everything, because the
  converter runs them in one process.
- **mkworld inputs** are freed after their last use; OBJTILES.VGA is read straight into the
  combined tileset instead of being read and copied; the tileset (400 KB, the biggest block) is
  loaded just before it is written and freed straight after, with the command-bar palette
  (U6WX band 1, its other user) worked out at that point; the portraits are decoded just before
  they are written and read again for the conversation palettes.
- **The indoor map** works on two bit planes (barrier, indoor; 128 KB each) instead of a byte per
  tile (1 MB), and its RLE buffer is sized by a counting pass (11 KB instead of 393 KB).
- **Function statics became heap blocks** freed after use: the gypsy-question page table keeps a
  ring of the last 28 pages (9 KB instead of 672 KB); the book text goes straight to the pack
  (instead of a 512 KB table); shapes decode into a block of their own size (`u6shape_size`)
  instead of a 256 KB canvas; the front-end scene canvases, the SE/MD face and conversation
  buffers and the roof tables are allocated per use.
- Savage Empire / Martian Dreams decode their objects before loading the tileset, so the two big
  blocks are not live together.

Emulated 2 MB ST (TOS 1.04): the whole conversion runs in 270 s with byte-identical packs. A
1 MB ST runs out of memory, as expected: it has less free RAM than the 1.2 MB needed.

## 3. Design (built 2026-11-02)

- **CONVERT.TOS** (`tools/convert.c`, `make convert`): mksave, nuviesave and mkworld linked into one
  plain-68000 program, their `main()`s renamed, so one binary runs on every machine of the line
  (196 KB code, 190 KB BSS). It converts whichever game's files are in the folder, told apart by
  SAVAGE.EXE / MARTIAN.EXE, else U6 by MAP + U6.CH (`platform/firstrun.h`, shared with the game).
  - **Outputs:** SAVE.000, MENU.REQ (boots to the intro and title menu), U6.PAK and, for U6, U6FE.PAK. Every game's core pack is called U6.PAK,
    because the game loads that name and tells the games apart by the pack's magic.
  - **Checks first:** the core files a conversion can't do without (the rest of the DOS set only
    adds front-end art, which mkworld goes without), the free space with `Dfree` (GEMDOS 0x36:
    3.5 MB for U6, 1.5 / 1.7 MB for SE / MD, less what an interrupted run already wrote) and the
    free RAM with `Malloc(-1)` (GEMDOS 0x48). Each failure says what is missing.
  - **Resets:** the core pack is written as U6.TMP and renamed with `Frename` (GEMDOS 0x56) last,
    so a reset part-way leaves no U6.PAK and the game asks again. The temporary object files
    (CV_*.TMP) are deleted at the end.
  - **Screen:** BIOS `Bconout` (BIOS 3) to the console, never redirected, with VT-52 positioning,
    40 columns so it fits ST low resolution. The tools' own messages go to CONVERT.LOG.
  - **Progress:** the stage name, one bar and the elapsed and remaining time from `_hz_200`. mkworld
    marks its stages with `cv_step()` and calls `cv_tick()` inside long loops (`tools/cvhook.h`:
    weak no-ops in the host tools). Each stage weighs its measured time on an 8 MHz ST (below).
    The remaining time comes from the run so far once 10% is done, and from the estimate before.
  - **Failure:** an `atexit` handler shows the stage that stopped and points at CONVERT.LOG.
  - **Handles:** stdout goes to CONVERT.LOG through `freopen`, and a child inherits that under MiNT.
    So CONVERT.TOS saves the handle (`Fdup`, GEMDOS 0x45) and puts it back (`Fforce`, 0x46) on the
    way out, or the restarted game's console text lands in the log.
- **Game boot** (`first_start()` in render/gamedemo.c, before anything is allocated): if U6.PAK is
  missing, the game clears the screen and:
  - with no DOS files, says which files to copy; without CONVERT.TOS, says so;
  - otherwise says the files need converting once, about how long that takes on this machine, and
    "Press a key to convert, or Esc to quit";
  - on a key, with an AES (started from the desktop): `shel_write` (AES 121, doex = 1) names
    CONVERT.TOS, with the game's own path from `shel_read` (AES 120) as its command tail, and the
    game exits. On a single-tasking AES (TOS 1.x to 4.x) the shell runs that program once the
    caller exits, instead of returning to the desktop (checked in the Falcon AES source,
    GEMSHLIB.C `sh_write` / `sh_main`). So the game's memory is all freed first. CONVERT.TOS
    then does the same with the game's path, and the game starts again with its data in place;
  - with no AES (AUTO folder; `appl_init`, AES 10, leaves `global[0]` at 0) or a multitasking one
    (MagiC, XaAES: `global[1]` is not 1, and `shel_write` would start CONVERT.TOS at once, beside
    the game): `Pexec` (GEMDOS 0x4B) CONVERT.TOS as a child with `-c`, if `Malloc(-1)` shows 1.2 MB
    beside the game, then boots on. Without that memory it says to run CONVERT.TOS from the desktop.
- **Time estimate:** the measured time on the stock machine of the family (from the `_MCH` cookie:
  ST, TT or Falcon), scaled by a 0.2 s speed probe against that machine's reading, so a faster CPU
  card shortens it. The probe alone can't predict the time: how much a 68030's caches help it
  depends on code alignment, by up to 2x in Hatari, while the converter gains 10x on a TT.
- **Afterwards:** the DOS folder can be trimmed to CONVERSE.*, PORTRAIT.*, *.M and MIDI.DAT, which
  the game still reads.

### Tested in Hatari (2026-11-02)

Each test had a folder of DOS files plus CONVERT.TOS and the game, with no packs, the game
started by `--auto` from the desktop and a key sent through the command FIFO:

| machine | notice said | conversion | then |
|---|---|---|---|
| 4 MB ST, TOS 1.04, GAMEST.PRG | about 3 minutes | 187 s | game restarted |
| 4 MB TT, TOS 3.06, GAME.PRG | under a minute | 17 s | game restarted |
| 4 MB Falcon, TOS 4.04, GAME.PRG | under a minute | 36 s | game restarted |

The AUTO-folder path (`Pexec`, game boots on) was tested at 266 s, before the third round of §2a.

Under the multitasking systems (2026-11-03), where the conversion runs as a `Pexec` child:

| system | notice said | conversion | then |
|---|---|---|---|
| FreeMiNT + XaAES, TT, 64 MB TT-RAM, GEMDOS folder (before §2a's third round) | under a minute | 22 s | game booted on, played |
| MagiC 6.2, 14 MB Falcon, TOS 4.04, game on an IDE drive (before §2a's third round) | about 1 minute | 58 s | game booted on, walked, Esc back to the console |
| MagiC 6.2, 4 MB ST, GAMEST.PRG, packs already built | - | - | 1009 KB pool: too little for the pack, says so |

MagiC needs a drive it owns. Its `fopen` for reading calls `Fstat64`/`Fxattr` (0x14B/0x12C), MagiC
answers those itself, and it has no such drive when Hatari's GEMDOS folder provides it: -46, so no
file opens. That is the rig, not the game. The rig: boot files in the `-d` folder, `AUTO\HDDRIVER.PRG`
then `AUTO\MAGXBOOT.PRG`; HDDRIVER's own image as `--ide-master` (takes C:); the game on a
FAT16 image as `--ide-slave` (D:, a real MagiC drive). A small shell stand-in `Pexec`s the game.

The game's two MagiC bugs this found (render/render.c, render/rdr_falcon.c):
- MagiC sets the `EdDI` cookie, which chose the VDI backend, and that forced 8 bits on a 16-colour
  desktop. Below 8 planes (line-A `VPLANES`) the game now takes the direct backend.
- MagiC treats `VsetScreen` (XBIOS 5) with rez 3 as a request to change the desktop's resolution:
  it reloads its screen driver, restarts the shell and stops at "MEMORY BLOCK DESTROYED". With the
  `MagX` cookie the Falcon backend uses `VsetMode` (XBIOS 88), then `Setscreen` with rez -1.
  `Setscreen` with rez 0 on the ST did not do this, so rdr_st is unchanged.

Not tested: MagiC's VDI path at 256 colours or more, and MagiC 6.3.

U6.PAK and U6FE.PAK were `cmp`-identical to the reference every time. CONVERT.TOS alone on the
4 MB ST converted Savage Empire in 37 s and Martian Dreams in 41 s (TT 4 s and 5 s, Falcon 9 s and
10 s), their packs identical to the earlier build (the saves differ only in the timestamp).

Where the 185 s go on the ST (U6): portrait palettes 63 s (the per-portrait quantiser), the 18
front-end sections 72 s between them, the indoor map 19 s, tiles 13 s, the world tables 8 s and the
save template 8 s. The notice's estimate (`fr_estimate_s`) and the progress bar's weights
(tools/convert.c) come from these runs: update them when the times move.

## 4. Steps

1. Host: cache the decoded `.SHP` in mkworld; turn statics into freed heap blocks; stream output.
   Packs must stay byte-identical to today's (`cmp`). Done (§2b).
2. Cross-build mkworld + mksave as CONVERT.TOS. Run it under Hatari on a 16 MB TT and `cmp` the
   output with the host packs. Done (§3).
3. Fit the 4 MB ST (working set) and time it on a stock ST, TT and Falcon in Hatari. Done in
   Hatari (§2a, §2b); a real ST is still to do.
4. Add the boot check to the game, the progress display and the restart logic. Done in Hatari,
   including MiNT and MagiC (§3); still to check on real hardware.
5. AdLib converter: per-instrument sampling on the host first, compared by ear with `m2pcm`'s
   output; then the real-time DMA mixer in the game, timed on a stock TT and Falcon.
6. Release zip (`make release`, scripts/make-release.sh): the programs, stripped, plus CONVERT.TOS,
   README.TXT and LICENSE.TXT, and an allow-list that refuses anything else. It builds from a clean
   export with no refdata. distribution.md's floppy set still carries converted packs; shipping it as
   the program and CONVERT.TOS only is still to do.
