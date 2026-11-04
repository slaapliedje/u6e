# Format: the ST music (inside U.PRG, GAME.PRG and END.PRG)

Games: U6, ST release only (Savage Empire and Martian Dreams had no ST release)
Implemented by: engine/sttune.c (finds the song and plays it), render/gamedemo.c (the music manager)
Tracked in: /docs/INDEX.md  ·  Milestone: M10

> Provenance
> - Derived from : the user's ST U.PRG, GAME.PRG and END.PRG, read in ~/src/u6e-re (the disassembly
>   stays there), and the original running in Hatari with every YM2149 write logged.
> - Tools        : tools/dis.py, tools/st_tunes.py (u6e-re); Hatari `--trace psg_write`.
> - Validated by : tools/sttest.c replays the song and compares every register write with the logged
>   run of the original (the result is in "Validation" below).
> - Note         : the copy read is a patched one (its loader says so), so another copy's offsets may
>   differ. The tables are found from the code around them, never by a fixed offset.
> - Confidence   : confirmed for the player and the tables; see "Which tune plays when" for the rest.
> - Last updated : 2026-11-03

## 1. Overview
The ST release has music of its own, and no sound effects. It carries one small three-voice tracker
song for the YM2149, split into 7 tunes, and asks for a tune by the same 12 song numbers as the DOS
game. This corrects milestone.md M10, which said the ST port was silent.

- The same song is in all three programs (same bytes). **U.PRG is not packed**, so it is the one to read.
  GAME.PRG is Pack-Ice compressed.
- The player runs from the vertical blank, so its tempo follows the display: 50 Hz on a PAL ST, 60 Hz
  on NTSC or VGA.
- Nothing else writes to the chip in play except TOS's own port A updates (floppy select). The driver
  has an entry for playing a pattern on voice C over the music, and no code calls it. A "Sound on/off"
  command stops and restarts the music. Its key isn't identified: Ctrl-S did nothing in one try.

We ship none of this: the game reads the song from the player's own U.PRG at start.

## 2. Finding the tables
All numbers are big-endian. Start from the player's tick code: `lea $FF8800.l,a5` then
`move.b #$3F,abs.l` (bytes `4B F9 00 FF 88 00 13 FC 00 3F`). After it, in order of appearance:

| Find | Gives |
|---|---|
| first `4D FA d16` | voice A's state block, PC-relative |
| first `70 00 10 39` | the 4 bytes before it are the absolute address of the position list |
| next `43 F9 abs` | the pattern pointer table |
| next `23 CA abs` | the absolute address of voice A's state + $22, which gives file offset = absolute + delta |
| first `C0 FC 00 0A` | the 4 bytes before it are the instrument table's address |
| first `D0 40 41 FA d16` | the period table, PC-relative |

## 3. Tables
| Table | Layout |
|---|---|
| Periods | 64 u16: YM2149 tone period by note number |
| Instruments | 10 bytes each, up to the position count (4 in this song) |
| Position count | u8, the byte before the position list (27) |
| Positions | 3 bytes each: the pattern number of voices A, B, C |
| Pattern pointers | u32 absolute addresses by pattern number (53 used; 0 is one long rest) |

A **pattern row** is 5 bytes: note, instrument, effect, parameter, hold. Note 0 leaves the voice alone.
Instrument 0 keeps the sounding one; other values are 1-based. A row is read again for each of its
hold + 1 steps. A pattern lasts 64 steps, so its rows' (hold + 1) add up to 64.

**Instrument:**
| Byte | Meaning |
|---|---|
| 0 | attack: added to the volume each tick, or the hardware envelope shape (bit 2 of byte 9) |
| 1 | decay: added each tick |
| 2 | sustain level |
| 3 | ticks until noise stops (0 = never) |
| 4 | peak volume, or the envelope rate |
| 5 | ticks before vibrato starts (0 = at once) |
| 6 | vibrato sweep length in ticks (top bit ignored); the first sweep is half of it, rounded up |
| 7, 8 | vibrato step per tick, low byte then high byte, signed |
| 9 | bit 0 tone on, bit 1 noise on, bit 2 hardware envelope |

## 4. The player
**Tick**, once per vertical blank:
1. Count a tick. When the count reaches the speed (5 at start): clear it, step all three voices, count
   a step, and after 64 steps go to the next position.
