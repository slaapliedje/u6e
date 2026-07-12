# CLAUDE.md — Platform Abstraction Subsystem

## Scope
The ONLY place in the project that talks to TOS/GEMDOS/BIOS/XBIOS directly
(except /render, which owns video specifically). Provides hardware-agnostic
services to /engine: hardware detection, memory, file I/O, input, timing.
The engine NEVER calls a TOS trap directly — it calls platform_* functions.

## Golden rules
- State the exact call (name + opcode/trap) used in any code.
- Big-endian 68000; this layer also owns LE->host conversion at the file-I/O
  boundary where raw bytes enter (cooperating with /dataformats specs).
- No FPU anywhere (base ST has none; TT often lacks the 68882; Falcon needs a
  mod). Integer/fixed-point only.
- Guard every "new" call behind a version/cookie check and degrade
  gracefully to the base-ST path. The base 68000 ST is the floor.

## Hardware detection (home of the detection routine)
- Cookie jar: `_VDO` (video class: ST/STE/TT/Falcon), `_CPU`, `_FPU`
  (expect/assume 0), `_MCH` (machine), `EdDI`/`NOVA`/`fVDI` (extended VDI /
  graphics card present). (Video specifics consumed by /render.)
- GEMDOS version via `Sversion()` to gate Mxalloc (see memory).
- Expose a single platform_caps struct the rest of the app reads
  (machine type, RAM kinds available, video class, has-blitter, etc.).

## Memory management (VERIFIED — important gotchas)
Two RAM types matter on our higher targets:
- ST-RAM ("dual-purpose"): shared with custom chips. ALL chip-accessed
  buffers MUST live here: video/screen memory (Shifter/VIDEL), DMA sound
  buffers, DMA disk I/O buffers.
- TT-RAM / Alt-RAM / Fast-RAM ("single-purpose", CPU-only): much faster for
  CPU-bound work (burst-mode cache fills). NOT present on base ST/STE/Falcon
  (only via accelerator cards on Falcon). Cannot be touched by custom chips.

