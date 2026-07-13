# Format: Ultima VI music (`.M` files) + sound drivers

Games: U6 (SE / MD have their own `.M` sets — same engine lineage, verify later).
Implemented by: /tools (host decode + analysis: `tools/mdump.c`), later /engine + /platform (playback).
Tracked in: /docs/INDEX.md · Milestone: **M10 (audio: music)**

> Provenance
> - Derived from : decompressing the user's own `refdata/dos/u6/*.M` with our LZW decoder
>   (`dataformats/u6lzw`) and inspecting the byte stream (`tools/mdump`); READING the AdPlug
>   `u6m` player (GPL) to UNDERSTAND the command encoding — NOT transcribed.
> - References   : AdPlug `src/u6m.cpp` (an AdLib player for exactly these files); the Codex of
>   Ultima Wisdom "Ultima VI Internal Formats" page (lists the files, no byte spec).
> - Tools        : `tools/mdump` (our own: LZW-decompress + dump/scan).
> - Validated by : our `u6lzw_decompress` reproduces the declared decompressed size for every `.M`,
>   AND our own disassembler (`tools/mdump`, written from the understanding below) walks the command
>   stream byte-accurately to a CLEAN END on ALL 12 `.M` files — every byte consumed, no desync,
>   each ending on the loop/return command. Correct operand counts are what keep the walk aligned,
>   so a clean end across all files confirms the opcode+operand table. Durations (BRIT ~37 s @60 Hz,
>   STONES ~114 s) are musically plausible.
> - Confidence   : **container = confirmed**; **OPL/AdLib not MIDI = confirmed**; **opcode +
>   operand table = confirmed** (byte-accurate walk). Pitch table + operand *semantics* for the
>   playback backends (F-number→Hz, 11-byte operator layout) = probable, pinned when we synthesize.
> - Last updated : 2026-07-13

## THE KEY FACT (drives the whole M10 approach)
U6's in-game music is **AdLib / OPL2 (Yamaha YM3812) FM synthesis**, **NOT MIDI**. The `.M`
stream is a compact per-channel FM command language that targets the OPL2 register model
directly (frequency words, operator "mute factors" = attenuation, FM instrument patches). There
is **no MIDI event stream and no MT-32/GM sequence inside `.M`**. So the milestone's original
"XMIDI → MIDI-OUT" premise does not hold as-is; see docs/design/milestone.md M10 and the options
recorded there. The notes ARE note-level (note-on/off + frequency + instrument), so a note
sequence CAN be recovered for a MIDI-out reinterpretation, but the native fidelity is FM.

## Container (confirmed) — U6 LZW
Each `.M` is a standard U6 LZW container (docs/formats/lzw.md), decoded by `dataformats/u6lzw`:
- Bytes 0..1  : decompressed size, **little-endian 16-bit** (BRIT.M = 0x0687 = 1671).
- Bytes 2..3  : 0x0000.
- Bytes 4..   : the LZW bitstream. First code is 0x100 (reset/clear) — byte4=0x00 with
  byte5 bit0 set gives `0x00 | (1<<8) = 0x100`. 9→12-bit codes, 0x100 = reset, 0x101 = end.
- Sizes observed (file → decompressed): BRIT 982→1671, INTRO 544→1214, STONES 1256→2849,
  ULTIMA 1704→2633, etc. `tools/mdump FILE.M` prints these + a scan.

Note: the source is little-endian; the engine is big-endian 68000, so the 16-bit size + any
16-bit OPL frequency words need explicit LE reads (u6lzw already handles the size header).

## Decompressed stream — OPL2 command language (CONFIRMED opcode + operand table)
**High nibble = opcode class, low nibble = OPL channel (0..8)** — nine 2-operator FM voices. Every
`.M` opens with a setup block (assign instrument, carrier/modulator volume, vibrato per channel),
then the body: `DELAY` commands advance time and, between delays, note commands retrigger the
voices. A `.M` typically drives all ~8 melodic voices in parallel (polyphonic FM).

