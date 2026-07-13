# Format: Ultima VI music (`.M` files) + sound drivers

Games: U6 (SE / MD have their own `.M` sets — same engine lineage, verify later).
Implemented by: /tools (host decode + analysis: `tools/mdump.c`), later /engine + /platform (playback).
Tracked in: /docs/INDEX.md · Milestone: **M10 (audio: music)**

> Provenance
> - Derived from : decompressing the user's own `refdata/dos/u6/*.M` with our LZW decoder
>   (`dataformats/u6lzw`) and inspecting the byte stream (`tools/mdump`); READING the AdPlug
>   `u6m` player (GPL) to UNDERSTAND the command encoding — NOT transcribed.
> - References   : ScummVM `engines/ultima/nuvie/sound/` — the authoritative decoder: `midiparser_m`
>   (the `.m` parser), `mididrv_m_adlib` (OPL rendering), `mididrv_m_mt32` (the MT-32/MIDI path),
>   `song_adplug`/`u6_adplug_decoder_stream`; AdPlug `src/u6m.cpp` (AdLib player for these files);
>   the Codex of Ultima Wisdom "Ultima VI Internal Formats" page (lists the files, no byte spec).
> - Tools        : `tools/mdump` (our own: LZW-decompress + dump/scan).
> - Validated by : our `u6lzw_decompress` reproduces the declared decompressed size for every `.M`,
>   AND our own disassembler (`tools/mdump`, written from the understanding below) walks the command
>   stream byte-accurately to a CLEAN END on ALL 12 `.M` files — every byte consumed, no desync,
>   each ending on the loop/return command. Correct operand counts are what keep the walk aligned,
>   so a clean end across all files confirms the opcode+operand table. Durations (BRIT ~37 s @60 Hz,
>   STONES ~114 s) are musically plausible.
> - Confidence   : **container = confirmed**; **OPL/AdLib not MIDI = confirmed**; **opcode + operand
>   table = confirmed** (byte-accurate walk); **pitch decode = confirmed** (24-entry F-number table →
>   MIDI note; note extraction to SMF matches the disassembler + renders a correct melody, `make
>   m2mid`). Remaining probable: the FM `0x83` 11-byte operator layout + volume/vibrato scaling for
>   the OPL2→DMA synth; the instrument→MT-32/GM patch map for MIDI-out (U6ROLAND.DRV).
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

The note commands' single operand is a **packed frequency byte** (CONFIRMED — validated by note
extraction, below):
- bits 0..4 = index into a 24-entry OPL **F-number table** (below); bits 5..7 = octave.
- F-number = `((table[idx].hi & 3) << 8) | table[idx].lo`; the packed octave adds to the block:
  effective block = `((table[idx].hi >> 2) & 7) + octave` (AdLib: `hi += octave << 2`).
- Table entries 0, 8, 16 = `{0,0}` = **rest** (no pitch). The 24 entries are 3 groups of 8.
- Pitch: `freq_Hz = F-number * 49716 / 2^(20 - block)`; MIDI note = `round(69 + 12*log2(freq/440))`.

24-entry F-number table (lo, hi):
```
{00,00}{58,01}{82,01}{B0,01}{CC,01}{03,02}{41,02}{86,02}   group 0: rest, then C D E F G A B (octave-relative)
{00,00}{6A,01}{96,01}{C7,01}{E4,01}{1E,02}{5F,02}{A8,02}   group 1: rest + the between-degrees (sharps/flats)
{00,00}{47,01}{6E,01}{9A,01}{B5,01}{E9,01}{24,02}{66,02}   group 2
```
Hand-check: index 1..7 at octave 4 decode to MIDI 60,62,64,65,67,69,71 = **C4 D4 E4 F4 G4 A4 B4**,
a clean C-major scale — confirming the decode. The `0x1n`/`0x2n` forms also set the OPL key-on bit
(0xB0 bit 0x20). The `0x83` 11 bytes = 5 modulator operator regs (0x20/40/60/80/E0) + 5 carrier + 1
feedback/algorithm (0xC0) — pinned when we build the OPL2→DMA synth.

**Note extraction validated (`tools/m2mid`, `make m2mid`)**: exporting each `.M` to a Standard MIDI
File (note-on/off + the pitch above, per OPL channel → MIDI channel, 60 PPQ so 1 tick = 1/60 s) gives
note counts that EXACTLY match the disassembler (BRIT 550, STONES 727, ULTIMA 797, …) and musical
MIDI ranges (~32..98). Rendered with a Roland SC-55 soundfont via fluidsynth — playable proof the
melody/timing decode is correct. This same extraction is the core of the M10 MIDI-OUT backend
(instrument-index → MT-32/GM patch mapping still to come, from U6ROLAND.DRV).

**Timing**: the driver runs at **60 Hz**; a `0x82 <n>` halts command reading for `n` ticks
(n/60 seconds). Portamento/vibrato/volume-slide update once per tick. So the sequencer schedules on
a 60 Hz clock (integer, no FPU) regardless of the frame rate. (ScummVM `midiparser_m`: `_psecPerTick
= 16667` µs.) ScummVM's opcode names, for cross-reference: 0x0 note-off, 0x1 note-on, 0x2 set-pitch,
0x3 set-level, 0x4 set-modulation, 0x5 set-slide, 0x6 set-vibrato, 0x7 program-change, 0x81
call-subroutine, 0x82 delay, 0x83 load-instrument, 0x85 fade-out, 0x86 fade-in, 0xE loop-point,
0xF return — the exact operand counts match our disassembler on all 12 files.

## On-target-relevant details (from ScummVM cross-check — FPU-free, per M10 backends)
- **Pitch, FPU-free**: `MIDI note = NOTE_MAP[freq_byte & 0x1F] + (freq_byte >> 5) * 12`, where
  NOTE_MAP is a 32-entry semitone table (ScummVM `MidiDriver_M_MT32::MIDI_NOTE_MAP`, first entries
  0,12,14,16,17,19,21,23 = C D E F G A B). This equals our F-number→Hz→log2 derivation but needs no
  float — so the on-target sequencer precomputes/ships this table instead of doing OPL math. (The OPL
  F-number path above is still what the OPL2→DMA synth uses to program the 0xA0/0xB0 registers.)
- **Volume → velocity** (MIDI/PSG dynamics): `vel = (0x3F − (level & 0x3F)) × 3/2` from the `0x3n`
  set-level command; track per channel, apply on note-on. (`tools/m2mid` now does this.)
- **MT-32 path** (`mididrv_m_mt32`): uses the MT-32's **built-in patches via program-change, NO
  SysEx** — it needs an instrument-index → MIDI-program map (`setInstrumentAssignments`), sourced
  outside the parser (TODO: find those values / derive from U6ROLAND.DRV for authentic timbres). The
  `0x83` OPL patch bytes are IGNORED on the MT-32 path (they matter only for OPL2→DMA/AdLib).
- **Channel map**: 9 data channels → MIDI channels, avoiding the drum channel; a direct 0..8 map works.

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
