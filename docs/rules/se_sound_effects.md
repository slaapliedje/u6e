# Savage Empire: sound effects (as DOS GAME.EXE plays them)

Games: Savage Empire (SE) only. U6's equivalent is rules/sound_effects.md (a very different system: U6 holds the
game and beeps the speaker; SE plays its effects as notes on the music driver, in the background).
Implemented by: engine/sesfx.c (the effect engine of §3) + render/gamedemo.c (`se_sfx`, the play call, and its call
sites; `se_sfx_stop_all`; `se_sfx_flight`, `se_sfx_melee` for the flight table and the melee blow).
Companions: formats/se_music.md §3.6 (channel claiming), §7 (drivers, SAVAGE.TIM), formats/se_adlib.md (the AdLib
driver the effects also go through), design/se_origin_fx.md §9 (the fireworks, SAVAGE.EXE).

> Provenance
> - Derived from : SE GAME.EXE image `~/src/u6e-re/dos/se.bin` (DGROUP image 0x2fa60, so DS:X = image
>                  0x2fa60+X; string segment 0x2f65): the play call 0x2701:0x648 (image 0x27658), the effect table
>                  DS:0x2366, the effect engine 0x2b5c:0x4-0x8e5 (image 0x2b5c4-0x2beef: start 0x2ba8f, step 0x2b731,
>                  tick 0x2b93f called from the music interrupt at 0x2c975, channel lists 0x2b5c4-0x2b6e9, controller
>                  0x2bc12, stop 0x2bdee/0x2be5c, stop-all 0x2bea5, hand-back 0x2bd80), the player's claim
>                  0x2c200-0x2c288, all 41 call sites of 0x2701:0x648; SAVAGE.EXE `savage.bin` (DGROUP 0xdaa0): the same
>                  engine at 0x8e4:0x4da and the spark call at 0x72f1; the user's `STRAX.DRV` items 0 and 2 (MT-32 and
>                  PC-speaker drivers, read whole) and `SAVAGE.TIM` items 0 and 2 (decoded with our LZC reader). The
>                  callers' meanings come from our own SE rule specs (se_combat, se_use, se_invoke, se_levels).
> - References   : the public Roland MT-32 manual facts (patch/timbre memory, reversed pan) only; no engine source read.
> - Tools        : objdump -m i8086; scratch Python (call-site scanner, descriptor and speaker-record decoders).
> - Validated by  : NOT YET validated against DOS running (no DOSBox capture yet). Internal checks only: the 34
>                  descriptors' programs are exactly the custom patches SAVAGE.TIM item 0 installs (3-23, 40-47,
>                  59-63), the spark descriptor is program 127; the speaker records' program numbers match the same
>                  patch numbers; the driver's pitch table equals round(256 x 2^(u/48)) for all 48 entries.
> - Confidence   : engine, descriptor format, drivers = probable (read from the code); call-site meanings = probable
>                  where an existing spec or a nearby string names them, guessed where marked
> - Last updated : 2026-11-23

## 1. Overview (probable)

- An effect is a **note on the music driver**: GAME.EXE borrows one of the music's MIDI channels 2-9, sends a program
  change, volume, pan and a note on, and a 60 Hz tick sends the note off when its time is up and gives the channel
  back. The same calls go to whichever driver CONFIG.SE chose: MT-32 (custom timbres), AdLib (FM bank programs) or
  the PC speaker (whose driver turns the program into a synthesised noise/tone recipe from SAVAGE.TIM item 2).
- It is **asynchronous**: the play call returns at once, the game goes on, the music interrupt ends the note. Nothing
  waits for an effect (U6, by contrast, holds the game).
- In GAME.EXE every effect uses the same handle (0), so **at most one effect sounds at a time**: a new one cuts the
  one before and takes its channel. SAVAGE.EXE's fireworks use one handle per spark and can sound several at once.
- No effect without a sound driver (CONFIG.SE `N`) or with sound off (DS:0x2810 = 0, the in-game toggle).

## 2. The play call (GAME.EXE 0x2701:0x648, far pascal, one word argument: the effect id)

1. If sound is off (DS:0x2810 = 0) or id >= 34: nothing.
2. Start effect: descriptor = DS:0x2366 + 8 x id, flags 0, volume 127, pan 63, handle 0, priority 2 (§3.2).

