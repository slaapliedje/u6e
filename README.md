# u6e — Ultima VI for the Atari ST, TT and Falcon

A from-scratch, clean-room reimplementation of the **Ultima VI: The False Prophet** engine for
the Atari 68000 line, written in C11. One source tree builds for a plain 4 MB ST, the TT030,
the Falcon030 and VDI graphics cards, and picks its display path at run time.

You need **your own copy of the DOS Ultima VI data** (GOG sells it). This repository contains no
game data: the engine reads the original files you provide, as ScummVM/Nuvie and other engine
remakes do.

## Status

The whole game is playable from the intro to the ending:

- **Front end:** the animated intro, title menu, character creation with the gypsy's questions,
  and the gargoyle opening, using the original art and music.
- **World:** the full surface, the dungeons and the gargoyle lands. NPCs follow their daily
  schedules, and there is day and night, light sources, eclipses, moongates, the orb, ships,
  skiffs, rafts, horses, wind and ship cannons.
- **Conversations:** every NPC's conversation script runs on our own interpreter, with the
  original portraits.
- **Systems:**
  - combat, with the A)ttack crosshair, missiles, and foes that cast;
  - all 80 spells, reagents and spellbooks;
  - shrines, mantras and leveling;
  - equipment, containers, pushing, levers, doors and keys;
  - resting, food and healers;
  - joining and leaving the party.
- **Saving:** S)ave writes a ScummVM/Nuvie-format save, so a save can be loaded in either engine.
- **Music:** the original songs, played through the YM2149 sound chip on any Atari, a Roland MT-32
  or General MIDI module on the MIDI port, or pre-rendered AdLib audio through STE/TT/Falcon DMA
  sound. F1 cycles through the sources.

Speed (idle frame rate, measured in Hatari): about **13 fps on a stock TT** and **11 fps on a
stock Falcon**, both with no Fast RAM.

What is still rough is listed under [Known issues](#known-issues). This is a **test release**:
expect bugs, and please report them (see [Reporting bugs](#reporting-bugs)).

| Machine | Binary | Display |
|---|---|---|
| ST / STE / Mega ST (68000, 4 MB) | `GAMEST.PRG` | 16 colours, quantised palettes per scene and per portrait |
| TT030 | `GAME.PRG` or `GAMETT.PRG` | 256 colours (TT low) |
| Falcon030 (stock, no FPU needed) | `GAME.PRG` or `GAMEF.PRG` | 256 colours (VIDEL) |
| Graphics card (NVDI/EdDI), 68020+ | `GAME.PRG` | through the card's VDI driver |
| Graphics card on a 68000 | `GAMEVC.PRG` | through the card's VDI driver |

`GAME.PRG` is the unified 68020+ binary: it detects the TT, the Falcon or a card at start-up. No
FPU is required anywhere. A 4 MB machine works; the engine streams the intro and front-end art
when RAM is short.

## Playing it

### From a release (no PC needed)

The release zip holds the programs and `CONVERT.TOS`, and no game data.

1. Make a folder on your hard drive or CF card, with about 13 MB free.
2. Copy every file from your DOS `ULTIMA6` folder into it, then the program for your machine (see
   the table above) and `CONVERT.TOS`.
3. Run the program from the desktop. The first time, it offers to convert your files and says how
   long that takes: about 3 minutes on an 8 MHz ST, under a minute on a stock TT or Falcon. It then
   starts the game ([docs/design/first_boot_convert.md](docs/design/first_boot_convert.md)).

Skip to [Controls](#3-controls).

### 1. Build the files on a PC (for developers)

`make release` needs no game data and writes the release zip to `build/`. To build the data packs on
the PC as well:

You need `m68k-atari-mint-gcc` (see [docs/design/toolchain.md](docs/design/toolchain.md)), a host
C compiler, `make`, and optionally Hatari for testing.

```sh
mkdir -p refdata/dos/u6
cp /path/to/your/ULTIMA6/* refdata/dos/u6/      # the DOS game folder (MAP, CHUNKS, U.EXE, ...)
make world          # converts your data -> build/U6.PAK, build/U6FE.PAK, build/SAVE.000
make game game-st game-falcon                    # -> build/game.prg, gamest.prg, gamef.prg
```

Optional extras:
- **Roofs:** if ScummVM is installed, `make world` takes the roof overlay from its `ultima.dat`
  (Nuvie's add-on data, `ULTIMA_DAT=/path/to/ultima.dat`). Without it, buildings show their
  floors from outside.
- **AdLib music:** `make m2pcm` converts the AdLib songs to audio (about 20 MB of `.PCM` files
  in `build/music/`) for the DMA sound path; copy them next to the game.

### 2. Install on the Atari

Copy these into one folder on the hard drive or CF card:

- the `.PRG` for your machine;
- `U6.PAK`, `U6FE.PAK`, `SAVE.000` from `build/`;
- `CONVERSE.A`, `CONVERSE.B`, `PORTRAIT.A`, `PORTRAIT.B`, all `*.M` and `MIDI.DAT` from your DOS
  folder;
- a one-byte file `MENU.REQ` (any content), which boots to the intro and title menu.

`make st-disks-falcon` (or `-st` / `-tt`) instead builds a set of `.ST` floppy images with an
installer, `INSTALL.TOS`, for a Gotek or real floppies
([docs/design/distribution.md](docs/design/distribution.md)).

Music: with no `.PCM` files the game uses the YM2149. For an MT-32 or General MIDI module, press
F1 in the game; the choice is saved in `MIDI.CFG`.

### 3. Controls

The original's keys: arrow keys walk. The first letter of each command starts it:

- A)ttack, C)ast, T)alk, L)ook, G)et, D)rop, U)se, M)ove (push), E)quip, R)est, I)nventory, S)ave;
- B toggles combat mode.

