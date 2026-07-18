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
> - Tools        : `tools/mdump` (LZW-decompress + disassemble), `tools/m2mid` (note stream → SMF),
>   `tools/m2wav` (our own software OPL2/YM3812 FM synth → WAV, the M10 OPL2→DMA de-risk).
> - Validated by : our `u6lzw_decompress` reproduces the declared decompressed size for every `.M`,
>   AND our own disassembler (`tools/mdump`, written from the understanding below) walks the command
>   stream byte-accurately to a CLEAN END on ALL 12 `.M` files — every byte consumed, no desync,
>   each ending on the loop/return command. Correct operand counts are what keep the walk aligned,
>   so a clean end across all files confirms the opcode+operand table. Durations (BRIT ~37 s @60 Hz,
>   STONES ~114 s) are musically plausible.
> - Confidence   : **container = confirmed**; **OPL/AdLib not MIDI = confirmed**; **opcode + operand
>   table = confirmed** (byte-accurate walk); **pitch decode = confirmed** (24-entry F-number table →
>   MIDI note; note extraction to SMF matches the disassembler + renders a correct melody, `make
>   m2mid`); **`0x83` 11-byte operator layout = confirmed** (grouped mod/car, byte-role evidence +
>   rendered to correct-pitch audio by our own OPL2 synth `tools/m2wav`). Remaining probable:
>   portamento/vibrato/fade dynamics scaling; the instrument→MT-32 map is confirmed (MIDI.DAT).
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
(0xB0 bit 0x20).

### `0x83` instrument = 11 OPL operator bytes (CONFIRMED — grouped mod/car layout)
After the 1-byte instrument index, the 11 bytes are **grouped by operator** (all 5 modulator regs,
then all 5 carrier regs, then the channel byte) — NOT interleaved (mod,car,mod,car…):

| byte | register | field(s)                                   |
|------|----------|--------------------------------------------|
| 0    | mod 0x20 | AM(7) VIB(6) EG-type/sustain(5) KSR(4) MULT(3-0) |
| 1    | mod 0x40 | KSL(7-6) Total-Level(5-0)                   |
| 2    | mod 0x60 | Attack(7-4) Decay(3-0)                      |
| 3    | mod 0x80 | Sustain-Level(7-4) Release(3-0)            |
| 4    | mod 0xE0 | Waveform-Select(1-0)                        |
| 5    | car 0x20 | (same fields as byte 0, carrier operator)  |
| 6    | car 0x40 | KSL / Total-Level                          |
| 7    | car 0x60 | Attack / Decay                             |
| 8    | car 0x80 | Sustain-Level / Release                    |
| 9    | car 0xE0 | Waveform-Select                            |
| 10   | ch 0xC0  | Feedback(3-1) Connection(0): 0=FM 1=AM     |

How the layout was pinned (byte-role evidence across all `.M`, not transcription): in the grouped
reading, byte-4 and byte-9 (the two waveform-select bytes) are **always 0/1/2** across every U6
instrument — the only valid OPL2 waveforms — while any interleaved reading puts values like 0x55/0x5e
there (invalid). Independently, the carrier `0x40` Total-Level (byte 6) is consistently 0/small (the
loud, audible operator) and the modulator `0x40` (byte 1) is attenuated (shapes the FM index) —
textbook 2-operator FM. Example (BRIT, one instrument): `61 1b 62 56 00 | 21 00 84 55 00 | 0e` =
modulator sine w/ TL 27, carrier sine at full level, feedback 7 / FM. Validated further by rendering
each `.M` through our own software OPL2 synth (`tools/m2wav`) to correct-pitch, musical audio.

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
  SysEx**. The instrument map is the authentic Origin data in **`MIDI.DAT`** (a plain-TEXT file in the
  U6 data — no decode needed; ScummVM ships the same values as its `DEFAULT_MT32_INSTRUMENT_MAPPING`
  fallback, 0-based). The `0x83` OPL patch bytes are IGNORED on the MT-32 path (OPL2→DMA/AdLib only).

### MIDI.DAT — the MT-32 instrument map (CONFIRMED, from the user's own data)
Self-documenting text: a header, then one line per song `N: Name  (ch/prog,ch/prog,...)`, where
`ch` = MIDI channel 1..16 and `prog` = MT-32 program 1..128. The pairs are indexed by **instrument
number**: the `.M`'s `0x7n` (program-change) sets an OPL channel's instrument index → `pair[index]`
= (MIDI channel, MT-32 program). Songs are listed in a fixed order keyed `1..9,0,-,=` → the 12 `.M`
files; the full U6 table:

| .M        | song                      | (channel/program) pairs                 |
|-----------|---------------------------|-----------------------------------------|
| ULTIMA.M  | Ultima Theme              | 2/26, 3/51, 4/25, 5/28                  |
| BOOTUP.M  | Bootup                    | 2/38, 3/39                              |
| INTRO.M   | Introduction              | 2/62, 3/61, 4/56, 5/118, 6/118          |
| CREATE.M  | Character Creation        | 2/7, 3/2, 4/34                          |
| FOREST.M  | Forest                    | 2/60, 3/61                              |
| HORNPIPE.M| Captain Johne's Hornpipe  | 2/88, 3/61, 4/60                        |
| ENGAGE.M  | Engagement and Melee      | 2/50, 3/27, 4/19, 5/17                  |
| STONES.M  | Stones                    | 2/7, 3/33                               |
| DUNGEON.M | Dungeon                   | 2/38, 3/114, 4/56                       |
| BRIT.M    | Rule Britannia            | 2/13                                    |
| GARGOYLE.M| Audchar Gargl Zenmur      | 2/39, 3/6                               |
| END.M     | Endgame Music             | 2/39, 3/13, 4/51, 5/95                  |

Voices sharing an instrument share a MIDI channel (BRIT = all voices → ch2/pg13, polyphonic). The
engine will extract `MIDI.DAT` into the pack (like BOOK.DAT) and ship it for the MIDI-OUT backend.
`tools/m2mid` reads `MIDI.DAT` beside the `.M` and applies this map (program-changes + per-instrument
channel routing) into the exported SMF — so it drives a real MT-32 with the authentic timbres.
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
VERIFIED (M10, 2026-07-18) — container + "it's OPL not MIDI" + the opcode/operand table all
CONFIRMED: `tools/mdump` reaches a clean END on all 12 `.M`; the engine sequencer (engine/music.c)
+ software FM synth (engine/oplsynth) + three platform backends (MIDI-OUT, OPL2→DMA PCM via
tools/m2pcm, YM2149 PSG) were written from this spec and validated end-to-end (`make musictest`
sequences all 12; cross-checked against ScummVM/Nuvie `midiparser_m`/`mididrv_m_*`). The pitch
F-number table + 11-byte operator layout are pinned by the working synth.