2. Run each voice's effect, A, B, C.
3. Advance and output each sounding voice, A, B, C.
4. Write register 7: its top two bits kept (read back), the low six = the enable bits, where a sounding
   voice has cleared its own.

**Start tune n:** position = n x 4, tick count 0. Voices silent, enable bits off. **Load a position:**
step count 0; a position equal to the count becomes 0; each voice takes its pattern's first row.

**Stepping a voice:** read the row. A note sets the voice's note and period. With an instrument too, it
sets the voice sounding, volume 0, attack, noise timer, enable bits from instrument byte 9, vibrato
offset 0. Keep the row's effect and parameter. Then count the hold down; below 0, go to the next row.

**Effects** (run on every tick, in the voice's kept effect):
| Effect | Does |
|---|---|
| 0 | arpeggio if the parameter isn't 0: on tick counts 1 and 4, note + high nibble; 2 and 5, note + low nibble; 3, the note |
| 1, 2 | period down, up by the parameter, each tick |
| 8 * | noise period (register 6) = parameter |
| 9 | noise period = (register 6 and $1F) + parameter, each tick |
| A * | nothing in the music |
| B * | go to position = parameter (how tunes loop) |
| C * | peak volume = parameter; once the voice has reached sustain, volume too |
| D * | next position |
| F * | speed = parameter and $F |

\* once: the kept effect is cleared after it runs. High-bit effects do nothing.

**Advancing and output of a sounding voice:**
1. Noise timer: if not 0, count down; at 0, drop the noise enable.
2. Vibrato: if the delay is $FF nothing. If it isn't 0, count down, and only at 0 go on. Add the step to the
   offset, and count the sweep down. At 0, take instrument byte 6: 0 keeps going; with the top bit set,
   delay = $FF, which stops vibrato; otherwise the sweep = that value and the step changes sign.
3. Volume (a signed byte; skipped with a hardware envelope): **attack** adds byte 0; a negative result or
   one at or over the peak becomes the peak, then decay. **Decay** adds byte 1; a negative result or one at or
   under the sustain becomes the sustain, then sustain. **Sustain** holds until the next note.
4. Write tone low, tone high (period + offset), volume >> 3 (the volume starts at $80 with a hardware
   envelope, so this writes 16), register 13 (shape), register 11 (rate). Clear the voice's enable bits.

A voice not sounding writes nothing, and its enable bits stay off.

**Stop:** set the low six bits of register 7, clear registers 8, 9, 10, silence every voice.

## 5. Which tune plays when
The program holds a table from song number to tune. Song numbers are MIDI.DAT's order, the DOS names:

| Song | ULTIMA | BOOTUP | INTRO | CREATE | FOREST | HORNPIPE | ENGAGE | STONES | DUNGEON | BRIT | GARGOYLE | END |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Number | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| ST tune | 3 | 0 | 5 | 2 | 4 | 1 | 3 | 6 | 3 | 1 | 2 | 1 |

GAME.PRG asks for: gargoyle lands (level 5) 11; surface, a random one of 8, 1, 5, 10; dungeon 9;
then in order of priority aboard a ship or skiff 6, in combat 7. That is the same choice the reference
makes (docs/rules/music_areas.md). Probable, read but not run in the emulator: combat and the vehicle.
It also asks for 8 from one command handler, not yet identified (possibly camping).

## 6. Validation
`tools/sttest.c` has a synthetic check that needs no game data (`make sttest`). Given U.PRG and a log of
register writes, it replays each stretch of the log and compares every write.

- **The original**, in Hatari on a 1 MB ST (intro, title, the opening, character creation, the gypsy, then
  play: walking and attacking): 367,725 writes. 363,394 are the tunes, in order, value for value, and the
  rest are idle ticks and stops. Nothing else. It played BOOTUP's tune for the intro, ULTIMA's at the
  title, INTRO's for the opening and CREATE's for the gypsy, as §5's table says, and a wandering tune in play.
- **Our game**, on a 4 MB ST, TT and Falcon: the same check, 0 unexplained writes. The tune follows the
  display: one tick per vertical blank on the ST (50 Hz, RGB) and on the TT (60 Hz, VGA).

Not run: combat, a vehicle, the dungeons and the ending with these tunes; a real machine; END.PRG's choices.

## 7. Status
VERIFIED for the player and the song. The player's choice of tune comes from docs/rules/music_areas.md.
