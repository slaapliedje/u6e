# Savage Empire DOS AdLib music driver — behavioural spec

Games: Savage (SE). Implemented by tools/se2pcm.c (host pre-render for DMA sound).

Subject: `STRAX.DRV` item 1 (3838 bytes, the AdLib/OPL2 driver) with its bank `SAVAGE.TIM` item 1, and
the parts of SE's `GAME.EXE` music player that drive it. Goal: let a clean-room OPL2 renderer produce
the same register stream DOS does. Companion to `docs/formats/se_music.md` §3.6, §3.7, §7.1, §7.3
(which this extends and in a few places corrects, see §12).

> Provenance
> - Derived from : `strax_1.bin` (driver, loaded at offset 0 of its own segment; addresses below are
>                  driver offsets written `drv:X`), `tim_1.bin` (bank), GAME.EXE image
>                  `~/src/u6e-re/dos/se.bin` (image offsets written `img:X`; DGROUP = image 0x2fa60,
>                  confirmed by the music interrupt loading DS = 0x2fa6 at img:0x2c871; `DS:X` =
>                  image 0x2fa60+X). The 20 songs (`mus_*.bin`) for census checks.
> - Tools        : objdump -m i8086; scratch Python (bank dumper, F-number table/quirk calculator, SMF
>                  walker). No engine source read; no code transcribed.
> - Validated by  : the host renderer built from this spec (tools/se2pcm.c) renders all 20 songs and they
>                  were checked by spectrogram and an emulator recording (not yet listened to); NOT yet checked against a DOS capture of OPL writes.
> - Confidence   : probable: everything below is read from the code unless marked **(uncertain)**.
> - Written      : 2026-11-23

---

## 1. Interface

### 1.1 Entry table (driver header)
Word 0 is unused (0). Words at drv:2..drv:0x12 are near offsets of far-callable routines; the game
copies them into far pointers with the driver's segment (img:0x2cb19-0x2cba0) and calls them with
`lcall`. All take their arguments on the stack, Pascal-style (callee pops), **first listed argument is
the one pushed last** (lowest address).

| Header | Target | Routine | Arguments (as the routine sees them) | Called by GAME.EXE? |
|---|---|---|---|---|
| +2 | drv:0x27e | info | none; returns DX:AX = far pointer to the info block at drv:0x14 | yes, once |
| +4 | drv:0x284 | init + load bank | far pointer to the bank (offset, segment) | yes, once |
| +6 | drv:0x2e1 | shut down | none | yes (music shutdown) |
| +8 | drv:0xd49 | timer | none — this is an **interrupt handler** (ends in IRET), installed on INT 8 | as a vector |
| +A | drv:0x6b5 | note | velocity, key, channel (1-based) | yes |
| +C | drv:0xac6 | controller | value, controller, channel (1-based) | yes |
| +E | drv:0x9dd | pitch bend | MSB, LSB, channel (1-based) | yes |
| +10 | drv:0xc16 | program | program, channel (1-based) | yes |
| +12 | drv:0x2d3 | set port | port word (stored at drv:0x2d) | no (port stays 0x388) |

drv:0x14 is **not** an entry: it is the info block (the "two more" entries in se_music.md §7.1 are
this +0x12 set-port entry and the start of the info block).

### 1.2 Info block (drv:0x14 ..)
| Block offset (drv) | Meaning |
|---|---|
| +0 (0x14) | 0 |
| +1 (0x15) | 1 = "keep the bank in memory" (the game reads it after load bank, img:0x2cbda, and frees the bank buffer only if 0) |
| +2 (0x16) | far pointer: the INT 8 handler the driver's timer chains to (filled in by the game's set-vector helper) |
| +0xA..+0x11 (0x1e..0x25) | four words the game fills in (0, 0x2f0d, 0x104, 0x2f30); **unused by this driver** (probably callbacks for other drivers) |

### 1.3 How the game starts it (img:0x2cbff .. 0x2cd6b)
1. Load the driver and bank (img:0x2ca87): call **info**, write the info words, call **load bank**.
2. AdLib only (driver type 2): **program** for the virtual drum channels 10..26 (table in §9.2).
3. Set the PIT to 60 Hz (divisor 19886; the driver's own delay loop wraps at 0x4dae = 19886, which
   confirms it).
