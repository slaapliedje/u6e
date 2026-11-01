# /refdata — original game data (USER-PROVIDED, NEVER COMMITTED)

This directory holds **your own** copies of the original game data, used at dev
time for format analysis and as input to the host-side asset pipeline (/tools).

## CLEAN-ROOM FIREWALL — read this
- Everything under /refdata EXCEPT this README is **gitignored** (see
  /.gitignore: `/refdata/` ignored, `!/refdata/README.md` kept). Do NOT force-add
  game files. We ship NO copyrighted assets — ever.
- The original ST executable, disk images, and data files are analyzed locally
  only. Findings flow OUTWARD as our own specs in /docs and notes in /routines;
  the binary/data themselves never enter git.
- These files come from **your own** purchased/owned DOS or Atari ST copy, the
  same way Nuvie/ScummVM require the user to supply original data.

## Expected layout
Drop each game's original files under the matching folder. The exact paths are
referenced by each game's asset manifest (consumed by /engine GameDef, written
by /tools — see tools/CLAUDE.md). Update the manifest if your layout differs.

```
refdata/
├── dos/                      # DOS/VGA releases (the richest 256-color art)
│   ├── u6/                   # Ultima VI: The False Prophet
│   ├── savage/               # The Savage Empire
│   └── martian/              # Martian Dreams
└── st/                       # Atari ST releases (for port comparison / palette study)
    └── u6/                   # original U6 ST binary + data (the "lazy palette" reference)
```

(The `dos/` and `st/` folders already exist; create the per-game subfolders as
you add data.)

## Why both DOS and ST?
- **dos/** is the primary asset source: 256-color VGA art is what we convert to
  every target (see CLAUDE.md asset pipeline). Source data is little-endian.
- **st/** is the comparison baseline: the original ST port is the thing our
  low-color pipeline aims to BEAT (the documented EGA-palette criticism, see
  engine/CLAUDE.md "Palette / asset quality"). The ST binary is also the
  disassembly subject for /routines.

## Notes
- Source files are little-endian; the engine runs big-endian on 68000. All
  conversion happens host-side in /tools (see dataformats/CLAUDE.md).
- Save files may embed copyrighted content and are also gitignored
  (`*.sav`, `saves/`).