Commands that need a direction or target take the arrow keys (or a crosshair, then Enter). Esc
cancels; with nothing to cancel, it asks whether to quit. The mouse works on the command bar, the party roster and the map.

## Known issues

- Where u6e knowingly behaves differently from the original (for example, theft is not punished
  yet), the difference is listed in [docs/rules/deviations.md](docs/rules/deviations.md).
- The Savage Empire and Martian Dreams boot and can be walked, but are not playable yet.
- Transfer a Character has been tried with real ST and DOS saves from Ultima IV and V, made by
  the games themselves, but only with new characters: a veteran's high level, or the all-zero
  karma of a real Avatar, has only been tried with test files.
- Everything so far has been tested in Hatari on stock configurations. MagiC on a 4 MB ST leaves too
  little memory for the game.
- Only the GOG/DOS release's `U.EXE` has been tested for the intro data. With another version,
  the TV and the gypsy's pour animation are left out rather than drawn wrong (`make world` says
  so).

## Reporting bugs

Open an issue with:
- the machine (or emulator and settings);
- which `.PRG` you ran;
- what you did and what you saw;
- if you can, a save (`SAVED.000`) from just before the problem.

Screenshots help. For crashes, the console output (Hatari `--conout 2`) is the most useful thing.

## For developers

- **Clean-room discipline.** ScummVM/Nuvie, community tools and format pages may be **read** to
  understand a format or behaviour and to write our own spec in [docs/](docs/). The engine is then
  written from that spec, never transcribed. Original data (art, tables, conversations, narration) is never committed
  or compiled in: anything the game needs from the original, such as the intro narration, the
  gypsy's questions, or the TV and casting tables, is read from your files when they are converted.
  The exception is about twenty short interface messages, such as "Not enough magic points.", whose
  wording matches the original's. See
  [CLAUDE.md](CLAUDE.md) and [docs/CLAUDE.md](docs/CLAUDE.md).
- **Layout:**
  - [engine/](engine/): game logic;
  - [render/](render/): the renderer and its per-machine backends, plus the game shell (`gamedemo.c`);
  - [platform/](platform/): TOS, GEMDOS and XBIOS calls;
  - [dataformats/](dataformats/): decoders for the original files;
  - [tools/](tools/): the host-side converters and tests;
  - [docs/](docs/): the specs (index in [docs/INDEX.md](docs/INDEX.md)).
- **Tests:**
  - `make check`: the headless self-test on an emulated ST and TT;
  - `make enginetest convtest cvmtest combattest`: host logic tests;
  - [tools/bot/](tools/bot/): a bot that plays the game through Hatari to find bugs
    ([docs/design/bot_harness.md](docs/design/bot_harness.md)).
- **Emulation:** Hatari with EmuTOS or real TOS images. The primary targets are the stock TT and
  Falcon, then the 4 MB ST.

## License

u6e is licensed under the **GNU General Public License, version 2 or later**
(`GPL-2.0-or-later`); see [LICENSE](LICENSE). This matches ScummVM and Nuvie, whose behaviour and
data formats we studied (read to understand, never transcribed) to write our own specs.

The GPL covers our engine code and documentation only. Ultima VI and its assets remain the
property of their rights holders and are supplied by you (see
[refdata/README.md](refdata/README.md)). Ultima is a trademark of Electronic Arts; this project is
not affiliated with or endorsed by EA or Origin Systems.
