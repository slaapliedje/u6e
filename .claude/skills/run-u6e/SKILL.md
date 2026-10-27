---
name: run-u6e
description: Build, run, screenshot, and drive the u6e Atari Ultima VI engine. Use when asked to launch u6e, capture a scene/screenshot of the game, render a map position, test a render/engine change in the actual game, or run its self-test/logic checks.
---

u6e is a **68000 Atari engine** — it produces an Atari `.PRG` that only runs
under an emulator. You drive it headlessly with
`.claude/skills/run-u6e/drive.sh`, which builds the game, boots it on an
**emulated Atari TT** (Hatari, TOS 3.06, fast-forward), records the screen, and
extracts **one PNG frame** so you can see what the engine drew. The two knobs
are the world **start position** (`X Y`) and a **scenario** (`--walk`,
`--combat`). There is no live keyboard — the emulator runs fast-forward and the
engine reads its scenario from sentinel files at boot.

All paths below are relative to the repo root (`u6e/`, where the `Makefile` is).

## Prerequisites

This container came pre-provisioned (Arch-based; not Ubuntu — there is no
`apt-get` step to re-run here). What `drive.sh` actually invokes:

```bash
which m68k-atari-mint-gcc hatari ffmpeg cc   # all must resolve
ls "/usr/share/hatari/TOS v3.06 (1991)(Atari)(GB)[TT].img"   # TT ROM for the game
ls refdata/dos/u6/CONVERSE.A                 # user's original U6 data (gitignored)
```

- `m68k-atari-mint-gcc` (GCC 15.2.0, big-endian 68000 cross-compiler) is
  **hand-installed**, not from a distro package — see
  `docs/design/toolchain.md` to rebuild it on another machine.
- `hatari` (emulator), `ffmpeg` (AVI→PNG), and a host `cc` are the runtime deps.
  `imagemagick` (`montage`/`compare`) is optional — only the `make m6-*` diff
  targets use it.
- **TOS ROMs** live in `/usr/share/hatari/`; **original U6 game data** lives in
  `refdata/` (user-provided, never shipped). Both must be present.

## Build

No separate step needed — `drive.sh` builds via `make` (so edits to `engine/`,
`render/`, `dataformats/` are picked up). To build the game `.PRG` by itself:

```bash
make game        # -> build/game.prg (TT/Falcon, -m68020-60)
```

## Run (agent path)

Everything goes through the driver. Screenshots land in `build/`.

```bash
# Capture the default scene (Lord British's throne room, 315,367):
.claude/skills/run-u6e/drive.sh scene -o build/scene.png

# Point the camera anywhere on the 1024x1024 surface map (U6 tile coords):
.claude/skills/run-u6e/drive.sh scene 64 32 -o build/water.png   # open water

# Drive a scenario, then capture:
.claude/skills/run-u6e/drive.sh scene 315 367 --combat 2 -o build/fight.png
.claude/skills/run-u6e/drive.sh scene 332 434 --walk 74 -o build/npc.png

# Fast checks (no emulator):
.claude/skills/run-u6e/drive.sh logic     # engine + conversation + combat unit tests
.claude/skills/run-u6e/drive.sh check     # headless TEXT self-test on emulated ST + TT
```

Then **look at the PNG** (e.g. Read `build/water.png`). A correct frame shows the
papyrus U6 border, an 11×11 map window on the left, the party roster + message
box on the right, the clock, and the command bar. At the default start (307,352)
three gargoyles stand before Lord British's throne — that is U6's real opening
battle, hatched from the throne-room egg (docs/rules/eggs.md), not a bug.

| command | what it does |
|---|---|
| `scene [X Y]` | build world at start tile `(X,Y)` (default 315 367), boot on TT, write PNG |
| `scene … --walk N` | stage `NPCWALK.BIN=N`: advance N game-turns (clock ticks, NPCs walk schedules) before capture |
| `scene … --combat 1\|2` | stage `COMBAT.BIN`: 1 = avatar kills an adjacent acid slug; 2 = a hostile slug assaults the party |
| `scene … --vbls N` | emulator run length (default 5000); raise if the frame looks unfinished |
| `scene … -o FILE` | output PNG path (default `build/scene.png`) |
| `logic` | host-side `make enginetest convtest cvmtest combattest savetest equiptest` — instant, no emulator |
| `check` | `make check` — boots `selftest.prg` on ST + TT, mirrors the VT-52 console; ends `RESULT: ALL PASS` |

