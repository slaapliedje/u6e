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

## Status
Rig WORKING + caps captured (2026-07-17). Next: implement `rdr_vcard.c` from these caps and validate
it here (change `drive_c/fvdi.sys` to an 8-bit mode too, to cover the 256-colour card path / goal #3).