4. With interrupts off: hook INT 8 with the **player's** handler (img:0x2c868), saving the BIOS
   vector for the player to chain to; then hook INT 8 again with the **driver's timer**, saving the
   player's handler into the info block's chain pointer.

Result, on every 60 Hz tick: **driver timer (voice effects) → player handler (fade, then the
sequencer's events for that tick) → BIOS**. The driver's per-tick effects always run before the
tick's MIDI events, and never interleave with them. There is no divider: the timer does its work on
every interrupt, music playing or not.

---

## 2. Driver state

Indices: voice v = 0..8 (arrays are sized for 11 so rhythm mode could use 9, 10); channel c = MIDI
channel − 1, 0..31 (32-entry arrays; SE uses 1..26 = 2..10 real + 11..26 virtual drum channels).
"Record" = one 48-byte bank entry; the driver identifies a record by a segment value (bank segment +
3 × index, records being 3 paragraphs long), the offset being fixed (bank offset + 1). Treat it as a
record index.

| Name (ours) | drv: | Size | Initial | Meaning |
|---|---|---|---|---|
| nvoices | 0x2f | byte | 0 file / **9** after load bank | number of melodic voices (6 in rhythm mode, never in SE) |
| bd_shadow | 0x30 | byte | 0 | shadow of OPL 0xBD (rhythm mode only) |
| v_key[v] | 0x31 | byte | 0xFF | MIDI key that started the voice |
| v_kpitch[v] | 0x3c | word | 0 | key pitch after key-follow, 1/256 semitone |
| v_rec[v] | 0x52 | word | **0** (not "none") | record loaded into the voice; 0xFFFF = none (after a program change) |
| v_state[v] | 0x68 | byte | 1 | 0 = released (release stage), 1 = idle, 2/3/4 = pitch-envelope stage 1/2/3 while held, 5 = held, envelope finished |
| link[13] | 0x73 | byte | see §3 | two circular lists: node 11 = FREE head, node 12 = BUSY head |
| v_chan[v] | 0x80 | byte | 0xFF | channel (0-based) the voice was last set up for |
| ch_rec[c] | 0x8b | word | set by load bank, see §8 | current program's record |
| v_env[v] | 0xcb | word | 0 | pitch-envelope value, 1/256 semitone (signed) |
| ch_bend[c] | 0xe1 | word | 0 | bend, 1/256 semitone (signed) |
| v_vib[v] | 0x121 | word | 0 | vibrato offset, 1/256 semitone (signed) |
| v_phase[v] | 0x137 | byte | 0 | vibrato phase |
| ch_vrate[c] | 0x142 | byte | 0 | vibrato rate (record byte 0x10) |
| ch_vdepth[c] | 0x162 | word | 0 | vibrato depth (record 0x11 + mod wheel) |
| ch_vol[c] | 0x1a2 | word | **0x100** | channel volume factor (cc7 + 128 once any cc7 arrives) |
| car_op[v] / mod_op[v] | 0x1e7 / 0x1fb | 9 bytes | 3,4,5,11,12,13,19,20,21 / 0,1,2,8,9,10,16,17,18 | standard OPL2 operator offsets per voice |
| ftab[13] | 0x20f | words | see §5 | F-numbers |
| env_ofs[6] | 0x229 | bytes | 0x18,0,0x12,0x14,0x16,0 | envelope stage per v_state, §6.1 |

OPL output (drv:0x264): write the register index to port (default 0x388), busy-wait 10 PIT counts,
write the value to port+1, busy-wait 55 PIT counts (the wait reads PIT channel 0, so it is CPU-speed
independent). An emulator applies writes in order, instantly.

---

## 3. Voice allocation

### 3.1 Lists
Two circular singly-linked lists with sentinels: FREE (head node 11) and BUSY (head node 12). Initial
FREE order: **0, 1, 2, 3, 4, 5, 6, 7, 8**; BUSY empty. Operations:
- take-first(L): the first node after the sentinel.
- append(L, v): put v at the end (just before the sentinel).
- unlink(v): remove v from whichever list holds it.

So FREE is a FIFO of released voices (oldest release reused first) and BUSY is in order of
allocation (oldest allocation first).

### 3.2 Note on (velocity ≠ 0), per record r of the note (§4.1)
1. If FREE is not empty: v = take-first(FREE), append(BUSY, v).
2. Else, **only if r is the channel's current program record (r == ch_rec[c])**, i.e. not a layered
   extra record: steal v = take-first(BUSY) (the oldest still-held allocation), move it to the end
   of BUSY, and key it off hard: write 0xA0+v = 0, 0xB0+v = 0.
3. Else: no voice; this record of the note is dropped (layer voices never steal).
4. With a voice: v_state = 2, v_key = key.
5. If v_chan[v] ≠ c or v_rec[v] ≠ r: load the instrument (§4.2) and set v_chan = c, v_rec = r.
   A voice reused for the same channel and the same record is **not** reloaded.

There is no per-channel limit and no check for the same key already sounding: a re-struck key that is
still held takes another voice; both sound.

### 3.3 Note off (velocity 0), per record r of the note
Scan voices 0..nvoices−1 **in index order**; the first v with v_state > 1, v_key = key, v_chan = c and
v_rec = r is released: v_state = 0, unlink(v), append(FREE, v), then the key-off write (§4.4). If
none matches, nothing happens. With a doubly-struck key, the lower-numbered voice is released first
(not the older one).

A voice that was stolen has its v_key/v_chan/v_rec overwritten, so the stolen note's own note-off
later finds nothing (unless the thief has the same key, channel and record — then it releases the
thief).

### 3.4 Sustain pedal
Controller 64 is ignored. There is no sustain.

---

## 4. Note call (+A): register sequence

The routine is called once per note event and walks the channel's **record chain**:
r = ch_rec[c]; repeat { handle r; if byte 0x26 of r is 0 stop, else r = the next record in the bank }.
If ch_rec[c] is 0xFFFF nothing is done (that is what happens to an unmapped drum key, whose channel
0 indexes the word just before ch_rec, which holds 0xFFFF).

### 4.1 Per record r
1. Voice: §3.2 (on) or §3.3 (off). No voice → next record.
2. Note on only: v_vib = 0, v_phase = 0, v_env = signed word at r+0x12.
3. Key pitch (on and off): let d = key − 60 (signed), n = signed byte r+0x27.
   - n ≥ 0: kp = 60·256 + ((d·256) >> n)
   - n < 0: kp = 60·256 + ((−d·256) >> (−n − 1))   (so −1 = mirrored around key 60, no compression)
   (`>>` is arithmetic; 16-bit). v_kpitch = kp.
4. Pitch P = kp + ch_bend[c] + v_env + v_vib + signed word r+0x24 (16-bit wrap). Convert (§5) to
   word W = block<<10 | fnum.
5. **Note off**: write 0xA0+v = W & 0xFF, 0xB0+v = W >> 8 (key bit clear). Done with this record.
6. **Note on**, carrier TL (§4.3): written only if byte r+0x0C ≠ 0 **or** ch_vol[c] < 0x100.
7. Modulator TL (§4.3): written only if byte r+0x0D ≠ 0.
8. Key on: W += 0x2000; write 0xA0+v = W & 0xFF, 0xB0+v = W >> 8.

No key-off is written before key-on (a free voice was already keyed off by its note off; a stolen
voice got A0/B0 = 0 in §3.2).

Full order for a note on with a reload: [steal: A0, B0 = 0] → 0x20/0x40/0x60/0x80/0xE0 of the
modulator → 0x20/0x40/0x60/0x80/0xE0 of the carrier → 0xC0+v → carrier 0x40 → modulator 0x40 → 0xA0
→ 0xB0. Without a reload the first eleven are skipped.

### 4.2 Instrument load (drv:0x34d)
| Register | Value |
|---|---|
| 0x20 + mod_op[v] .. 0xE0 + mod_op[v] (5 writes, order 20,40,60,80,E0) | record bytes 0x00..0x04 |
| 0x20 + car_op[v] .. 0xE0 + car_op[v] (same order) | record bytes 0x05..0x09 |
| 0xC0 + v | record byte 0x0A, unchanged |

(The carrier and C0 writes are skipped for rhythm-mode slots 7..10 — unreachable in SE, §8.)

### 4.3 Total level
All arithmetic 16-bit signed; `>>` arithmetic; vel = MIDI velocity 0..127 as received; s = the
sensitivity byte.

Carrier (s = byte 0x0C, base = byte 0x06):
```
t   = ((63 - vel) >> (7 - s)) + (base & 63)        ; not clamped here
out = 63 - ((ch_vol[c] * (63 - t)) >> 8)           ; low 16 bits of the product, then >> 8
out = clamp(out, 0, 63)
reg 0x40 + car_op[v] = (base & 0xC0) + out
```
Modulator (s = byte 0x0D, base = byte 0x01), only when s ≠ 0, no channel volume:
```
t = clamp(((63 - vel) >> (7 - s)) + (base & 63), 0, 63)
reg 0x40 + mod_op[v] = (base & 0xC0) + t
```
Notes:
- Velocities above 63 make (63 − vel) negative, so loud notes **reduce** TL (louder than the patch's
  own level); vel 64..127 with s = 5 subtracts up to 16.
- With s = 0 the shift is 7: the term is 0 for vel ≤ 63 and −1 for vel ≥ 64.
- If s = 0 and ch_vol = 0x100 the carrier TL is not written at all and keeps whatever was last
  written to that operator (the load value, or the previous note's computed value). ch_vol is 0x100
  only from load until the channel's first cc7/cc121 (every song start sends cc121, §7.1), so in play
  the carrier TL is always recomputed.
- If the modulator s = 0, its TL stays as loaded (or as the previous note on that voice left it, if
  the voice was not reloaded — same record, so the same value unless a different velocity was
  applied earlier with s ≠ 0, which cannot happen for one record).
- Channel volume does **not** reach notes already sounding: cc7 only stores ch_vol (§7). Fades
  therefore only affect notes started during the fade.

### 4.4 Channel volume curve
ch_vol[c] = value + 128 for cc7 value 0..127, i.e. 128..255 (0x100 is the pre-cc7 default only). In
TL terms the carrier's attenuation above the patch is multiplied by ch_vol/256: cc7 = 0 still plays at
"half" (63 − t halved, about −24 dB at most), cc7 = 127 is 255/256. There is no table; the curve is
this linear factor. The value reaching the driver is the song's cc7 × master >> 8 (§7.2).

---

## 5. Pitch → F-number / block (drv:0x60d)

Input P (signed 16-bit, 1/256 semitone, MIDI note numbers: 60·256 = middle C).
```
n    = P >> 8              (arithmetic)
frac = P & 0xFF
blk  = clamp(trunc((n + 6) / 12) - 2, 0, 7)        ; C division, truncates toward zero
s    = rem(n + 6, 12)                                ; C remainder (sign of the dividend)
f    = ftab[s]
if frac != 0:
    nx = word at drv:0x211 + 2*rem(n - 18, 12)       ; = ftab[s+1] whenever n >= 18
    f  = f + (int16(low16((nx - f) * frac)) >> 8)
result = (blk << 10) + f   (16-bit)
```
ftab (13 words, the 13th is the octave above the 1st):

| s | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| F-num | 0x1E5 | 0x202 | 0x220 | 0x241 | 0x263 | 0x287 | 0x2AE | 0x2D7 | 0x302 | 0x330 | 0x360 | 0x393 | 0x3CA |

(0x1E5 at block 0 = 23.0 Hz = MIDI 18, F♯0, with the OPL2's 49716 Hz clock: so n is the MIDI note.)
Interpolation is linear in F-number between neighbouring semitones, which is a slightly flat
approximation of the exponential curve between them.

Out-of-range behaviour (reproduce bit-exact if notes go there):
- **n ≥ 114**: block clamps at 7 while s keeps cycling, so notes from 114 up sound an octave low
  (songs reach key 119).
- **6 ≤ n ≤ 17**: block clamps at 0, so these sound an octave high (n −6..5: two octaves high).
- **n < 18 with frac ≠ 0** (any bend/vibrato/envelope/offset that leaves a fraction): the "next" value
  nx is read from the wrong place, giving a wrong, sometimes huge interpolation. The index
  rem(n−18, 12) is n−18 for 7 ≤ n ≤ 17 (and n−6 for −5 ≤ n ≤ 5), landing before ftab in the operator
  tables. The values read (also checked: n = 6 and n = 18 are correct):

| n (and n−12) | 17 (5) | 16 (4) | 15 (3) | 14 (2) | 13 (1) | 12 (0) | 11 (−1) | 10 (−2) | 9 (−3) | 8 (−4) | 7 (−5) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| f = ftab[s] | 0x393 | 0x360 | 0x330 | 0x302 | 0x2D7 | 0x2AE | 0x287 | 0x263 | 0x241 | 0x220 | 0x202 |
| nx used | 0x01E5 | 0x1115 | 0x1214 | 0x100A | 0x0908 | 0x0201 | 0x0012 | 0x1110 | 0x0A09 | 0x0802 | 0x0100 |
| nx correct | 0x3CA | 0x393 | 0x360 | 0x330 | 0x302 | 0x2D7 | 0x2AE | 0x287 | 0x263 | 0x241 | 0x220 |

  The product (nx − f)·frac is truncated to 16 bits (signed) before the shift, and the sum can spill
  into the block bits; the bytes written to A0/B0 are simply the low and high byte of the 16-bit
  result (B0 + 0x20 for key on, modulo 256). Song 19 has a key 12, so this is reachable.
- n < −6 reads further outside the table (not reachable with SE's data as far as checked).

### 5.1 Pitch bend (+E)
```
b   = (MSB << 7) + LSB - 0x2000                         ; −8192..8191
bend = (b * range) >> 8   ; 32-bit product, arithmetic shift, keep 16 bits
```
range = byte 0x0E of the channel's **current program record** ch_rec[c] (96 in the music
instruments = ±12 semitones; 0 in most drums and effects = bend ignored). Full deflection =
32·range/256-ths = range/8 semitones. ch_bend[c] = bend. Then every voice with v_state > 1 and
v_chan = c is re-pitched immediately (§4.1 step 4 with its own record's 0x24 offset, or 0 if
v_rec = 0xFFFF) and written as A0, then B0 with the key bit set. Released voices (state 0/1) are not
re-pitched here; they pick the new bend up at the next timer tick if the timer rewrites them (§6.3).
A layered voice uses the primary record's range (it is per channel). Bend on channel 10 does **not**
propagate to the virtual drum channels (only controllers do).

---

## 6. Timer (+8): per-tick effects at 60 Hz

For each voice v = 0..nvoices−1, every interrupt (whether the voice is sounding or not):

Let r = v_rec[v] (if 0xFFFF: the bank's record 0 for envelope reads and offset 0); off = signed word
r+0x24 (0 if v_rec = 0xFFFF); c = v_chan[v]; changed = false.

### 6.1 Pitch envelope
Stage by state: state 2 → pair at r+0x14, state 3 → r+0x18, state 4 → r+0x1C, state 0 (released) →
r+0x20; states 1 and 5 → no envelope. Each pair is two signed words: **rate**, then **target**.
```
if |target - v_env| < rate  (unsigned compare):  v_env = target; v_state += 1
elif v_env < target:                             v_env += rate
else:                                            v_env -= rate
changed = true
```
So: note on starts at the word at 0x12 and moves through targets 1, 2, 3 at their rates (one step per
tick), then holds (state 5); a note off at any point starts the release stage (from wherever v_env
is) toward the release target, after which the voice is idle (state 1). The released state 0 → 1
transition does not touch the lists.

**Rate 0 never completes** (|d| < 0 is false), so a stage with rate 0 holds the voice in that stage
forever with v_env unchanged — and still sets `changed`. Nearly every record has all-zero stages, so
in practice nearly every voice that has ever played gets its frequency rewritten every tick (§6.3).
This is inaudible (same value; OPL key-on is edge-triggered) but it is part of the register stream.

### 6.2 Vibrato (only if c < 32 and ch_vrate[c] ≠ 0)
```
v_phase = (v_phase + ch_vrate[c]) & 0xFF
p = v_phase as signed byte
if p > 63 or p < -64:  p = (0x80 - v_phase) as signed byte    ; triangle, range −64..+64
v_vib = low16(ch_vdepth[c] * p) >> 4                         ; arithmetic
changed = true
```
Period = 256/rate ticks (rate 25 → 10.24 ticks ≈ 5.9 Hz); peak = depth × 4 /256 semitone (depth 10 →
40/256 ≈ 16 cents). Vibrato also runs on released voices (their v_chan is kept), and stops for a
voice whose v_chan was cleared by a program change. v_phase/v_vib restart at 0 on each note on, so
the first tick after a note on is the first vibrato step.

There is **no tremolo** or any TL change in the timer.

### 6.3 Write-back
If changed or off ≠ 0: P = v_kpitch + ch_bend[c] + v_env + v_vib + off, convert (§5), set the key
bit if v_state > 1, write 0xA0+v then 0xB0+v.
(For a voice never used since load, v_rec = 0 — a garbage record at segment 0 — but state 1 means no
envelope, v_chan = 0xFF means no vibrato; only a non-zero "offset" read from that garbage could cause
a key-off frequency write. Inaudible; an implementation should simply skip never-used voices.)
**Voices cleared by a program change (§8 step 2) while still in their release tail** are rewritten
by this step every tick with: v_rec = 0xFFFF → record 0's release stage (all zero, rate 0: v_env
frozen, `changed` set) and off = 0 (the old record's 0x24 offset is dropped); v_chan = 0xFF → no
vibrato step (v_vib frozen at its last value) and ch_bend[0xFF] reads the word at drv:0x2df, which is
**0x0002** (code bytes) — so +2/256 semitone. Net effect, audible only for the old record's
non-zero 0x24 offset (records 19, 22, 23, 33, 39, 41-48): the dying tail jumps by −offset. Key bit
clear. Reproduce as: pitch = v_kpitch + 2 + v_env + v_vib (all frozen).

After the loop the handler chains to the saved INT 8 vector (the player).

---

## 7. Controllers (+C)

If channel = 10, the controller is first applied (recursively, same routine) to channels 11..26,
then to 10 itself. So drum-channel controllers reach every virtual drum channel.

| Controller | Effect |
|---|---|
| 1 modulation | ch_vdepth[c] = ((byte 0x0F of ch_rec[c]) × value >> 7) + byte 0x11 of ch_rec[c] |
| 7 volume | ch_vol[c] = value + 128 (no re-write of sounding voices) |
| 121 reset all | controller(1, 0), controller(7, 127), bend(MSB 0x40, LSB 0) on that channel (bend re-pitches its held voices) |
| 123 all notes off | for v = 0..10 with v_state > 1 and v_chan = c: a note off (§4) for key v_key[v] on that channel (walks the current program's chain, so layer voices are released too). Then the rhythm-mode exit check (never relevant) |
| everything else (10 pan, 64 sustain, 0/32 bank, …) | ignored |

### 7.1 What the player sends (GAME.EXE)
- Song start (img:0x2c989): cc121 value 0 on channels 2..10 (→ 11..26 too). Nothing else; no all
  notes off at start; channels the music already holds stay held across songs.
- Channel claim (first channel event on a channel the music doesn't hold, se_music.md §3.6): cc123,
  the remembered program if it differs from the last one sent (not on channel 10), cc7 =
  remembered × master >> 8 (initial remembered = 127), cc10 = remembered (initial 64; ignored here).
  In normal play channels are claimed once after load and only re-claimed after a sound effect has
  borrowed them.
- 0xFE type 0 loop jump: cc123 on the track's channel. Type 1 exit: cc123 on 2..10. Stop
  (img:0x2c7f4): cc123 on every held channel 1..16. A song that just runs to its end sends nothing:
  its voices release normally.
- Note off events (0x8n) go out as note calls with velocity 0 (the event's velocity is dropped).

### 7.2 Master volume and fades (player side)
cc7 values from the songs (and the re-sends) are scaled `value × M >> 8` with M = DS:0x2bf2, initial
0x100. The setter img:0x2d08b is never called, so outside fades M = 0x100 and cc7 passes through
unchanged. A fade (img:0x2ca2d, N ticks; called from img:0x271b7, 0x27582, 0x276ad) sets
level = 0x7FFF and step = −(0x8000 / N) (unsigned divide); each tick while playing,
level += step, M = ((level >> 8) × DS:0x2bf4) >> 7 (DS:0x2bf4 = 0x100, so M starts at 254) and cc7 is
re-sent on held channels 2..10. When level goes negative: stop the song (cc123 on held channels,
unless DS:0x2bfa is set) and restore M = 0x100 with another cc7 re-send. Because the driver applies
volume only at note on and maps cc7 0 to ch_vol 128, a fade lowers new notes toward half-attenuation
and then cuts everything with cc123.

---

## 8. Program change (+10)

1. Find the record: scan the bank's records in order for byte 0x2F = program (low byte); the first
   match wins (no duplicates in the bank); none → **record 0** (program 0). Songs' programs 25, 73,
   97 get record 0.
2. For every v = 0..10 with v_chan = c (sounding or not): a note off for v_key[v] through the
   channel's *old* chain, then v_chan = 0xFF, v_rec = 0xFFFF (forces a reload next time). So a
   program change **cuts the channel's sounding notes** (they go to release).
3. ch_rec[c] = the new record; ch_vrate[c] = byte 0x10; ch_vdepth[c] = byte 0x11 (mod wheel is
   forgotten until the next cc1).
4. If byte 0x0B ≠ 0 and nvoices = 9: enter rhythm mode. **Never happens: byte 0x0B is 0 in all 86
   records** (checked). Rhythm mode is therefore unreachable and the driver stays in 9-voice melodic
   mode; 0xBD stays 0.

Initial ch_rec: load bank fills all 32 entries with a value left in AX (the bank's count byte, 0x56,
with AH = 0) — a bogus record pointer. Harmless in SE: every melodic channel of every song gets a
program change before its first note (checked on all 20 songs), and the drum channels are programmed
at init. Only cc1/cc121 at the very first song start read that bogus record (vibrato depth), and
the following program change overwrites it. An implementation can initialise ch_rec to record 0.

### 8.1 Layered records (byte 0x26)
If byte 0x26 ≠ 0, the note also plays the **next record in the bank** on a further voice; that
record's own 0x26 is then checked, so chains can be longer (program 7 = records 54, 55, 56: three
voices). The value of 0x26 itself is not used (in the bank it always equals the next record's
program + 1). Each record of the chain gets its own voice (§3.2, layers never steal), its own
instrument registers, TL sensitivities, key-follow, envelope and 0x24 offset; it shares the key,
velocity, channel bend (with the primary's range), channel vibrato and channel volume. Layered
records in the bank: 41, 43, 45, 47 (programs 123, 125-127), 49 (3), 54 (7), 55 (154 → 155).

---

## 9. Init, shutdown, drums

### 9.1 Load bank (+4) and shutdown (+6)
Load: nvoices = 9; writes **0x01 = 0x20** (waveform select enable), **0xBD = 0x00**, **0x08 = 0x00**,
in that order; remembers the bank (count, first record). No other register is initialised: voice
registers stay at whatever the chip held until a voice's first instrument load (an emulator should
start from a reset chip). Shutdown: nvoices = 0, 0xBD = 0, then for v = 0..8: 0xA0+v = 0,
0xB0+v = 0.

### 9.2 Drums (confirmed: se_music.md §7.3 is right)
The remap is in GAME.EXE, before the driver (img:0x2c2fc note off, img:0x2c367 note on, only for
driver type 2): a channel-10 key k (0..76 table; songs use only 36..75) becomes key DS:0x2e6e[k] on
channel DS:0x2e21[k]; channel 0 = silent (59, 65, 66). Tables re-read from the image: identical to
§7.3. At init the virtual channels 10..26 get exactly the programs of §7.3's table (img:0x2cc23..
0x2ccef: 128, 114, 131, 113, 134, 135, 133, 132, 129, 136, 141, 143, 144, 145, 147, 140, 139). Drum
"channels" are ordinary melodic voices from the same pool of 9; songs never send a program change on
channel 10 and the claim re-send skips it, so these programs never change. Channel arrays hold 32
entries; SE uses channels 2..26.

---

## 10. Bank effect bytes (decoded; completes the record table)

| Off | Meaning |
|---|---|
| 0x0F | mod-wheel vibrato sensitivity (depth added = byte × cc1 / 128) |
| 0x10 | vibrato rate (phase step per tick; 0 = no vibrato) — per channel, at program change |
| 0x11 | vibrato base depth — per channel, at program change |
| 0x12-0x13 | pitch-envelope start value (signed, 1/256 semitone) |
| 0x14/0x16 | stage 1 rate / target |
| 0x18/0x1A | stage 2 rate / target |
| 0x1C/0x1E | stage 3 rate / target (then hold) |
| 0x20/0x22 | release rate / target (from note off) |

Non-zero envelopes: record 4 (prog 48: start −80, scoop to +75 then 0, rate 20), 17 (113: 150 →
0 at 500, then −200 at 50), 18 (114, drum ch 11: release rises to +2000 at 20/tick), 24 (124: 4000 →
0 at 200), 25 (128, drum ch 10 bass drum: 50 → 0 at 10, → −30 at 20, release → −50 at 100).
Vibrato rate non-zero: records 2-5 and 7-13 (music instruments; programs 24, 33, 48, 50, 65, 72, 75,
83, 84, 92, 93). Of these only 4, 5, 10, 11, 12, 13 have a base depth (10, 7, 5, 7, 2, 5); the others
vibrate only with the mod wheel (cc1 occurs in song 3 only), sensitivity 15-30.

---

## 11. What can make two renders differ

- **No randomness** in the driver or in the player paths read.
- Timing: all driver and player work happens on the 60 Hz interrupt (PIT divisor 19886 → 60.0008 Hz);
  events are quantised to ticks (se_music.md §3.7). Within a tick order is: driver timer for voices
  0..8, then the player's fade step, then the tick's events in scheduler order. CPU speed only
  changes how long the register writes take inside the tick (each write waits ≈ 65 PIT counts ≈ 55 µs;
  a busy tick can spread its writes over a few ms) — negligible; apply each tick's writes at the tick.
- History: voices, the free-list order, programs and stuck envelope stages carry over from song to
  song and from sound effects (which use the same driver and the same 9 voices on AdLib — they can
  steal voices from the music). A cold render matches DOS exactly only from load-bank state; after
  other music/effects the voice numbers differ, which matters only for stealing and for overlapping
  release tails.
- Main-program calls (song start cc121, stop/fade cc123, claims after sound effects) run outside the
  interrupt and can be interrupted by the timer mid-call; the game wraps some in cli/sti (e.g.
  img:0x2b699) but not all. Rare, nondeterministic, ignore.
- Fades depend on when the game calls them; they only affect new notes (§7.2).

---

## 12. Corrections / additions to se_music.md §7.3

- 0x0C: the carrier term is not clamped before the volume step; the final value is clamped. With
  s = 0 there is still a −1 for velocities ≥ 64, and the write is skipped only while ch_vol = 0x100.
  Channel volume = cc7 + 128 (a linear factor /256), not cc7 itself.
- 0x0F-0x11 and 0x12-0x23 are decoded (§10). 0x26 is a chain flag (§8.1), value unused.
- 0x27: negative n mirrors around key 60 and shifts by (−n − 1).
- "Two more" entries: +0x12 = set port; drv:0x14 is the info block, not an entry; +8 is an INT 8
  handler chained in front of the player's (§1.3).
- The F-number table is at drv:0x20f (13 words); drv:0x60d is the conversion routine (§5).