That is all; the caller never stops an effect itself. Effects stop by running out (§3.4), by a newer effect (same
handle), or by **stop-all** (§3.6), which GAME.EXE calls at: sound turned off (the toggle at 0x570e, with the 1 s music
fade); a level change (0x8cb:0x855, image 0x9505, se_levels.md); and after each frame of the tremor (§5, id 1).

A debug command (`SFX number: `, two digits, only when sound is on) plays any id 0-99 (ids >= 34 do nothing).

## 3. The effect engine (shared by GAME.EXE and SAVAGE.EXE)

### 3.1 Descriptor (8 bytes, a list of steps; GAME.EXE's 34 are all one step)

| Off | Size | Meaning |
|---|---|---|
| 0 | 1 | step flags: 0x01 another step follows (next 8 bytes); 0x02 glide (below); 0x04 hold (the timer never runs: sounds until stopped); 0x08 repeat the glide forever |
| 1 | 1 | program + 1 (0 = end of the list: the effect ends here) |
| 2 | 1 | MIDI key |
| 3 | 1 | velocity |
| 4 | 2 | duration in 60 Hz ticks (little-endian) |
| 6 | 1 | glide target key |
| 7 | 1 | unused (0) |

Glide: when the duration runs out the note is released; if the current key is below the target it goes up one, if
above down one, and the new key is struck again (same velocity, a fresh duration); on reaching the target the key
returns to byte 2 and, only with flag 0x08, starts over; otherwise the step is over. (A glide re-strikes only the
first channel of a stereo pair: DOS's own.) Only GAME.EXE id 26 glides, and nothing calls it.

### 3.2 Starting an effect (0x2b5c:0x4cf; arguments: descriptor far pointer, flags, volume, pan, handle, priority)

The engine keeps 32 effect slots (14 bytes at DS:0x2c06: flags, channel 1, channel 2, current key, volume, pan,
handle, ticks left, descriptor step pointer). Call-flags bit 0x01 = **stereo pair** (two channels, one hard each way);
bit 0x02 = keep the slot after the end. GAME.EXE and SAVAGE.EXE always pass flags 0.

1. No driver (DS:0x2be6 = 0): nothing.
2. A sounding slot with the same handle is cut (note off for its key on its channel(s)) and **its channel is reused**
   without a new allocation. Otherwise a free slot is taken and a channel (two for a stereo pair) is allocated (§3.3);
   none available: nothing plays.
3. The slot is filled and its first step started (§3.4). The call returns the slot (SAVAGE.EXE keeps it per spark).

### 3.3 Channels: free list, used list, priorities (DS:0x2df2 next-links, DS:0x2dff priority, DS:0x2e0c owner)

Only MIDI channels **2-9** take part (never 1, never the rhythm channel 10). Two linked lists: *free* (at start 2, 3,
..., 9 in that order) and *used*. Owner per channel: 0 nobody, 1 the music, 2 an effect.

- **Allocate (priority p):** the head of the free list if any; otherwise the first channel along the used list (oldest
  first) whose priority is <= p; none: fail. The channel is moved to the tail of the used list with priority p, and
  any other effect slot using it as its first channel is dropped (no note off).
- **The music touches a channel** (every channel event it plays on a channel it owns): the channel moves to the tail
  of the used list with priority 0. So the used list is in order of the music's last activity, and an effect steals
  the music channel that has been **quiet the longest**. Music priority is 0, GAME.EXE effects 2, sparks 1: an effect
  can always take a music channel; a spark can take another spark's, and a game effect can take a spark's.
- **Release** (an effect ends): All Notes Off (controller 123) on the channel, owner 0, channel to the tail of the free
  list. The music does not resume at once: at its **next event on that channel** it claims it again (se_music.md §3.6:
  All Notes Off, its remembered program if the last program sent there differs, its volume scaled by the master, its
  pan; owner 1). While an effect owns the channel the music's events for it are read and timed but **not sent**: the
  borrowed part is silent for the effect's length and its next note comes back.
- Channels a song never used stay on the free list (they are never returned by the music), so with an 8-part song
  playing, effects almost always steal.

### 3.4 A step (0x2b731) and the tick (0x2b93f, 60 Hz)

**Step start.** Program byte 0 or the slot has lost its channel: the effect ends (release; the slot is freed unless
call-flag 0x02). Otherwise, with interrupts off, for the channel (and the second one of a pair):
1. if the effect does not own it yet: All Notes Off on it, owner := effect (this is what cuts the music part);
2. program change (descriptor program), remembered as the last program sent there;
3. controller 7 = the volume, controller 10 = the pan. A stereo pair instead sends pan 0 on its first channel with
   volume x (pan < 32 ? 127 : 159 - pan) / 128 and pan 127 on its second with volume x (pan > 95 ? 127 : pan + 32) / 128;
4. note on (key, velocity).
Then ticks left := duration, current key := the key.

**Tick** (called at the end of the music interrupt, after the sequencer's events and fades, every tick whether music
plays or not; skipped while the engine is changing its tables): for each sounding slot not on a hold step, ticks
left -= 1; at 0: note off (velocity 0) for the current key on its channel(s); then a glide re-strikes (§3.1), else a
step with flag 0x01 starts the next step, else the effect ends (release). Timing is therefore in **1/60 s** on all
three drivers (the PC-speaker driver runs the PIT at 7680 Hz and calls the music interrupt every 128th tick).

### 3.5 Controller change (0x2b5c:0x652; slot, handle, value, controller) - unused by GAME.EXE

Volume (7) and pan (10) are stored in the slot; for a stereo pair a pan change re-sends the two volumes; otherwise the
controller goes to the effect's channel(s) if it still owns them.

### 3.6 Stop one / stop all (0x2b5c:0x82e / 0x89c / 0x8e5)

Stop one (slot or all slots, optional handle check): each channel it owns is **handed back to the music at once**:
release, then All Notes Off again, the music's remembered program, volume (scaled) and pan, owner := music. No note
off is sent for the effect's key (All Notes Off does it). Stop-all (0x8e5, the one GAME.EXE uses) clears all 32 slots
and hands back every effect-owned channel 2-9 the same way. SAVAGE.EXE stops each spark's sound as its burst ends.

## 4. The three drivers (what a note/program/controller becomes)

### 4.1 Roland MT-32 (STRAX.DRV item 0) - probable
- Every call goes out as a MIDI message unchanged: channel n -> status nibble n-1; so effects play on MT-32 parts
  2-9 at their default channels.
- At start the driver uploads SAVAGE.TIM item 0 by DT1 SysEx (se_music.md §7.2): 38 timbres into timbre memory 0-37,
  and for each a patch (patch number = its record's last byte - 1) set to timbre group 2 (memory), that timbre, key
  shift 24 (none), fine tune 50 (none), bender range 24, assign mode 0, reverb off. So program p of an effect is a
  custom sound (names below), not a factory one; the songs never select these numbers.
- MT-32 pan runs the other way from General MIDI (publicly documented); GAME.EXE always sends 63 (centre), so only
  the sparks' pan direction depends on it (§6).

### 4.2 AdLib (STRAX.DRV item 1) - probable
The same note/program/volume calls on virtual channels 2-9, into the driver of se_adlib.md: the program is found in the
FM bank (SAVAGE.TIM item 1 has every effect program: 3-23, 40-47, 59-63, and 123-127 for the sparks, the last
layered), the voice comes from the same 9 the music uses (effects and music compete for voices), controller 7 sets
the channel volume, pan is ignored, a program change on the channel cuts its sounding notes.

### 4.3 PC speaker (STRAX.DRV item 2, SAVAGE.TIM item 2) - probable

**One voice.** The driver keeps, per MIDI channel 1-16, the record chosen by the last program change (none = silent),
and the channel and key now sounding.
- Program change (p, ch): if ch is the one sounding, stop. Find the record whose program word equals p (none: the
  channel is silent).
- Note on (key, ch, any velocity): if the channel has a record, start it with that key on that channel (whatever was
  sounding stops). Velocity, volume and pan are ignored.
- Note off: stops only if channel and key are the ones sounding. Controller 123 on **any** channel stops. Bend and
  other controllers: nothing.
So the speaker sounds for the shorter of the record's own length and the descriptor's duration.

**Records (SAVAGE.TIM item 2):** a count byte (28), then 28 records of 16 little-endian 16-bit words:

| Word (byte off) | Name | Meaning |
|---|---|---|
| 0 (0x00) | F | base frequency, Hz |
| 1 (0x02) | n | number of random jitter terms per step (0 = none) |
| 2 (0x04) | W | jitter width, Hz (also transposed) |
| 3 (0x06) | D | random octave drop: each step f is shifted right by a random 0..D (low 4 bits) |
| 4 (0x08) | prog | the program number this record answers to (0x80-0x83: continuation records, reached only by chaining) |
| 5 (0x0a) | P | density, %: below 100 a step sounds with chance (P+1)/101, 100 or more always |
| 6 (0x0c) | S | step period in 1/7680 s |
| 7 (0x0e) | T | duration in 1/7680 s |
| 8 (0x10) | dF | added to F after each step (signed) |
| 9 (0x12) | dW | added to W after each step (W kept >= 0) |
| 10 (0x14) | dP | added to P after each step |
| 11 (0x16) | dS | added to S after each step |
| 12 (0x18) | gap | silence after each T, in 1/7680 s (0 = none) |
| 13 (0x1a) | rep | extra repeats of (T, gap) |
| 14 (0x1c) | kf | key follow mode (signed) |
| 15 (0x1e) | chain | 0, or the program + 1 of the record to start next (same key); not found -> the first record |

**Synthesis**, one PIT tick = 1/7680 s; all arithmetic 16-bit with wrap-around (the effects rely on it: F falling
below 0 wraps, and the logical shifts below turn "negative" values into high ones):
- Start: load F, W, P, S, T, rep from the record; gate the speaker on (PIT channel 2 square wave, port 0x61 bits 0-1).
- Every S ticks (the first on the first tick):
  1. f = F, w = W. Transpose by the key: u = key - 60, negated if kf < 0; u is shifted left by
     sh = (kf >= 0 ? 2 - kf : kf + 3) & 3 (so kf 0 = a semitone per key, 1 = half, 2 = a quarter, -1 inverted
     semitones, ...); u counts 1/48 octave. While u < 0: u += 48, f and w halve (logical shift); while u > 47: u -= 48,
     f and w double. If u > 0: f = (f x T[u]) >> 8 and w likewise, T[u] = round(256 x 2^(u/48)) (the driver's table).
  2. If w != 0, n times: f += rand(0..w) - w/2 (w/2 a logical shift).
  3. If D != 0: f >>= (rand(0..D) & 15) (logical).
  4. If P < 100: the speaker is gated on when rand(0..100) <= P, off otherwise (P >= 100 leaves it as it is).
  5. If f < 19 (signed): f = 32767 (inaudible). PIT channel 2 divisor = 1193182 / f, i.e. a square wave at f Hz.
  6. F += dF; W = max(0, W + dW); P += dP; S += dS.
- Every tick: T -= 1; at 0: if not already in a gap and gap != 0, a gap begins (speaker off for gap ticks); else, if
  rep > 0, rep -= 1 and the record restarts (F, W, P, S, T reloaded, speaker on); if nothing restarted and chain != 0
  the chained record starts with the same key; if still nothing, the speaker goes off. So a record plays
  (T, gap) x (rep + 1), then its chain.
- rand(a..b) is uniform over a..b inclusive, from a 32-bit generator x <- 3x + (x_low16 << 16) seeded 0x87654321,
  taking (new high word) mod (b - a + 1). (Exact sequence matters only for bit-exact tests.)

Which effects have a speaker record: ids 0-16, 26-31 and 33 (programs 3-19, 45-47, 59-61, 63). **No record (silent on
the speaker):** ids 17-25 (programs 20-23, 40-44) and **32 (the machine)**; the sparks' program 127 has none either.

## 5. The effects (GAME.EXE ids, DS:0x2366)

All have velocity 64; GAME.EXE adds volume 127, pan 63. Program = MT-32 patch number = AdLib bank program = speaker
record program. Names are the 10-character timbre names in the user's SAVAGE.TIM item 0. Durations in 60 Hz ticks.
"Speaker" summarises the record (§4.3) as heard at the effect's key; length = what actually sounds.

| id | prog | key | dur | MT-32 timbre | Speaker (record: what it sounds like, length) | Trigger in the game |
|---|---|---|---|---|---|---|
| 0 | 3 | 60 | 60 | Explosion | noise ~450 Hz +-200, sinking, 0.33 s | the control panel breaks; the dark moonstone breaks (one per round, 3 rounds) (se_combat §3g) - probable |
| 1 | 4 | 48 | 60 | Savage | noise ~225 Hz +-100 (an octave down), 1 s | the **tremor**: 4 frames of map-view shake, each frame starts id 1 and stop-all follows the frame at once, so 4 short bursts (0x28cf7). Rolled in the routine at 0x4264 (probably once per world turn: guessed) on level 3 while actor 1's talk-flag byte has 0x04: chance 1 in (d/4 + 2), d = the larger of the party's distances in x and y from (157,137), the trap door of se_levels.md - probable (story meaning of the flag guessed) |
| 2 | 5 | 60 | 10 | Missleweap | crackling noise sweeps ~5 kHz down, 52 ms | a missile's flight (se_combat §3b): the spear of shamap (1), spear (26), throwing axe (27), bow (31), atl atl (37), knife (44), the devices (240, 241) - probable |
| 3 | 6 | 60 | 10 | Boomarang | as id 2 | the boomerang's flight (39) - probable |
| 4 | 7 | 60 | 10 | Explosion | 200 Hz crackle with octave drops, 80% density, 30 ms | the rifle's and the flintlock's shot (40, 50) - probable |
| 5 | 8 | 60 | 10 | Blowgun | as id 2 | the blowgun's dart (32) - probable |
| 6 | 9 | 60 | 60 | Explosion | noise ~300 Hz +-200, sinking, 0.26 s | a grenade bursts (0x19a41) - probable |
| 7 | 10 | 60 | 10 | Oouuch!!!! | ~500 Hz, octave drops, widening, slowing, 39 ms | a blow that wakes: the struck one flashes (0x1a181), not a party member - probable |
| 8 | 11 | 60 | 10 | Touche | as id 7 | the same for a party member - probable |
| 9 | 12 | 60 | 30 | Savage | ~700 Hz buzz, 26 ms | **no caller** |
| 10 | 13 | 60 | 60 | Summon | 600 Hz noise in 26 ms steps, 11 x 65 ms | Motazz with chocolatl: the beast is sought (0x1259e) - probable |
| 11 | 14 | 69 | 60 | Charm | rising noise from ~170 Hz then a falling one (chained), cut at 1 s | Aphazz with chocolatl, someone charmed - probable |
| 12 | 15 | 60 | 60 | Curse | as id 10 | Motazz with pinde, someone cursed - probable |
| 13 | 16 | 60 | 60 | Heal | rising then falling noise (chained), cut at 1 s | Aphazz with pinde, someone healed; and `....You awaken in Intanya's hut` after the leader falls (se_combat §3e) - probable |
| 14 | 17 | 60 | 60 | Protect | as id 13 | `Aphazz protects the party.` - probable |
| 15 | 18 | 60 | 60 | Berserk | as id 10 | Motazz with yopo, someone wild - probable |
| 16 | 19 | 60 | 60 | TruthSpell | tone rising from 300 Hz by 75 Hz a step, steps lengthening from 13 ms, cut at 1 s | Heluzz: `Triolo creates light.`, `Triolo leaves his body...`, and danger sensed - probable |
| 17 | 20 | 60 | 60 | Savage | none | no caller |
| 18 | 21 | 36 | 60 | Generator | none | started by the black staff's flight (21) and by a device's burst, and **replaced at once** by id 30 / id 29 (same handle): at most a click - probable |
| 19-24 | 22, 23, 40-43 | 60 | 60 | Tarpit | none | no caller |
| 25 | 44 | 60 | 60 | WaterBells | none | no caller |
| 26 | 45 | 48 | 90 | Crash!!!! | 26 ms buzz then (chain) sparse noise 0.33 s | no caller (the only glide: down to key 0, a semitone per 1.5 s) |
| 27 | 46 | 60 | 60 | Success | as id 7 | the beast comes: `Success!` - probable |
| 28 | 47 | 60 | 60 | Failure | ~700 Hz buzz, 26 ms | the player's A)ttack: `Out of range!` (0x1b9e7); a boulder that can move no more (quality 1, se_use §5c) - probable |
| 29 | 59 | 77 | 90 | Fire Ext | ~4 kHz tone (1500 Hz +17 semitones) randomly gated at 50%, 0.39 s | the fire extinguisher: `You put the fire out.` (fire, burning logs), the lava hardened (se_use §5o); a device's burst (after id 18) - probable |
| 30 | 60 | 60 | 60 | Bonfire | low sparse crackle, octave drops, 0.59 s | the flight of a grenade (137, 138) and of the black staff's bolt (21, after id 18) - probable |
| 31 | 61 | 53 | 60 | Tree | ~33 Hz noise 39 ms + 33 ms gap | a tree falls (se_combat §3f); a boulder's step crushes or shoves an actor, dams the waterfall, or reaches water (se_use §5c); a big creature (se_big) is laid down as its remains (0x18764); a **melee** blow struck within 6 tiles of the party (0x1b47d) - probable |
| 32 | 62 | 60 | 60 | Lever | none | `You put a gem in the machine.` (se_use §5r) - probable |
| 33 | 63 | 60 | 60 | Fizzle | noise ~450 Hz +-200 sinking, 0.78 s | an invocation that does nothing: `Motazz aids only those who are in mortal combat.`, `No effect.` (nobody near: it follows and cuts the effect's own sound), `Triolo senses no danger.`, `No help available.`, `Failed.` (no beast) - probable |

Notes: the flight sound is chosen by the weapon's object number from a 15-entry table (0x19b5a); a weapon not in it
flies in silence. The patches named
"Tarpit" (19-24), "WaterBells" and "Crash!!!!", and 17's "Savage", are installed but never played by GAME.EXE.

Nothing in the choice of an effect is random; only the speaker synthesis is.

## 6. SAVAGE.EXE: the fireworks (design/se_origin_fx.md §9)

The same engine (0x8e4:0x4da, byte-for-byte the same code but for DS offsets). Each spark's first frame starts
descriptor DS:0x200e: one step, **program 127** (MT-32 timbre 37 "Explosion", layered on AdLib), key 64, velocity 64,
60 ticks; volume 127, pan = 127 - 127 x spark_x / 319 (integer), handle = the spark's slot (0-29), priority 1; the
slot's sound is stopped when its burst ends (frame 7). With MIDI pan as the MT-32 reads it (reversed), 127 is the left:
sparks on the left sound on the left (probable). Up to 8 sparks sound at once (channels 2-9), each newer one stealing
the oldest-quiet channel from song 19 or from an older spark. Patches 123 ("fireworks"), 125 and 126 ("Explosion")
are installed but no descriptor uses them (guessed: none found in SAVAGE.EXE). No speaker record for 127.

## 7a. As built in u6e (2026-11-23)

- engine/sesfx.c is §3 as written (32 slots, channels 2-9 with the free and used lists and priorities, the music's
  claiming as a gate the song's events pass through, stop one / stop-all), minus the stereo pair (call-flag 0x01,
  never passed); and §4.3's speaker as a second output. GAME.EXE's 34 descriptors and the spark's are in its table.
- The game's calls only queue a request; everything is sent from the timer interrupt, which runs at 240 Hz while an
  SE song (or its DMA shadow) is set up: the speaker model every tick (32 of its 1/7680 s ticks), the song and the
  effect tick every 4th (59.94 Hz, as before), MIDI OUT 12 bytes a tick.
- MIDI OUT, MT-32: SAVAGE.TIM item 0 is uploaded once before the first song (76 DT1 messages, 30 ms apart; the pack
  carries the item as group (8, 0), from the user's file); the effects go out as §3.4's messages. Checked in Hatari
  (TT): the upload's 76 messages have good checksums, and the opening's sparks go out as program 127 on channels
  2-9 with their pans.
- YM2149: the speaker's tone becomes voice C (period 125000 / f, volume 13; off while the gate is off or f is the
  silent 32767). While an effect sounds, PSG music folds to voices A and B. Checked in Hatari (TT and Falcon,
  SESFX.REQ): all 34 effects go to voice C over song 0.
- General MIDI and DMA music send the effects to the YM2149 too (our choices, rules/se_deviations.md).
- tools/sesfxtest.c (`make sesfxtest`) checks the channel rules on a recorded stream and writes each effect through
  the speaker model as a WAV (build/sfx/). The traces there match §5's descriptions (e.g. id 16 rises from 300 Hz by
  75 Hz a step; id 0 sinks from about 450 Hz).

## 7. Building it on the Atari (suggestions, not decided)

- **Common:** an effect sequencer exactly as §3 (32 slots, the channel lists and priorities, the 60 Hz tick after the
  song's events), sitting between engine/smf.c and the output; the song sequencer must keep §3.3's ownership (drop
  its events on an effect-owned channel, re-claim afterwards). The descriptor table (§5) is 34 small entries and can
  be written from this spec; the timbre and speaker data come from the user's SAVAGE.TIM at pack time (never shipped).
- **MIDI OUT, MT-32:** at start send the SAVAGE.TIM item 0 upload (timbre + patch memory, se_music.md §7.2), then the
  effects as §3.4's messages on channels 2-9. Exactly DOS.
- **MIDI OUT, General MIDI:** custom timbres cannot be uploaded; a GM substitute program per effect is our choice
  (e.g. a GM drum note on channel 10 or a melodic program near the name). To be listed in se_deviations.md if built.
- **YM2149:** rebuild §4.3 on one PSG voice: tone period = round(125000 / f) for the ST's 2 MHz clock (f >= 19 Hz; the
  silent 32767 Hz becomes the voice off), the gate as the voice's mixer/volume bit, the jitter and octave drops per
  step. Steps are 1/7680 s units; an MFP timer at that rate is costly on a 68000, so either run the recipe at a coarser
  rate (steps under ~1 ms collapse into noise: the YM noise generator with its period from f is the natural stand-in
  for records with large W or D), or pre-render each of the 23 records on the host into short PCM/YM register
  streams. Which voice the effect takes from the music's three is our choice (DOS steals the quietest part, §3.3).
- **DMA (OPL2 pre-render):** an effect could be pre-rendered per id through tools/se2pcm.c's AdLib model and mixed,
  but that needs a second DMA stream or mixing; deferred.

## 8. Open questions

- [ ] Listen in DOSBox (MT-32 emulation and the speaker) to every id: the names "Tree" for the blow/boulder/tree
      sound and "Lever" for the machine are as read, not heard.
- [ ] The tremor: is each burst audible (start then stop-all within one frame)? Measure in DOSBox. Meaning of actor
      1's talk-flag bit 0x04 that enables it.
- [ ] Id 18 before 30/29: inaudible or a click on the MT-32?
- [ ] MT-32 pan direction for the sparks (and whether SAVAGE.EXE also sends effects on the PC speaker driver).
- [ ] Hear how the borrowed music part comes back on the MT-32 (the re-claim re-sends the music's program because the
      effect's was the last one sent there).

Placement in the engine (2026-11-23), where our code differs from DOS's shape:
- [ ] The tremor (id 1, 0x4264 / 0x28cf7) is not built in the engine, so id 1 is never played.
- [ ] Ids 7/8 sit in the shared `hit_flash` (SE only), so they sound wherever our engine flashes a struck actor; DOS's
      0x1a181 also needs DS:0xde0 set (not identified) and a struck actor within 5 tiles, as `hit_flash` does.
- [ ] The melee 31 and the flight sounds go in the shared attack paths (`do_attack`, `npc_attack`) just before the
      blow or the flight; DOS plays them inside its blow 0x1b47d after the hit roll. The melee test uses our
      leader position and a box of 6 (DOS: 0x18168's distance, taken as the larger axis).
- [ ] The big creature's 31 (0x18764) is in `se_lie` (sleep) and `se_dying` (death with remains); DOS's own test is
      0x130f:0xca / 0x130f:0x70, taken here as "a big creature" (se_big.md).
- [ ] Boulder 28 (quality 1) is played on every blast that meets a settled boulder, as DOS's test at 0x195ea reads.
- [ ] Stop-all at the level change is after the party is placed (`se_level_go`), at sound off in the shared
      `music_sound_toggle` (Ctrl-Z).
- [ ] The A)ttack aimed into the status display (`attack_panel`, "Out of range!") plays nothing: not read in DOS.

## 9. Test hooks

None yet. Proposed: a host test that feeds the §5 table and a recorded sequence of song events through the effect
sequencer and checks the emitted MIDI stream (channel stolen = the least-recently-active, All Notes Off on take and
release, the music's re-claim at its next event); a host render of the 23 speaker records compared by spectrogram
with a DOSBox speaker capture.

## Status

BUILT (2026-11-23) but for the tremor (id 1, its routine not built); read from the code; not yet compared with DOS by ear.