| Command (byte)     | Operand bytes   | Meaning                                                  |
|--------------------|-----------------|---------------------------------------------------------|
| `0x0n`             | 1 (freq)        | set frequency, note OFF on channel n                    |
| `0x1n`             | 1 (freq)        | note OFF (old) + note ON (new) — the workhorse retrigger|
| `0x2n`             | 1 (freq)        | set frequency, note ON on channel n                     |
| `0x3n`             | 1 (vol)         | carrier "mute factor" (attenuation/volume), channel n   |
| `0x4n`             | 1 (vol)         | modulator mute factor, channel n                        |
| `0x5n`             | 1 (delta)       | portamento — signed pitch slide per 60 Hz frame         |
| `0x6n`             | 1 (amp<<4\|mul) | vibrato, channel n                                      |
| `0x7n`             | 1 (instr)       | assign instrument index (0..8) to channel n             |
| `0x81`             | 3 (rep,lo,hi)   | BRANCH to subsong: rep repeats, 16-bit LE offset; push  |
| `0x82`             | 1 (ticks)       | **DELAY** ticks before the next command — 60 Hz clock   |
| `0x83`             | 1+11 (idx,ops)  | DEFINE instrument idx = 11 OPL operator bytes           |
| `0x85`             | 1 (ch<<4\|dly)  | volume slide up                                         |
| `0x86`             | 1 (ch<<4\|dly)  | volume slide down                                       |
| `0xEn`             | 0               | set LOOP point = current stream position                |
| `0xFn`             | 0               | RETURN from subsong / at top level jump to loop (end)   |
| `0x80,0x84,0x87..` | (0)             | no handler (ignored)                                    |
| `0x9n..0xDn`       | —               | undefined (never occur in the 12 U6 `.M` files)         |

The note commands' single operand is a **packed frequency byte**: bits 0..4 = index into a 24-entry
F-number table, bits 5..7 = octave/block; the OPL "note-on" key bit (0xB0 bit 0x20) is set for the
`0x1n`/`0x2n` (on) forms. The 24-entry table + F-number→Hz math are needed to recover pitch (MIDI
note / YM2149 period) for the non-OPL backends — pinned when we build note extraction. The `0x83`
11 bytes = 5 modulator operator regs (0x20/40/60/80/E0) + 5 carrier + 1 feedback/algorithm (0xC0).

**Timing**: the driver runs at **60 Hz**; a `0x82 <n>` halts command reading for `n` ticks
(n/60 seconds). Portamento/vibrato/volume-slide update once per tick. So the sequencer schedules on
a 60 Hz clock (integer, no FPU) regardless of the frame rate.

## The `.DRV` device drivers (context, not decoded)
`U6ADLIB.DRV` (OPL2/AdLib), `U6ROLAND.DRV` (Roland MT-32), `U6TANDY/U6CMS/U6COVOX/U6INNOVA`,
`U6TMUS`, plus `U6CGA/EGA/MCGA.DRV` (video). These are Origin/Miles AIL device drivers — the
runtime binds one and feeds it the `.M` stream. We do NOT run the DOS drivers; the `.M` FM data
is authored for the OPL2 voice model, so the AdLib driver is the reference sound. (Whether the
Roland driver plays these same `.M` files by re-mapping FM→MT-32, or U6 simply has no MT-32
in-game music, is unconfirmed — not needed for our playback path.)

## Open questions
- Exact opcode operand semantics (frequency word layout: F-number + block/octave; attenuation
  scale; instrument-define 11-byte operator layout; loop/branch stack).
- Tick rate / tempo: what real-time rate does `0x82 <delay>` count in (OPL timer? a fixed Hz)?
- The unmapped `0xA..`/`0xB..` opcodes in the note section.
- Per-game deltas for SE / MD `.M` files.

## Status
IN PROGRESS — container + "it's OPL not MIDI" CONFIRMED; opcode table PROBABLE. Next: a full
host disassembler (`tools/mdump` → command-level) to pin the table, THEN pick the playback path
(see milestone M10) and write the engine sequencer + platform audio backend from this spec.
