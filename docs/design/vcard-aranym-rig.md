# M8 graphics-card (vcard) validation rig: ARAnyM + fVDI + AFROS

The vcard backend (EdDI/`vq_extnd` detection → off-screen bitmap → `vro_cpyfm` present) runs *through*
GEM/VDI on a graphics card, which Hatari can't emulate. This note records the working rig used to
probe a real card's VDI and validate the backend, and the caps it reported. Everything here is host
dev-machine setup — none of it ships.

## Components (all built from source on the dev box)
- **ARAnyM** — `~/src/aranym` (github.com/aranym/aranym), `./autogen.sh && make`, SDL2, x86-64
  interpreter core (JIT is i386-only). Installed to `~/.local/bin/aranym` (1.1.0).
- **EmuTOS-ARAnyM** — `emutos-aranym-1.4.zip` from the EmuTOS SourceForge release →
  `~/.aranym/emutos-aranym.img`. (The generic 512k ROM will NOT boot ARAnyM's virtual machine.)
- **fVDI** — `~/src/fvdi` (github.com/freemint/fvdi). `cd engine && make CPU=020` → `fvdi_gnu.prg`;
  `cd drivers/aranym && make CPU=020` → `aranym.sys`. Built with our m68k-atari-mint toolchain.
- **AFROS** — `~/src/afros` (github.com/ragnar76/afros): a ready-made EmuTOS + FreeMiNT + fVDI +
  XaAES + Teradesk image. `aranym -c config` from that dir boots the whole stack. This is what made
  it work end-to-end: MiNT provides **hostfs** (`[HOSTFS] C = drive_c`, so `drive_c/` *is* C:) and
  fVDI comes up in a real graphics-card colour mode. `mintara.prg` (the MiNT kernel) is AFROS's
  `Bootstrap` — the piece a bare EmuTOS boot lacks.

## Running the probe headlessly
1. Build the probe: `make vcardtest` → `build/vcardtest.prg` (GEM app, `libgem16`).
2. Copy it into `~/src/afros/drive_c/VCARDTST.PRG`.
3. Add `run C:\VCARDTST.PRG` to `drive_c/mint/1-19-cur/xaaes/xaaes.cnf` (next to the other `run`
   lines) so XaAES auto-launches it after the AES is up.
4. Boot headless and capture:
   ```
   xvfb-run -a --server-args="-screen 0 1152x900x24" bash -c '
     cd ~/src/afros
     env SDL_VIDEODRIVER=x11 ~/.local/bin/aranym -c config > console.txt 2>&1 &
     AP=$!; sleep 68; kill -TERM $AP; ... '
   ```
5. Read the probe output from `console.txt` (the `NF_STDERR` lines).

### Gotchas that cost time (read before touching this again)
- **SDL picks wayland** under a wayland session even inside xvfb → the ARAnyM window never lands on
  the X display and screenshots are blank. Force `SDL_VIDEODRIVER=x11`.
- **Never run two ARAnyM instances at once** on the same `drive_c` — they corrupt each other (blank
  screen, no progress). Kill stale `aranym`/`Xvfb` before each run; bound ARAnyM with `timeout`.
- **hostfs writes are not flushed to the host** on a hard kill (`SIGKILL`) — the Atari sees the file
  in C:, the host `drive_c/` does not. Even `SIGTERM` didn't reliably flush. → the probe outputs via
  **`NF_STDERR`** (host console) instead of relying on the hostfs file `VCARD.OUT`.
- **`vq_extnd(owflag=1)` returns all-zeros under fVDI** (and garbage under base TOS). Do NOT use it
  for planes/format. Use `v_opnvwk` work_out (resolution, colours) + `vq_scrninfo` (true pixel
  format) + the EdDI cookie.
- The probe must **exit cleanly** (no `Crawcin`) as an XaAES autorun app, or it stalls synchronous
  autorun.

## Caps reported (AFROS default fVDI mode)
- **EdDI: PRESENT**, dispatcher `0x010F45D0` (≥1.00 ⇒ `v_opnbm` off-screen bitmaps usable).
- **Resolution 800 x 608**, `v_opnvwk` work_out[13] (colours) = **256** (the VDI colour-model cap;
  it reports 256 even in truecolour).
- fVDI boot log: `screen: planes=32 pixel-format=0 src:8, transform 8 -> 32 bpp...OK!` — the
  framebuffer is **32-bit truecolour**; fVDI converts an 8-bit source to 32bpp at blit time.

## Implications for `rdr_vcard.c`
- Detect via the **EdDI cookie** (present ⇒ card/extended VDI) + `v_opnvwk` for resolution and
  `vq_scrninfo` for the true pixel format (indexed vs truecolour, bit layout).
- Present: build an off-screen bitmap with **`v_opnbm`** in the screen's device format and
  **`vro_cpyfm`** it to the screen. On a truecolour card we expand our 8-bit indexed backbuffer to
  the card format into that bitmap (same shape as the Falcon 8→16 path); on a 256-colour card the
  source is 8-bit and the VDI palette (`vs_color`) maps indices. Composite into our own 8-bit
  backbuffer first, exactly like the other backends — never per-tile VDI ops.
- The card mode is **user-selected** (fVDI config / card ACC); we adapt to the current mode, we do
  not set it.

## Present-path findings (from bringing up rdr_vcard.c on this rig — each cost a bug)
- **libgem's VDI wrappers fault here.** `vs_color` (and likely the other `v*`/`vs*` VDI calls) BUS
  ERROR under this ABI. Drive every VDI op through the generic `vdi(VDIPB*)` trap instead: Set Color
  Representation (14), `vr_trnfm` (110), `vro_cpyfm` (109), `vs_clip` (129). AES calls (appl_init,
  graf_handle, v_opnvwk, wind_update, graf_mouse) are fine via libgem.
- **A standard-format source does not blit.** `vro_cpyfm` from an `fd_stand=1` MFDB draws nothing; you
  must `vr_trnfm` it to the card's DEVICE format first, then `vro_cpyfm` device->screen.
- **VDI "standard format" is WHOLE-BITMAP plane-sequential** — all (h*wdwidth) words of plane 0 for the
  entire bitmap, then plane 1, ... NOT word-interleaved (Atari-screen style) and NOT plane-sequential
  per line. Word-interleaved gave vertical stripes; per-line plane-sequential gave horizontal stripes;
  whole-bitmap plane-sequential rendered correctly.
- **You can't just blit to the screen under XaAES** (multitasking AES): the desktop owns it and
  redraws. `wind_update(BEG_UPDATE)` + `wind_update(BEG_MCTRL)` + `graf_mouse(M_OFF)` take the screen;
  the game runs fullscreen like the other backends.
- Setting the VDI colour table with our palette recolours the whole (shared) desktop — expected for a
  fullscreen app that owns the screen.
- Screenshot the running `vcardrender` MID-loop: run ARAnyM under `SDL_VIDEODRIVER=x11 xvfb-run`, sleep
  ~58 s, `import -window root`, then SIGTERM ARAnyM.

## Truecolour-card retest (2026-07-17)
Reran the harness with fVDI in 800x608x32 (drive_c/fvdi.sys line 264 -> `...x32`). The harness runs
fine (rdr_init OK, palette set, present loop) but **nothing blits** — the `c2p -> vr_trnfm ->
vro_cpyfm` path shows on an INDEXED card only. `vr_trnfm` keeps the bitmap 8-plane, and an 8-plane
device bitmap will not `vro_cpyfm` to a 16/24/32-bit screen (depth mismatch). A truecolour CARD needs
an explicit 8-bit-index -> device-pixel expand (like the Falcon 8->16 path), driven by `vq_scrninfo`'s
exact pixel layout — a follow-up. Workaround for a truecolour-capable card: run it in a 256-colour
mode (the validated path). Rig left back at 640x480x8.

## Running the actual game on the card
`make game-vcard` -> gamevc.prg. Stage into drive_c: `GAMEVC.PRG`, `U6.PAK` (from `make world-svm`),
`CONVERSE.A/B`, `PORTRAIT.A/B`; set the `run` line in xaaes.cnf to `run C:\GAMEVC.PRG`; boot (allow
~80 s — it loads a 2.4 MB pack). The full U6 game renders through the VDI backend on the card. (The
desktop recolours while the game runs — it owns the shared VDI colour table.)

## Status
Rig WORKING; caps captured; **rdr_vcard.c present path VALIDATED** on the card (2026-07-17) — the
256-colour test grid renders pixel-correct in 640x480x8 (`make vcardrender`), AND the **full U6 game
renders on the card** via `make game-vcard` (gamevc.prg, throne-room scene + party roster + UI).
Truecolour-card mode = a known follow-up (needs the v_opnbm device-format path, above). Remaining M8
acceptance: collapse the per-target backend binaries into one runtime-detected binary.
