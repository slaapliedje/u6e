# Memory: resident where the RAM permits, streamed where it doesn't

Status: IMPLEMENTED 2026-08-16 (all targets: ST/STE, TT, Falcon, cards).
Owner: /render (loader in gamedemo.c), /platform (pools, file I/O), /tools (mkworld pack layout).

## Why

The target baseline is the machine the community actually owns: a **stock 4 MB Falcon030 or
TT030 with no Fast RAM**, then the ST/STE (4 MB, 16 colours). The user's own DFB1X (50 MHz,
128 MB Fast RAM) is a convenience, not the target. Before this change the TT/Falcon builds loaded
the whole 2.9 MB `U6.PAK` (front-end art inline) plus 0.6 MB of CONVERSE/PORTRAIT libraries into
RAM — measured on Hatari `--machine falcon --memsize 4 --ttram 0`: the ST-RAM pool after the
program is **2050 KB**, the resident set was **3535 KB**. It could not even open the pack.

At the same time the user wants the *opposite* on a big machine: "we definitely want extra RAM
taken advantage of if it's available" — the MegaSTe-ramdisk feeling of no load times. So the
policy is not "small everywhere"; it is **decide at boot from the free pools**, and keep one binary
per CPU class and one data set for every machine.

## The budget (measured, Hatari)

| config | ST-RAM pool | Fast pool | note |
|---|---|---|---|
| Falcon 4 MB, no Fast RAM | 2014 KB | — | program (text 251K + BSS 1.4 MB) already taken |
| Falcon 14 MB | 12290 KB | — | |
| Falcon 4 MB + 4 MB TT-RAM (DFB1X-like, small) | 3765 KB | 2252 KB | program lands in Fast RAM |
| ST 4 MB, TOS 1.04 | ~1.9 MB | — | |

Data (from `make world`, U6): core `U6.PAK` 1084 KB; `U6FE.PAK` 1800 KB (sections: common 69 KB /
intro 999 KB / chargen 728 KB, largest section 361 KB); CONVERSE.A/.B + PORTRAIT.A/.B 597 KB
(largest single entry 4.3 KB). Video buffers: Falcon truecolour 77 K back + 154 K screen; TT 3 × 77 K;
ST ~100 K. DMA-sound ring 32 K.

Result on the 4 MB Falcon with this design: after the core loads, **1380 KB used, 634 KB free**;
front-end streamed (peak ≈ common + intro ≈ 1.07 MB, freed before the core), libraries on demand.

## Data files: one pair for every machine

`make world` writes:

- **`U6.PAK`** — the gameplay core (map, tiles, objects, actors, books, low-colour palettes, …).
  Where the front-end block used to be inline it carries a 4-byte `NOFE` marker. The old
  `U6ST.PAK`/`U6TT.PAK` "slim" packs were byte-identical to this; they are gone (`world-st`/
  `world-tt` are aliases of `world`).
- **`U6FE.PAK` v2** — the front-end/intro/chargen art, **sectioned** (below). Never inline any more;
  an engine that meets an old inline pack says so on the console and skips the tail.

The engine (`PACK_NAME`, default `U6.PAK`) is the same binary regardless of RAM size.

### U6FE.PAK v2 layout (`dataformats/fepack.h`)

```
[4  "U6F2"]                      magic
[768 base palette]               U6PAL as RGB6 — the menu/title palette, so the front-end can run
                                 BEFORE the core pack is loaded (TT/Falcon need a real palette)
[48  ST 16 palette][256 lut]     low-colour game 16 for the shifter (palette_lowcolor.md)
[2048 font]                      U6.CH 8x8 glyphs for front-end text
[u8 nsec] nsec x [u8 id][u32 off][u32 len]   directory (off from file start)
sections...
```

Section ids (stable; append new ones):

