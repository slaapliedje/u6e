# u6e — Ultima VI engine for Atari (clean-room remake)

A from-scratch, clean-room reimplementation of the **Ultima VI** game engine
for the Atari line — and, because the three games share one engine lineage,
also **The Savage Empire** and **Martian Dreams**. Written in C for the
Motorola 68000 (big-endian), it scales from a base 68000 ST up through STE,
TT030, Falcon030, and 68000 + graphics-card setups from a single source tree
and a single runtime hardware-detection path.

Original game data is **user-provided** (from your own DOS/ST copy). We ship no
copyrighted assets — see [refdata/README.md](refdata/README.md).

## Project goals
1. One engine, all three games (data + small per-game rule deltas, not forks).
2. Full Atari hardware matrix from one detection routine.
3. Fix the original ST port's criticized 16-color palette — produce a low-color
   result that visibly beats it (host-side, build-time quantization).

## Repository map
| Path            | What lives there |
|-----------------|------------------|
| [CLAUDE.md](CLAUDE.md) | Top-level project rules (clean-room, CPU, renderer, pipeline). |
| [engine/](engine/)     | Shared, hardware-independent game logic (C). |
| [render/](render/)     | Renderer interface + per-target backends (ST/STE/TT/Falcon/vcard). |
| [platform/](platform/) | The only place that calls TOS/GEMDOS/BIOS/XBIOS (memory, I/O, input, timing). |
| [dataformats/](dataformats/) | Loaders/parsers for the original game files. |
| [tools/](tools/)       | Host-side asset pipeline (runs on the dev PC, not the ST). |
| [docs/](docs/)         | Clean-room knowledge base: format/rules specs, design, milestones. |
| [routines/](routines/) | Disassembly notebook — per-routine RE notes feeding the specs. |
| [refdata/](refdata/)   | Your original game data (gitignored; never committed). |

Each subsystem has its own `CLAUDE.md` with detailed rules and decisions.

## Clean-room discipline (the core constraint)
Reference projects (Nuvie, ScummVM, U6Edit, community decoders) and format
pages may be **read** to UNDERSTAND a format/behavior and to write a spec in
[docs/](docs/). Engine code is then implemented **from our spec**, never
transcribed from their source. The /docs ↔ code boundary is the clean-room
firewall. See [docs/CLAUDE.md](docs/CLAUDE.md).

## Getting started (dev)
- **Toolchain:** `m68k-atari-mint-gcc` (GCC 15.2.0). Full setup, CFLAGS, and
  IDE/clangd configuration: [docs/design/toolchain.md](docs/design/toolchain.md).
- **IDE:** clangd, configured by [.clangd](.clangd) + [.vscode/](.vscode/).
  Install the clangd extension (recommended in `.vscode/extensions.json`).
- **Emulator:** Hatari + EmuTOS with GEMDOS HD emulation.
- **Plan:** work the milestones in
  [docs/design/milestone.md](docs/design/milestone.md) (M0 = scaffolding &
  "hello, hardware"); spec status lives in [docs/INDEX.md](docs/INDEX.md).

## Status
**M0–M2 core done; M4 gate PASSED** (2026-06-23). Toolchain + IDE configured;
the build→run→self-verify loop works. The `/platform` foundation is in
(cookie-jar detection, ST-RAM/Alt-RAM pools, GEMDOS file I/O, measured-refresh
timing). The `/dataformats` foundation decodes the original files — LZW, library
containers, MAP/CHUNKS, palette, 16×16 tiles, and **conversations** — all
validated host-side against real game data: `make mapdump` renders a recognizable
Britannia world map, and `make convspike` runs a real NPC conversation (Dupre)
end-to-end. The project's biggest risk — the conversation "usecode" — is retired
(see [docs/design/m4-usecode-gate.md](docs/design/m4-usecode-gate.md)).
**M3 + M5 done**: the U6 map renders on screen, and the **avatar walks it** —
`make run-game` runs a walkable, scrolling U6 world on an emulated TT (real TOS
3.06): arrows/WASD move the avatar, the view re-centers, water blocks movement,
turns advance. The engine is portable and headless-tested (`make enginetest`).
**M6 in progress**: the conversation VM is transcript-tested across all 200 NPCs
(`make convtest`), and the world is now **populated** — `make run-game` walks a
furnished U6 town (8329 objects decoded from the savegame, drawn over terrain).
Next M6: NPC actors + schedules, object interaction/inventory, the usecode
expression VM, save/restore → the first release tag.

```sh
make            # -> build/selftest.prg (base ST, 68000)
make check      # headless self-verify on emulated ST + TT (PASS/FAIL)
make dftest     # validate LZW + library decoders vs real /refdata files
make mapdump    # decode world -> build/worldmap.png + tilesheet.png
make convspike  # run a real NPC conversation end-to-end (M4 gate)
make run-falcon # interactive run on an emulated Falcon
```

## License

u6e is licensed under the **GNU General Public License, version 2 or later**
(`GPL-2.0-or-later`) — see [LICENSE](LICENSE). This matches **ScummVM** and **Nuvie**,
the reference implementations whose data formats and behavior we studied (read to
*understand*, never transcribed) to write our own specs — see *Clean-room discipline*
above.

The GPL covers **our** engine code and documentation only. We ship **no** original
game data: Ultima VI and its assets remain the property of their rights holders and
are user-provided at build time (see `refdata/README.md`).