### Rules
- At startup: Mshrink to release the giant default block TOS hands us, then
  allocate our own big pools (TOS hands the whole machine to the program and
  relies on us returning what we don't use).
- TOS 1.0-era Malloc is buggy with many small blocks. So: allocate a FEW
  LARGE pools and sub-allocate ourselves (bump allocator + a fixed/dynamic
  split). Min block size >= 16KB per period guidance.
- Allocation calls:
  - `Sversion()` >= 0x1900 (GEMDOS 0.19) => Mxalloc available.
  - `Mxalloc(amount, mode)` (opcode 68): mode bits 0-2 select RAM:
      0=ST-RAM only, 1=Alt/TT-RAM only, 2=either pref ST, 3=either pref Alt.
    Use the Mxmask() trick before using protection bits (some GEMDOS versions
    crash with protection bits set).
  - Fallback `Malloc(amount)` (opcode 72) when Mxalloc absent — but note
    Malloc obeys the program's file-flag RAM type, which is dangerous for
    screen buffers. Prefer Mxalloc where present.
  - `Mfree`, `Mshrink` for release/resize.
- CRITICAL allocation policy for our renderer/engine:
  - Screen/backbuffer that the Shifter/VIDEL displays => Mxalloc mode 0
    (ST-RAM ONLY). Falcon screen buffers also ST-RAM (use VgetSize for size).
  - Working/CPU-only buffers (decompression scratch, decoded maps, off-screen
    composition that we copy from, music caches) => Mxalloc mode 3 (prefer
    Alt/TT-RAM) for the big CPU speedup on TT, falling back to ST-RAM
    automatically on machines without Alt-RAM.
  - When Mxalloc(amount,3) returns an address, CHECK it: if it's not in the
    Alt-RAM range (e.g. < 0xE00000 on relevant machines) we actually got
    ST-RAM — record that so we DON'T then try to satisfy a "must be ST-RAM"
    request out of the same assumption. (Real-world pattern from period code.)
- platform memory API to expose to engine:
    plat_mem_init()      -> Mshrink + grab big pools (ST + Alt where avail)
    plat_alloc_chip(n)   -> ST-RAM (screen/DMA-safe)
    plat_alloc_fast(n)   -> Alt/TT-RAM preferred, ST-RAM fallback
    plat_mem_reset(mark) / bump-style scratch for per-scene churn
  Engine asks by INTENT (chip vs fast), never by trap.

## File I/O (GEMDOS)
- Open/read/write/seek/close via GEMDOS:
    Fopen (0x3D), Fread (0x3F), Fwrite (0x40), Fseek (0x42), Fclose (0x3E),
    Fcreate (0x3C); directory via Fsfirst/Fsnext as needed.
- DMA disk transfer buffers must be in ST-RAM (chip-accessible). If reading
  into a Fast-RAM destination, read into an ST-RAM bounce buffer then copy,
  OR let GEMDOS handle it (GEMDOS buffers internally) — but verify large-read
  behavior on TT. <<FILL IN: confirm whether our toolchain's Fread into
  Alt-RAM is safe on target TOS, else bounce-buffer.>>
- Read big, parse in memory: pull whole data files into a pool, hand the
  buffer to /dataformats. Avoid many small reads.
- Path handling: TOS uses '\' and drive letters; keep all original-asset
  paths in config via the GameDef manifest (set by /tools output). EmuTOS +
  GEMDOS hard-disk emulation under Hatari for dev.

## Input
- Keyboard: BIOS `Bconin`/`Bconstat` (con dev) for blocking-ish; or
  `Kbshift` for modifier state; IKBD/system-variable polling for game-style
  non-blocking input. Decide one model and wrap it. <<FILL IN: confirm input
  model — likely poll IKBD/keyboard buffer each loop for responsiveness.>>
- Mouse: via the IKBD / line-A or VDI mouse vector if GEM is up. Since we run
  fullscreen non-GEM in-game, prefer reading IKBD packets directly through a
  platform wrapper. <<FILL IN: GEM-up menus vs fullscreen game input split.>>
- Expose plat_input_poll() -> normalized events to /engine.

## Timing
- VBL (50/60Hz, mono 70Hz) is the natural frame heartbeat. Hook the VBL
  queue or read the frame counter via the system variable for pacing.
- `_hz_200` (200Hz system timer) system variable for finer-grained timing /
  game clock; or Timer-C. Read via Supexec for protected vars.
- Engine wants: a monotonic millisecond-ish tick + a frame signal. Expose
    plat_ticks_ms()  (derived from 200Hz timer)
    plat_wait_vbl()  / plat_frame_ready()
  Keep simulation on game-time ticks, present on VBL (per /engine main loop).
- Different VBL rates across targets/monitors: derive real rate at startup,
  don't hardcode 50Hz.

## GEM / TOS coexistence
- In-game runs effectively fullscreen, non-GEM, for speed and full screen
  control. On entry: save current resolution/palette/screen base; on exit:
  restore them (recall Setscreen/VsetMode/EsetShift reinit the VDI but NOT
  the AES — see /render). 
- If we present any GEM UI (file selector for locating original assets,
  config), do it BEFORE switching to game mode, or restore mode around it.
- Always Mshrink early and clean up (Mfree, restore vectors, restore screen)
  on exit so we're a well-behaved app.

## Testing
- Run under Hatari with EmuTOS + GEMDOS HD emulation; configure ST/STE/TT/
  Falcon machine variants to exercise each detection branch and the
  ST-RAM/Alt-RAM allocation paths.
- Add a "platform self-test" build that prints detected caps (machine, RAM
  kinds + sizes, video class, blitter, GEMDOS ver) — verify across emulated
  machines and on real hardware later.
- Hatari --trace gemdos,xbios to verify which calls we actually issue.

## Decisions log (platform)
<<FILL IN>>
- Fread-into-Alt-RAM safety vs. bounce buffer policy.
- Input model (IKBD direct vs BIOS con) + GEM-menu/fullscreen split.
- Timer source for game clock (200Hz var vs Timer-C).