A `scene` capture takes **~2–3 minutes** (the world rebuild decodes the whole U6
map, then the emulator runs thousands of fast-forwarded VBLs). Run it in the
background or with a ≥240s timeout. `logic` is instant; `check` is ~1 min.

## Run (human path)

```bash
make run-game    # interactive: walk the world on an emulated TT (arrows/WASD move, S saves)
```

Opens a live Hatari window — **useless headless** (no display); it's for a
developer at a desk. Use `drive.sh scene` instead.

## Gotchas

- **It only runs in the emulator.** The target is a big-endian Motorola 68000
  Atari binary. There is no native Linux build of the game; every visual check
  goes through Hatari.
- **No live input headless.** Fast-forward capture sends no keystrokes. Scenes
  are driven by *where you start* (`X Y`) and *1-byte sentinel files* the engine
  reads at boot (`NPCWALK.BIN`, `COMBAT.BIN`) — that's what `--walk`/`--combat`
  stage. `drive.sh` clears stale sentinels each run.
- **The roster is the real saved party (Avatar/Dupre/Shamino/Iolo), message box
  empty.** (The old "Jaana joins thy party!" boot demo is gone — the party now
  comes from the pack's saved-party list. `TALKTEST.REQ` is the hook if a capture
  needs a scripted conversation.)
- **The captured frame can be MID-REDRAW.** Hatari is hard-killed and the last
  decoded AVI frame kept, so occasionally the map has un-composed black regions
  or the clock/MP line shows two overprinted values. That is the harness, not
  your change — re-run the capture (or use a `SHOT.REQ` pixel dump) before
  chasing a "corruption" bug.
- **`build/SAVE.000` is a build artifact, not a stray file.** `make world`
  synthesizes it (tools/mksave: the pristine new-game save in ScummVM layout)
  and builds the pack FROM it; it is the template S)ave patches, and it ships.
  Don't delete it. The old auto-walk-and-save demo only runs behind a
  `SAVEDEMO.REQ` sentinel (`make save-engine`), so captures stay at `(X,Y)`.
  `SAVED.000` (a written save) makes "Journey Onward" resume it — `drive.sh`
  clears it before each `scene`.
- **The screen is a 320×200 UI letterboxed into TT-low 320×480**, 2×
  vertically-doubled — that's why the captured PNG is 640×480-ish with the game
  centered. Expected, not a stretch bug.
- **`X Y` are U6 surface tile coords, 0–1023.** Useful spots: `315 367` = Lord
  British's throne; `64 32` = open water (animated water tiles); `332 434` = an
  NPC commute route (pair with `--walk`).
- **Capture extracts the *last* AVI frame.** Hatari is force-quit with no AVI
  index, so `ffmpeg -update 1` keeps the final decoded frame = the held UI.
  Don't expect a mid-boot frame.

## Troubleshooting

- **`no AVI produced — emulator failed to boot`**: the TT ROM path is wrong.
  `ls "/usr/share/hatari/TOS v3.06 (1991)(Atari)(GB)[TT].img"`; if it's
  elsewhere, `export TOS_TT=/path/to/tt-tos.img` before running `drive.sh`.
- **Blank / black map window in the PNG**: the engine was still loading when the
  frame was grabbed. Re-run with `--vbls 7000` (or higher).
- **`drive.sh scene` "times out" around 2 min**: it isn't hung — the first
  build+boot legitimately needs ~3 min. Give it a longer timeout or background
  it; subsequent runs reuse the build and are faster.
- **`world/gamedata build failed`**: original game data is missing. Confirm
  `refdata/dos/u6/` (or `refdata/st/u6/`) holds the user's U6 files; the
  pipeline cannot synthesize them.
