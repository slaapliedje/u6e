# Recording a showcase video (Hatari + MT-32)

How to record u6e running on an emulated **stock 4 MB Falcon** (no Fast RAM, no FPU — the primary
target, so the speed shown is honest) with **MT-32 music**, and turn it into a YouTube-ready MP4.
Tools: `tools/video/`. Nothing here ships; recordings contain the user's own game data and stay local.

> Provenance
> - Derived from : the u7port session's method for its Ultima 7 video (2026-10-29/30: Hatari AVI +
>   MIDI capture + libmt32emu render + content-anchored sync); adapted for u6e.
> - Tools        : tools/video/{midirec.c, smf2mt32.c, vidrun.py, menudet.py, finish.py, showcase.tl,
>   hatari-midi.cfg}
> - Validated by  : a 4 m 26 s showcase (boot, full intro, menu with pointer, conversation, walking),
>   ~/Videos/u6e/u6e-falcon-mt32.mp4; the intro anchor gave the same offset within 21 ms on three runs.
> - Last updated : 2026-10-30

## 1. Pipeline

1. **Private MIDI port.** `midirec out.mid t0file` opens an ALSA sequencer client `u6e-rec` with one
   writable port and records everything sent to it (1 tick = 1 ms, type-0 SMF), until SIGINT.
   Hatari's PortMidi output is pointed at that port (`hatari-midi.cfg`: `sMidiOutPortName = u6e-rec in`),
   so u6e's music never touches the shared *Midi Through* port and cannot mix with another
   recording (or reach the real MT-32 on the UM-ONE).
2. **Record.** `HDDIR=<drive dir> tools/video/vidrun.py <timeline> <outdir>` starts the recorder,
   then Hatari in **real time** (`--fast-forward off --frameskips 0`) with `--avirecord
   --avi-vcodec png --png-level 1`, `--sound 48000`, `--cmd-fifo`, and plays the timeline into the
   FIFO. Stock-Falcon flags: `--machine falcon --memsize 4 --ttram 0 --dsp none --monitor vga`,
   TOS 4.04, `--auto C:\GAMEF.PRG`, drives A/B off, statusbar off.
3. **Render.** `smf2mt32 music.mid music.wav ~/.local/share/mt32-roms` renders the capture through
   libmt32emu (MT-32 control 1.07 + PCM ROMs) at 32 kHz.
4. **Sync + encode.** `tools/video/finish.py <outdir> out.mp4` finds the offset from the content
   (§3), trims the music, and encodes: 640x480 → 1280x960 nearest-neighbour, padded to 1440x1080,
   H.264 CRF 16 preset slow at 60 fps, AAC 320 kb/s, `+faststart`.

## 2. The drive folder

`HDDIR` is a folder used as `C:`: `GAMEF.PRG`, `U6.PAK`, `U6FE.PAK`, `CONVERSE.A/B`, `PORTRAIT.A/B`,
`SAVE.000` (the start position for Journey Onward), all `*.M` songs + `MIDI.DAT` from the user's
data, `MIDI.CFG` containing `1` (MIDI out, MT-32), and **`MENU.REQ`** containing `1` — without it the
engine skips the intro and menu and boots straight into play (the installed copy ships it).
Keep recordings on disk (`~/Videos/u6e/`), not `/tmp` (tmpfs): the PNG AVI is ~2.5 MB/s.

## 3. Sync

The MIDI file's clock is wall time from the recorder's start; the AVI's clock is emulated frames.
Hatari runs in real time, so the two only differ by a constant, but that constant is **not** the gap
between starting the two programs (the AVI starts ~0.2 s after launch here; on u7port the emulated
clock ran 1.6 s ahead during boot). `finish.py` measures it from content:
- **intro anchor (sets the offset)**: the start of the fade to black before the intro against the
  first MIDI event — the intro song is cued as that fade begins (0.698 / 0.711 / 0.719 s on three runs);
- **menu anchor (checks drift)**: the first frame of the menu's fade-in (detected by `menudet`: blue
  title, black gap, grey panel) against the menu song's all-notes-off burst (CC 123). The menu song
  starts 0-0.12 s after its fade begins (its .M is loaded from disk first), so a residual up to
  ~+0.12 s is normal; anything larger or negative means real drift.
The AVI header says 60.0002 fps (the game's VGA mode); u7port saw the TOS desktop at 50 Hz stamped
as 60 — not an issue here because both anchors lie after the game sets its own mode, but check the
residual if the timeline ever records the desktop.
u6e in MT-32 mode makes no PSG/DMA sound, so the AVI's own audio track is silent and is not mixed.

## 4. Timelines

One command per line, `<seconds> <op> [args]` (sorted per segment by the driver):
`key <ST scancode> [n] [gap]`, `mouse <dx> <dy>` (Alt+arrows, TOS's keyboard mouse), `click`
(Alt+Insert), `shot`, `quit`, and `waitmenu <every> <scancode> <timeout>`, which presses a key every
few seconds to turn the intro's pages until the menu is on screen; later times count from that
moment. `showcase.tl`: full intro (a page every 10 s), pointer moves over the menu, `J`ourney
Onward, `T`alk to Dupre (name / job / bye typed), a walk south then east.
- Use **scancodes**, not `char`: Hatari's ASCII mapping is unreliable under the SDL dummy driver.
- The game drains a burst of keys to the newest one during play (text entry keeps every key), so
  space keys ≥ 0.5 s apart.
- The keyboard `T` talks to an ADJACENT actor (the first one found, often a party member) — see
  docs/rules/deviations.md if that changes.
- Hatari ignores SIGTERM with the dummy drivers: end with `quit` (`--confirm-quit off`), else kill -9.

## 5. Performance reading

The recording is real time on a stock Falcon: boot to intro ≈ 15 s, menu → play ≈ 2 s, one walking
step per key press with the full scroll + NPC update visible frame by frame. Count frames in the
AVI (60 per second) to time anything.

## 6. Found while making the first video (2026-10-30)

After the intro had played to its end, the game took no keyboard input: the intro's moongate-rise
art binder stored the gate width in `g_gw`, the same file-scope static the moongate walk-in uses as
its state (C merges two `static int g_gw;` tentative definitions silently), so play believed a gate
walk was running and dropped every key. Renamed the intro's to `g_gate_w`. Real hardware was
affected the same way whenever the intro was watched to the end.