| id | name | contents | group |
|---|---|---|---|
| 0 | MENU | main menu + title art + menu ST 16 | COMMON |
| 1 | INTRO | intro scenes (living-room pan / stones / gate-rise) | INTRO |
| 2 | TV | TV sprites + finger + LED clock digits | INTRO |
| 3 | GATE | moongate + ground cover | INTRO |
| 4 | CCPORT | chargen portraits (MONTAGE 0..11) | CHARGEN |
| 5 | TEXT | gypsy questions + intro narration + ceremony text (from U.EXE at build time) | COMMON |
| 6 | GYPSY | gypsy casting scene + scroll + GYPSY.SHP shapes | CHARGEN |
| 7 | WOODS | woods panorama | CHARGEN |
| 8 | STORM | storm window: palette, bolts, strikes, 15 layers | INTRO |
| 9 | STONES | stones pickup: hand, obsidian, bg5, clutch hand | INTRO |
| 10 | AVATAR | Avatar-into-gate climax frames | INTRO |
| 11 | GARG | §1B gargoyle-sacrifice scene | CHARGEN |
| 12 | VELLUM | chargen vellum screens | CHARGEN |
| 13 | FSET | U6.SET proportional font | COMMON |
| 14 | LOGOS | ORIGIN / Lord British boot logos | INTRO |
| 15 | SCROLL | shared narration parchment (intro captions AND the gargoyle scene) | COMMON |

Each section keeps the exact byte layout the inline block always had, cut at these seams — the
per-section binders in `gamedemo.c` (`fe_bind_*`) are the old `parse_frontend` split up. A binder
called with a NULL base **unbinds** (every pointer the runners test goes NULL/0), so a scene whose
art isn't loaded degrades to "absent" rather than reading freed memory.

## Runtime policy (`main()` in gamedemo.c)

Boot order is now the same on every target: **pools → audio → memory plan → video → front-end →
core pack → libraries → play.** (The TT/Falcon used to init video after the core; the front-end
must run before the core so its art can be freed first.)

Memory plan, before the video switch (so it prints on the console):

```
sz_fe, sz_core, sz_libs   from Fopen/Fseek only
chip_free = ST-RAM pool free - VIDEO(256K)      fast_free = Fast pool free (0 if none)
FE resident in FAST  if fast_free >= sz_fe + sz_core  and chip_free >= sz_libs + RESERVE(384K)
FE resident in CHIP  if chip_free >= sz_fe + (core in fast ? 0 : sz_core) + sz_libs + RESERVE
else                 FE streamed
```

- **Front-end resident**: `U6FE.PAK` read once (Fast pool preferred when the core also fits there,
  else ST-RAM), every section bound for good; group load/free are no-ops. Menu/intro/chargen can be
  revisited later without touching the disk.
- **Front-end streamed**: `fe_group_load(FEG_COMMON, depth 0)` at front-end start; `run_intro`
  and `run_chargen`/`chargen_capture` wrap their bodies in `fe_group_load(FEG_INTRO|FEG_CHARGEN,
  depth 1)` … `fe_group_free(1)`; `fe_group_free(0)` before the core loads. Groups are read section
  by section (`plat_read_at`) into `plat_alloc_chip` regions and released LIFO with
  `plat_chip_mark/plat_chip_reset`. A group that does not fit is released again and its runner
  returns (no partial scenes; `MEM fe_missing=1` in the MEMINFO diagnostics).
- **Libraries** (after the core): resident if `free_now >= sz_libs + 384K`, else *indexed* — only
  the 4-byte offset table (≤ 640 B) stays in RAM and `lib_get()` reads an entry into a static 8 KB
  bounce buffer per talk/portrait (one Fopen/Fseek/Fread; U6's largest entry is 4.3 KB). The four
  call sites (`talk`, `resolve_name`, the portrait decode, `portrait_for_actor`) go through the
  same `lib_get()` in both modes.
- **Music** (`music_play_seq`): the .M song, its unpacked stream and MIDI.DAT now live in static
  buffers (`g_mus_raw/g_mus_song/g_mus_dat`, 14 KB BSS). They used to be bump-allocated per play,
  which leaked and — worse — could land inside the streamed front-end region and be freed under
  the still-running sequencer ISR.
- **The chosen chargen face** is copied to `g_cc_face[56*64]` when the portrait screen completes:
  it doubles as the in-game Avatar portrait and the art it pointed into may be freed before play.

### LIFO discipline (what makes the scoped free safe)

Everything persistent is allocated **before** the first front-end mark: pools, DMA ring, video
buffers. That is why `rdr_tt.c`/`rdr_st.c` now allocate their second (flip) screen **eagerly in
rdr_init** — the old lazy allocation on the first `rdr_flip_begin` (called by the intro) would have
landed inside the streamed region and been freed while still in use. Anything that must survive the
front-end must not be pool-allocated during it (hence the static music/lib buffers).

### Console lines (real-hardware triage)

```
u6e loading (stage 1: memory)...
  ST-RAM pool KB = 2014
  Fast-RAM: none
stage 1b: audio...
  front-end: streamed (tight RAM)        | resident
stage 2: video...
```
Nothing prints after the video switch (text would scribble on the game screen); a pack failure
restores the desktop mode (`rdr_shutdown`) before printing which file it could not open and where.
`MEMINFO.BIN` in the game folder still dumps the pool numbers (`MEM …`) via `--conout 2`.

## Validation

- Hatari Falcon `--memsize 4 --ttram 0` (stock): boots to the throne room, party names resolved
  through on-demand CONVERSE reads, `MEM after_pack_free KB=634`, `libs_resident=0`; the full logos
  + intro plays streamed (INTRO.REQ `L`).
- Falcon 14 MB and 4+4 MB: `front-end: resident`, libraries resident (the ramdisk feel).
- ST 4 MB, TOS 1.04: streamed front-end plays; the ST had silently lost its front-end once the
  storm/TV/logo art pushed U6FE past what the ST pool could hold whole — grouped streaming fixes that.
- `make game-st game-tt game game-falcon` all build from the same source; the disks ship
  `U6.PAK` + `U6FE.PAK` for every target (`scripts/make-st-disks.sh`).

## DMA music: resident song cache (2026-08-16)

The last thing that streamed from disk during play on a big machine was the DMA music: the
pump refills a 32 KB ST-RAM ring from the song's `.PCM` (1–3 MB each, 21 MB for U6's twelve)
in 4 KB sector-aligned reads. `platform/platdma.c` now has a **resident song cache**:

- At the end of boot (everything else loaded), `main()` hands the backend an **arena** = the
  bigger pool's remaining free space minus a 512 KB reserve, capped at 24 MB, Fast RAM preferred
  (`plat_dma_music_cache_init`). Stock 4 MB machines have nothing left → no arena → streaming
  exactly as before. Measured: Falcon 14 MB → **7976 KB** arena; DFB1X-like → 24 MB (all songs).
- Songs are placed by a **ring allocator** (head advances, wrapping to 0 evicts whatever the new
  extent overlaps — the oldest first). Up to 16 entries; each keeps the header facts (rate,
  length, loop point).
- **Progressive fill**: the FIRST pass of a song streams from disk as usual, and every contiguous
  chunk is also copied into the song's slot. The first pass reads the body sequentially from 0,
  so at its first loop wrap the whole file has gone by → the entry is COMPLETE, the file is
  closed, and from then on the ring is refilled by `memcpy` from RAM (the DMA itself still plays
  the ST-RAM ring; that's why the arena can be Fast RAM). A later replay of a complete song opens
  no file at all. An interrupted first pass leaves a partial entry that refills from 0 next time.
- No stall on song change (unlike loading the file whole), no disk activity after a song has been
  heard once, and the same code path serves both tiers (`plat_dma_music_cached()` tells).

Validation (Hatari Falcon 14 MB, `--conout 2`, AVI audio vs the PCM): BRIT.PCM (74.7 s, loop at
37.3 s) tracks with corr ≈ 0.995 and a constant offset through the first pass AND after the wrap
(t = 90…117 s, served from RAM); `MEM music_cache KB=7976`. `SONG.REQ` accepts a playlist
(`GARGOYLE,BRIT,GARGOYLE`, 80 s each) so a replay-from-cache can be exercised headlessly
(`music from_cache=1` on the console).

## Follow-ups (not done)

- Finer streaming granularity (per scene inside INTRO) if a smaller-RAM config ever matters — the
  directory already allows it; only the group masks change.
- The 1.4 MB BSS: `g_blocked/g_sightblk/g_indoor/g_dimcache` (4 × 128 K), `g_wobj` 216 K,
  `s_stub_paper` 64 K (core packs only) are static and could become pool allocations sized per game.
- SAVE.000 template stays a whole-file load when present (optional dev file).
- The front-end's own songs (BOOTUP/ULTIMA/CREATE/INTRO) play before the arena exists, so they
  stream once and are not cached; if a "return to the menu" ever lands, allocate the arena
  before the front-end on the resident tier.
- PCM quality/size knobs already exist and are header-driven (`make m2pcm PCM_RATE=25033`
  renders 25 kHz; the backend programs the STE mode bits / Falcon prescaler from the header) —
  the shipped default stays 12517 Hz 8-bit mono (21 MB set).
