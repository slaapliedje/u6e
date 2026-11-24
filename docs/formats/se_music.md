# Format: Savage Empire music (`MUSIC.LZC`, `SAVAGE.TIM`, `STRAX.DRV`) and how DOS plays it

Games: Savage — SE only (U6's music is the OPL `.M` format, music.md; MD not yet looked at)
Implemented by: nothing yet (checklist item 9, design/se_md_checklist.md). Intended: /tools (pack the
songs + FM bank), /engine (an SMF sequencer beside engine/music.c), /render (SE's song choice hook).
Tracked in: /docs/INDEX.md  ·  Milestone: M9 (SE/MD) on the M10 audio backends

> Provenance
> - Derived from : the user's own DOS Savage Empire (`refdata/dos/savage/SAVAGE/`): `MUSIC.LZC`,
>                  `SAVAGE.TIM`, `STRAX.DRV`, `TM.DRV`, `SUPERTM.DRV`, `CONFIG.SE`, `INSTALL.EXE`
>                  strings; the GAME.EXE image `~/src/u6e-re/dos/se.bin` (Turbo C++, DGROUP at image
>                  0x2fa60, so DS:X = image 0x2fa60+X; string segment 0x2f65 = image 0x2f650) and the
>                  SAVAGE.EXE image `~/src/u6e-re/dos/savage.bin` (DGROUP 0xdaa0). Every address below
>                  is an image offset unless written DS:X or seg:off.
> - References   : the Roland MT-32 factory timbre list (public) to name programs; no engine source read
> - Tools        : objdump -m i8086; scratch Python (LZC/LZW unpack with our LZW, an SMF walker that
>                  knows SE's 0xFE events, a census of all 20 songs, a loop-target checker)
> - Validated by  : all 20 songs walk cleanly to the end of every track with the 0xFE rule in §3.3;
>                  every 0xFE jump in every track lands exactly on an event boundary, and all tracks of a
>                  song jump at the same tick to the same tick (§3.4); track names against program numbers
>                  match the MT-32 factory timbre names (§6). NOT YET heard against DOS.
> - Confidence   : SMF dialect + 0xFE events + player rules = confirmed from the code and checked on the
>                  data; song choice = confirmed for the code paths named, probable for the game meaning
>                  of a few flags; FM bank layout = probable (main fields confirmed, effect bytes not decoded)
> - Last updated : 2026-11-22

## 1. Overview
Savage Empire's music is **18 + 2 Standard MIDI Files** (format 1, 480 ticks per quarter note) kept
LZW-compressed in `MUSIC.LZC`. They were written for a **Roland MT-32**: melodic parts on MIDI channels
2-9, rhythm on channel 10, programs are MT-32 factory timbre numbers, no SysEx. The files carry a few
**non-standard 0xFE events** (Origin's own) for loops, exit points and a "now playing" number that the
game polls. GAME.EXE plays them through one shared player (code segment 0x2b5c, image 0x2b5c0) and one
of three drivers from `STRAX.DRV`: Roland MT-32 (MPU-401), AdLib (OPL2 FM, with its own 86-instrument
bank in `SAVAGE.TIM`) or the PC speaker (sound effects only, no music). Music needs **EMS**: the songs
are held in expanded memory, and without it GAME.EXE prints "No EMS found; no music possible." and
plays none.

## 2. Container facts
- `MUSIC.LZC`, `SAVAGE.TIM` and `STRAX.DRV` are all LZC libraries (lzc.md), every item LZW-compressed
  (flag 0x01, lzw.md).
- `MUSIC.LZC`: 20 items. Decompressed, each is a complete SMF (`MThd` ... `MTrk` ...).
- `SAVAGE.TIM`: 3 items, one per driver (item n goes with `STRAX.DRV` item n), §7.
- `STRAX.DRV`: 3 items, the drivers themselves (16-bit code), §7.
- `TM.DRV` / `SUPERTM.DRV` are **not sound**: LZC libraries of video drivers that only SAVAGE.EXE
  loads (by display mode, image 0xd46c / 0xd60a). Ignore them for music.
- Endianness: SMF fields are **big-endian** (as in any SMF); LZC/LZW headers and the 0xFE jump word
  are **little-endian** (§3.3). The 68000 can read the SMF fields natively and must swap the jump word.

## 3. The SMF dialect

### 3.1 Header and tracks
- `MThd` length 6; format 1 in all 20; track count 10-19; division 480 in all 20.
- The player (load at 0x2cdf5) accepts format 0..2 and at most 32 tracks, reads the division word
  (big-endian, MThd+12) into DS:0xee10, and finds each `MTrk` by walking chunk lengths (big-endian).
- Track 0 of every song holds the song's name (meta 3), meta 0x54 (SMPTE offset, unused), meta 0x58
  (time signature, unused) and the tempo (meta 0x51). The other tracks are one instrument each, named
  after it (meta 3).

### 3.2 Census of what occurs (all 20 songs)
| Kind | Occurs | Notes |
|---|---|---|
| Channels | 2, 3, 4, 5, 6, 7, 8, 9 (melodic), 10 (rhythm) | channel 1 and 11-16 never used |
| Note on / off | 0x9n / 0x8n | every note-off is a real 0x8n with velocity 64 |
| Program change | 27 programs (§6) | only on channels 2-9; never on 10 |
| Controllers | 7 volume (all songs), 10 pan (songs 4, 18, 19), 1 modulation (song 3), 121 reset-all (song 8) | |
| Pitch bend 0xEn | songs 2, 5, 9, 18, 19 | |
| Running status | songs 5, 8, 18, 19 (319 events in 19) | never across a meta/0xFE in a way that changes it, see §3.3 |
| Aftertouch, channel pressure, SysEx | none | the player can skip them anyway |
| Meta 0x03 track name | every track | |
| Meta 0x06 marker text | songs 1, 13, 14, 17, 18, 19 | composer's notes, **ignored by the player** (§3.5) |
| Meta 0x51 tempo | every song; many changes in 3, 5, 15, 16, 17 (rubato) | honoured |
| Meta 0x54, 0x58 | every song | ignored |
| 0xFE type 0 (jump back) | one per track in songs 0, 1, 2, 3, 4, 8, 18 | §3.3 |
| 0xFE type 1 (exit point) | songs 4 (1 per track), 5, 8 (3 per track), 12 (1 per track) | §3.3 |
| 0xFE type 3 (song number) | every track of songs 0-12 (value = item+1); songs 18, 19 carry seven (0-6) per track | §3.3 |
| 0xFE types 2, 4, 5 | none | the player knows them (§3.3) |
| Songs 13-17 | no 0xFE at all | they play once (§3.5) |

### 3.3 The 0xFE event (confirmed: dispatcher 0x2c49a, handlers 0x2c4d3-0x2c74c)
Encoding inside a track, after the delta time:

`FE  type  len  data[len]`

- `type` is one byte, `len` is **one plain byte** (not a variable-length number; every length in the
  data is 0, 1 or 2), then `len` data bytes.
- It does **not** touch running status. Running status is only set by channel status bytes 0x80-0xEF
  (0x2c1c3); 0xFE, 0xFF and 0xF0/0xF7 leave it alone. A generic SMF reader trips here twice: 0xFE is
  "Active Sensing" (a one-byte real-time message) in the MIDI standard, so it reads `type` and `len` as
  running-status data and loses its place.
- 0xF0 / 0xF7 are skipped by their variable-length size (0x2c4bc); 0xFF meta is `FF type varlen data`.

Meanings (DS:0x2bfc is the "ending requested" flag, set by the queue call in §4.2 and cleared when a
song starts):

| Type | len | Data | What the player does |
|---|---|---|---|
| 0 | 2 | `lo hi` = W, a little-endian byte count | If ending is **not** requested: All Notes Off (controller 123) on this track's channel, then move this track's read position **back W bytes from the first data byte** (from `lo`). If ending is requested: skip it. |
| 1 | 0 | — | If ending **is** requested: All Notes Off on channels 2-10 and **stop the song** here (returns "song over", §4.2). Otherwise nothing. |
| 2 | 2 | W | The reverse of type 0: jump back W only when ending is requested. Unused by SE's songs. |
| 3 | 1 | n | DS:0x2bea = n, the "now playing" number the game polls (§4.3). |
| 4 | 3 | `lo hi sel` | Jump back W if `sel` ≠ DS:0x2bec (a selector the game could set); else skip. Unused. |
| 5 | 3 | `lo hi sel` | Jump back W if `sel` = DS:0x2bec; else skip. Unused. |
| other | len | — | skipped |

After any 0xFE event (jump or not) the next thing read is the following delta time. A jump target is
always the start of a delta time.

The "track's channel" for the type-0 All Notes Off is the channel of the track's running status
(DS:-0x1210[track] & 15, +1).

### 3.4 How the loops sit in the data (checked on every track)
Each looping song has one type-0 jump in **every** track, all at the same tick, all landing at the same
tick, so the tracks stay in step:

| Song | Exit points (type 1) at tick | Jump at tick → to tick | Plays |
|---|---|---|---|
| 0 | — | 122851 → 0 | loops, 160 s |
| 1 | — | 230541 → 0 | loops, 240 s |
| 2 | — | 200652 → 0 | loops, 111 s |
| 3 | — | 98958 → 0 | loops, 94 s |
| 4 | 46718 | 130444 → 0 | loops, 136 s |
| 5 | 15279, 24882, 34485 | — | once, 38 s |
| 8 | 21190, 32480, 51820 | 53760 → 0 | loops, 56 s |
| 12 | 8160 (its end) | — | once, 8.5 s |
| 18 | — | 103680 → 42240 | intro, then loops |
| all others | — | — | once |

Bytes after a track's jump are unreachable (some tracks carry note tails there). Durations are
computed from the tempo map.

### 3.5 Text markers are not loop marks
The meta-6 texts ("Loop to here", "Loop from here to \"loop to here\"", "Marker 1", "Loop to \"marker 1\"
as needed from here, then continue", "Loop to start from here", the title-screen cue names in 18/19)
are the composer's notes. The meta handler (0x2c000) acts only on 0x51 (tempo); 0x2F (end of track)
is handled at 0x2c786; every other meta type is skipped. So in DOS:
- songs 13-17 (no 0xFE) **play once and stop**, whatever their texts say;
- songs that loop do it only through the 0xFE type-0 jumps above (which sit where the texts say).

### 3.6 Channel events in the player (0x2c200-0x2c497)
- Channel number = (status & 15) + 1, so 1..16; SE's songs use 2..10.
- 0x8n note off → driver note call with velocity 0. 0x9n → note call with the velocity (velocity 0 =
  off). On the AdLib driver only, a note on **channel 10** is first remapped (§7.3).
- 0xAn: two data bytes read, nothing sent.
- 0xBn: sent to the driver; controller 7 is first scaled by the master volume
  (value × DS:0x2bf2 >> 8, 0x100 = unity); the player remembers controller 7 (DS:0x2dd1[ch]) and 10
  (DS:0x2ddc[ch]) per channel.
- 0xCn: sent, and remembered (DS:0x2de7[ch]) so it can be re-sent (below).
- 0xDn: one data byte read, nothing sent. 0xEn: both bytes sent to the driver's bend call.
- Channel claiming: the first channel event on a channel the music does not yet hold (DS:0x2e0c[ch] = 0,
  or DS:0x2dff[ch] < 0, i.e. a sound effect has it) first sends All Notes Off, re-sends the remembered
  program (not on channel 10), volume (scaled) and pan, then marks the channel held. This is how the
  music and the MT-32 sound effects share the synth.
- Output gate: when music is muted (DS:0x2bf0 = 0, set by 0x2d0d5) events are still read and timed
  but nothing is sent; unmuting re-sends program, volume and pan on channels 2-10.

### 3.7 Timing (confirmed: tempo 0x2c016, scheduler 0x2c0c5/0x2c175, interrupt 0x2c868)
- The PIT is set to **60 Hz** (0x2ccff: 1193182 / 60) for the Roland and AdLib drivers (7680 Hz for the
  PC speaker driver, which plays no music). The music interrupt chains INT 8.
- Tempo: on meta 0x51 with µs-per-quarter T, the player sets
  `scale = floor( floor(T / division) × 4887 / 1243 )`. 4887/1243 ≈ 65536/16 669 µs, so `scale` is
  "60 Hz interrupts per MIDI tick" in 16.16 fixed point. Integer truncation as written.
- Scheduler: each track keeps "ticks until my next event". The player takes the track with the
  smallest wait, adds `wait × scale` to a 32-bit accumulator (DS:0x2e1d/0x2e1f), subtracts that wait
  from every track, and while the accumulator is below 0x10000 it plays that track's event and reads
  its next delta. Each 60 Hz interrupt runs that loop and then subtracts 0x10000. Ties go to the
  lowest-numbered track.
- A song ends when every track has reached meta 0x2F, or at an exit point (§3.3).

## 4. The player's game interface

### 4.1 Songs in memory
At start (0x2701a → 0x270ab) GAME.EXE loads **MUSIC.LZC items 0..17** (loop bound 0x12 at 0x2718b) into
EMS, one far pointer per song at DS:0xecbc + 4×n. **Song number = LZC item index, 0-based.** Items 18
and 19 are only loaded by SAVAGE.EXE (§5.3). The 0xFE type-3 number in songs 0-12 is item+1.

### 4.2 Calls (segment 0x2b5c)
| Call | Image | What it does |
|---|---|---|
| play now (song) | 0x2cfb4 | stop at once (All Notes Off, controller 123, on channels 1-16), load, start: controller 121 (Reset All Controllers) on channels 2-10, clear DS:0x2bea and the ending flag |
| fade to (song, n, flags) | 0x2cfe0 | fade the current song out linearly over n 60 Hz ticks (n = 15, 30, 60, 90 or 120 in SE), then stop it and start `song` at full volume; n = 0 → play now |
| queue (song, flags) | 0x2d04c | set "ending requested" and remember `song`: the current song plays on until its next exit point (type 1) or its end, ignoring its type-0 loops; then `song` starts at once. With flag 4, the song that was playing is queued again behind `song`, so a one-shot interlude returns to it (from its start) |
| fade out (n) | 0x2ca2d | as fade-to with no next song |
| master volume (v) | 0x2d08b | 0x100 = unity |
| mute (on/off) | 0x2d0d5 | the output gate of §3.6 |

The fade sets master volume to `(level>>8) × base >> 7` each tick, with `level` stepping from 0x7FFF
down by 0x8000/n, and re-sends controller 7 on the held channels 2-10.

### 4.3 GAME.EXE's own wrapper (segment 0x2701)
- **play(song)** (0x271a0): does nothing when music is off (DS:0xd4 = 0). song −1 → fade out over 60
  ticks (1 s). Else if DS:0x2bea = 0 (nothing playing, or the current song has no type-3 number, i.e.
  songs 13-17) → play now; otherwise fade to it over 60 ticks. There is **no check for "already
  playing"**: asking for the current song fades it out and starts it again.
- **wait for song n**: several callers start a song with fade-to and then loop up to 100 times
  (~0.13 s each, a PIT busy-wait, keeping the mouse pointer alive) until `DS:0x2bea − 1 = n`, i.e. the
  new song's type-3 event has played, and only then queue the follow-on song. A song without a type-3
  number would never satisfy this; SE only does it with songs 4, 6, 7, 11, 12, which have one.

## 5. Which song plays when

### 5.1 The songs
Name = track 0's meta-3 text, as the composer typed it.

| # | Name in the file | GAME.EXE uses it for |
|---|---|---|
| 0 | Datytime-Jungle Book Jingle | surface, by chance (§5.2); requested once more after the main loop returns (quit path, 0x3564); also a SAVAGE.EXE song |
| 1 | Nighttime | surface, by chance; surface after waking from death; after the morning wake-up song |
| 2 | Ant Tunnels: 'Orrible | underground, every level except 3 |
| 3 | Underground: Echoes and Gekkos | underground level 3 |
| 4 | Exploring underground: Atlantis | waking from R)est on the surface between 05:00 and 11:59 |
| 5 | Something to the East | "You hear something to the <direction>!" interlude |
| 6 | Charge- 12-tone fury | combat begun by the player ("Begin combat!") |
| 7 | Battle-LeapStomp | combat forced by a hostile hitting a party member |
| 8 | Going Normal | the combat loop |
| 9 | Going badly-Regroup | combat turns bad |
| 10 | Going Well | combat turns good |
| 11 | Defeat-Too Bad You Got Killed | combat ended badly; falling unconscious |
| 12 | Victory-You Kicked Butt | combat ended well; the dark moonstone's scene |
| 13 | Happy Jungle Dance-Black Magic | **no request found** in GAME.EXE or SAVAGE.EXE (loaded, never played) |
| 14 | Aela's Theme-Aeia | scene 1, Aiela's (0x1dbfe) |
| 15 | Strange Device-Green Meatball | scene 2, the gem's (0x1dd70) |
| 16 | Death of the City-Triumphant Cra(sh) | scene 3, the control panel's (0x1ded4) |
| 17 | Giant Jungle Drum-Thunder | scene 4, the drum's (0x1e1ac) |
| 18 | Theme: Savage Empire | SAVAGE.EXE title / introduction |
| 19 | Current | SAVAGE.EXE, handed to its animation player (probably the ending's fireworks) |

### 5.2 The place's song (0x27201; called on game start, sound on, level changes, teleports, load, after each scene)
DS:0x27f8 is the party's map level (z), DS:0x2860 the combat-mode flag:
1. z = 3 → song 3;
2. else z > 0 → song 2;
3. else in combat → song 8;
4. else **random 0 or 1** (0x2b57:0x1f, uniform in [0,1]) → song 0 or song 1. The hour is not consulted.

Then play(song) (§4.3).

### 5.3 Situations (each guarded by "music on", DS:0xd4)
- **Combat begun by the player** (0x1822a, from the combat command after "Begin combat!", and the
  scripted start at 0x1dd41): fade to 6 over 15 ticks, wait for 6, queue 8. Song 6 has no exit point,
  so it plays out and 8 follows.
- **Combat forced** (0x1dac8, called at 0x1b2f7 when an attacker with actor flag 0x20 hits a defender
  with flag 0x80 while not in combat): fade to 7 over 15, wait for 7, queue 8. (Flags in DS:0x6cf0[actor];
  0x80 reads as "party member", 0x20 as "hostile": probable.)
- **The tide of a fight** (DS:0xde4, 0 at combat start): when an actor dies in combat (0x1a715), a
  0x80-flagged actor sets it to −1; a 0x20-flagged one sets it to +1 if it is still 0. The main loop
  (0x5bd9) sees ±1 in combat and calls 0x2746e, which doubles it (so it fires once per change) and,
  **only if song 8 is the one playing**, queues 9 (negative) or 10 (positive) with flag 4: song 8 runs
  to its next exit point, the tide song plays once, then 8 starts again.
- **Combat ends** (0x1833b, "Break off combat!" and every other end; 0x27365 with the tide): tide < 0 →
  fade to 11 over 30, wait, queue the place's song; tide > 0 → same with 12; tide 0 → fade straight to
  the place's song over 120 ticks (2 s). The place's song here is z = 3 → 3, z > 0 → 2, else random 0/1.
- **Falling unconscious** (0x27566; skipped when DS:0x27f2 = 0x10, meaning not decoded): fade out over
  90 ticks, "Overcome by your wounds...", play 11 now, wait for 11, queue the place's song with the
  surface fixed to **1** (not random), "...You awaken in Intanya's hut".
- **Morning wake-up** (0x2724f, from the end of R)est at 0x2a808, only when sound is on and the driver
  is not the PC speaker): hour (DS:0x27eb) 5..11 and z = 0 → fade to 4 over 60, wait for 4, queue 1.
  Song 4's exit point at tick 46718 (~49 s) is where it hands over to 1.
- **"You hear something to the <dir>!"** (0x274b7, called at 0x26f0b when an unseen hostile appears):
  not in combat and the current song number ≤ 4 (an outdoor/underground song) → queue 5 with flag 4.
  The current song runs on to its next exit point or its end before 5 starts. Only song 4 has an
  exit point; songs 0-3 loop and have none, so as read the queued 5 would not play until some other
  call changes the song. Not yet checked against DOS (§9).
- **Scenes** (se_scenes.md): each routine calls play(14 / 15 / 16 / 17 / 12) as it starts (a 1 s
  fade from the place's song); afterwards play(place's song), which cuts in at once because songs
  13-17 have no type-3 number. Scene 5 (12) ends the game.
- **Sound off / on** (the toggle at 0x570e, "Sound off" / "Sound on", DS:0x2810): off → play(−1) (1 s
  fade) and stop the sound effects; on → the place's song.

Correction for other notes: **0x2a81:0x98 is not a song call.** It is the floppy prompt "Please insert
%s." with DS:0x2a64[n] = "DISK 1/2/3", skipped when that disk is already in. The per-file table at
DS:0x27c6 that se_scenes.md lists as the scenes' "Song" column is the disk each MIDGAME file lives on.
Likewise 0x2abf:0x13d / 0x14e are show / hide mouse pointer and DS:0xd8 is "mouse present", not music.
The music flags are DS:0xd4 (music possible: driver supports it and EMS loaded) and DS:0x2810 (sound
on/off).

### 5.4 SAVAGE.EXE (front end)
It contains the same player (the 0xFE dispatcher's bytes recur at savage image 0x9d25) and loads
MUSIC.LZC items **18, 0 and 19** (0x3a84-0x3abe). It plays 18 at the introduction's film (0x429a; it goes on into the menu), passes 19 to the Origin FX
opening (0x419e; design/se_origin_fx.md), and plays 0 at the ending (0x5ebf). The menu's call for song 0 (0x4a82)
is reached only when DS:0x353e (music on, 0 or 1) is 0xffff, so it never plays (read 2026-11-23). DS:0x20ce holds the
last FE type-3 number passed (0 at a song's start), not a count. Songs 18 and 19 carry type-3 numbers 0-6 at the cue
points the composer named in meta-6 texts (18: first title screen 0, second title screen 1, dinosaur
screen 2, melody 3 ...; 19: start tuning 0 ... start fireworks 3 ...), which is how the front end keeps
its pictures in step with the music.

## 6. MT-32, not General MIDI (confirmed)
- Channels 2-9 + 10 is the MT-32's default part layout; channel 1 is never used.
- The composer named every instrument track, and the names match the **MT-32 factory timbres** for
  the program numbers, not GM: 92 "Fr. Horn" (MT-32 Fr Horn 1; GM 92 is a pad), 112 "Tympani" (MT-32
  Timpani; GM Tinkle Bell), 122 "Orch. Hit" (MT-32 Orche Hit; GM Seashore), 117/118 "Taiko"/"Taiko Rim",
  33 "Harmo Pan", 107 "Shakuhachi", 58 "Harp", 24 "Synbrass".
- Programs used (0-based, MT-32 name): 0 Acou Piano 1, 2 Acou Piano 3, 24 Syn Brass 1, 25 Syn Brass 2,
  33 Harmo Pan, 48 Str Sect 1, 50 Str Sect 3, 58 Harp 2, 65 Acou Bass 2, 72 Flute 1, 73 Flute 2,
  75 Piccolo 2, 83 Clarinet 2, 84 Oboe, 92 Fr Horn 1, 93 Fr Horn 2, 97 Vibe 1, 104 Marimba,
  107 Shakuhachi, 112 Timpani, 113 Melodic Tom, 117 Taiko, 118 Taiko Rim, 119 Cymbal, 121 Triangle,
  122 Orche Hit, 124 Bird Tweet.
- Rhythm keys used on channel 10: 36-38, 41-51, 56, 59-70, 73, 75 (MT-32 rhythm set).
- No SysEx in any song. The MT-32 **driver** sends some at start (§7.1), but it only reprograms patch
  numbers the songs never select.

## 7. The drivers and their data

### 7.1 Which driver (CONFIG.SE, parsed at 0x2dbd)
CONFIG.SE is a short text line (the user's: `v m a388`). Each character is upper-cased and looked up
in a 10-entry table (image 0x2e6b); others (digits, spaces) are ignored:

| Letter | Effect |
|---|---|
| V / E / T | video VGA / EGA / Tandy (DS:0xd2 = 0/1/2) |
| M / K | mouse / keyboard (DS:0xd8 = 1/0) |
| N | no sound: DS:0xd4 = 0, driver 0, sound off |
| A | **AdLib**: music on, driver type 2 |
| R | **Roland MT-32**: music on, driver type 1 |
| P | PC speaker: no music (DS:0xd4 = 0), driver type 3 |
| $ | DS:0xd6 = 1 (not sound related) |

INSTALL offers "No sound / PC speaker / AdLib / Sound Blaster / Roland MT-32 or LAPC-1"; GAME.EXE
has no Sound Blaster letter, so the Sound Blaster choice must write `A` (its FM part is AdLib
compatible; probable). The port digits after `A` are not read by GAME.EXE; the AdLib driver uses
0x388 (its port word at driver offset 0x2d, settable through an entry GAME.EXE never calls).

Driver type t loads `STRAX.DRV` item t−1 and `SAVAGE.TIM` item t−1 (0x2ca87). Each driver starts
with a table of near entry points (offsets +2 .. +0x14): init, load bank, shut down, timer, note
(velocity, key, channel), controller (value, controller, channel), pitch bend, program (program,
channel), and two more.

### 7.2 Roland MT-32 (STRAX.DRV item 0, SAVAGE.TIM item 0)
- MPU-401 at 0x330, reset and UART mode; then Roland DT1 SysEx (`F0 41 10 16 12 addr data sum F7`):
  the text "Origin Sound System!" to the display (address 20 00 00), and the bank.
- SAVAGE.TIM item 0: a count byte (38), then 38 records of 247 bytes: a 246-byte MT-32 timbre (10-char
  name + common + 4 partials) and one byte p. Timbre k goes to timbre memory k (08 2k 00); patch p−1
  (05 ...) is set to "memory timbre k" (key shift 0, fine tune 0, bend range 24 as sent). The patches
  touched are 3-23, 40-47, 59-63, 123, 125-127: the **sound effects** (names like Explosion, Blowgun,
  Heal, Tarpit, Lever, fireworks). None of these are programs the songs use, so the songs hear the
  factory timbres.
- Notes, controllers, programs and bend go out unchanged.

### 7.3 AdLib (STRAX.DRV item 1, SAVAGE.TIM item 1)
The whole driver (voices, TL, pitch, the timer's envelope and vibrato, controllers) is in **se_adlib.md**,
which also corrects a few points below (its §12).
**Drum remap (in GAME.EXE, for driver type 2 only, 0x2c2fc / 0x2c367):** a channel-10 note with key k is
sent instead as note `DS:0x2e6e[k]` on virtual channel `DS:0x2e21[k]` (keys outside the table, or with
channel 0 there, go nowhere). At init (0x2cc23) the player gives each virtual drum channel its own
program from the AdLib bank:

| Virtual ch | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Program | 128 | 114 | 131 | 113 | 134 | 135 | 133 | 132 | 129 | 136 | 141 | 143 | 144 | 145 | 147 | 140 | 139 |

| Key → (ch, note) | | | |
|---|---|---|---|
| 35, 36 → (10, 48) | 37 → (18, 48) | 38 → (11, 48) | 40 → (12, 48) |
| 41, 43 → (13, 42) | 42 → (17, 71) | 44 → (16, 71) | 45, 47 → (13, 47) |
| 46 → (14, 71) | 48, 50 → (13, 52) | 49 → (15, 79) | 51 → (19, 77) |
| 56 → (21, 71) | 60 → (26, 72) | 61 → (26, 79) | 62 → (25, 79) |
| 63 → (20, 64) | 64 → (20, 58) | 67 → (21, 89) | 68 → (21, 84) |
| 69 → (22, 48) | 70 → (23, 72) | 73 → (24, 36) | 75 → (20, 96) |

Keys 59, 65, 66 (used 41 times in all) are unmapped: silent on AdLib.

**The FM bank (SAVAGE.TIM item 1, 4129 bytes):** a count byte (86), then 86 records of 48 bytes. The
driver keeps a pointer to the records and finds a program by scanning for it (0x68e); a program
**not in the bank falls back to the first record**. Songs use three programs the bank lacks (25, 73,
97), so on AdLib those parts sound with record 0's patch. The bank holds programs 0, 2-24, 33, 40-48,
50, 58-65, 72, 75, 83, 84, 92, 93, 104, 107, 112-114, 117-119, 121-129, 131-136, 139-141, 143-145,
147-155: the music's programs, the sound effects' (the same numbers as on the MT-32) and the drums
(128 up).

Record layout (confirmed where the driver code reads it: 0x34d register load, 0x6b5 note, 0x9dd bend,
0xc16 program):

| Off | Size | Meaning |
|---|---|---|
| 0x00-0x04 | 5 | modulator registers 0x20 (AM/VIB/EG/KSR/MULT), 0x40 (KSL/TL), 0x60 (AR/DR), 0x80 (SL/RR), 0xE0 (wave) |
| 0x05-0x09 | 5 | carrier registers, same order |
| 0x0A | 1 | register 0xC0 (feedback / connection) |
| 0x0B | 1 | rhythm-mode slot (6 BD, 7 SD, 8 TT, 9 CY, 10 HH; 0 = melodic). **0 in all 86 records**, so the driver stays in 9-voice melodic mode and drums are ordinary voices |
| 0x0C | 1 | carrier velocity sensitivity s: adds `(63 − velocity) >> (7 − s)` to the carrier's TL (clamped 0..63), then channel volume scales it; 0 = carrier TL only follows channel volume |
| 0x0D | 1 | the same for the modulator's TL (no channel volume); 0 = fixed |
| 0x0E | 1 | pitch-bend range in 1/8 semitones (96 = ±12) |
| 0x0F-0x11 | 3 | per-channel effect settings (0x10, 0x11 kept per channel at program change); probably vibrato; not decoded |
| 0x12-0x13 | 2 | little-endian word set into the voice at note on; probably a pitch-envelope start; not decoded |
| 0x14-0x23 | 16 | four 4-byte groups, rarely non-zero; probably the pitch envelope; not decoded |
| 0x24-0x25 | 2 | little-endian signed pitch offset in 1/256 semitone (high byte = semitones) added to every note |
| 0x26 | 1 | non-zero = **layered**: the note also sounds the **next** record in the bank (one more voice) |
| 0x27 | 1 | key-follow shift: pitch = 60 + (key − 60) >> n (negative n: inverted); 0 = normal |
| 0x28-0x2E | 7 | zero in all records |
| 0x2F | 1 | the program number this record answers to |

Driver start-up writes OPL register 0x01 = 0x20 (waveform select on), 0xBD = 0, 0x08 = 0. Note-on
pitch is computed in 1/256 semitones (key×256 + bend + record offset) and converted to F-number/block
by a table (0x60d).

### 7.4 PC speaker (STRAX.DRV item 2, SAVAGE.TIM item 2)
SAVAGE.TIM item 2 is a count byte (28) and 28 records of 32 bytes for speaker sound effects; this driver
runs the PIT at 7680 Hz and GAME.EXE plays no music with it. Decoded in rules/se_sound_effects.md §4.3 (2026-11-23).

## 8. Feeding u6e's three backends (suggestion, not decided)
What u6e has today (music.md, music_areas.md, milestone.md M10): an OPL `.M` sequencer whose events go
to MIDI OUT (MT-32 or General MIDI through `MT32_TO_GM`), to an OPL2 software synth pre-rendered on the
host into DMA `.PCM`, or folded to the YM2149's three voices; F9 / MIDI.CFG pick the source; the
U6 song choice is a group model in gamedemo.

- **Common**: SE needs an SMF sequencer of its own (480 PPQN, tempo map, the 0xFE rules of §3.3, the
  play / fade / queue calls of §4.2, the "now playing" number). Timing can copy §3.7 exactly on a 60 Hz
  timer (the M10 code already runs Timer-A tempo). The packer can store the 20 decompressed SMFs as
  they are (big-endian already; only the 0xFE jump word is little-endian). Songs 13-17 should stay
  play-once to match DOS. The song choice of §5 becomes SE's music hook (z-level, combat flag, tide,
  random 0/1, the named situations).
- **MIDI OUT, MT-32**: send the events unchanged on their channels; at song start controller 121 on
  2-10, on stop controller 123 on 1-16; master volume through controller 7 as DOS does. No SysEx needed
  for music (the SAVAGE.TIM upload matters only if SE's sound effects go to the MT-32 too).
- **MIDI OUT, General MIDI**: `MT32_TO_GM` must cover the 27 programs of §6 (it was filled for U6's
  set; 0, 25, 48, 58, 65, 72-75, 83, 84, 92, 93, 97, 104, 107, 112, 117-124 need checking/adding);
  channel 10 can go as is (MT-32 rhythm keys 35-75 are close to the GM drum map). Channel layout is
  already GM-safe (drums on 10).
- **OPL2 → DMA (built 2026-11-23)**: `make se-pcm` runs tools/se2pcm.c, which plays each song through engine/smf.c
  into a model of the AdLib driver written from se_adlib.md (the user's SAVAGE.TIM bank, the drum remap, 9 voices),
  its OPL2 writes rendered by engine/oplsynth.c (register level, 4x oversampled), into build/music/SE00-19.PCM (U6PC,
  12517 Hz). A song that loops to its start keeps one warmed pass looped whole (songs 0-4, 8); song 18 keeps its intro
  and two passes, looping on the second; a song that ends gets its release and 0.5 s of silence the DMA loops. 18 MB
  for all 20. In the game the sequencer runs silently beside the PCM (music_play_se_dma), ticked from the frames the
  DMA has played (plat_dma_music_elapsed), so cues, ends and the queue's exit points behave as on MIDI and the PSG.
  The sound effects (rules/se_sound_effects.md, built 2026-11-23) are not in the PCM: beside DMA music they play on
  the YM2149 with the PC speaker's recipes. The original suggestion follows.
- **OPL2 → DMA** (suggestion as first written): render with engine/oplsynth on the host, driving it as the AdLib driver does: the
  SAVAGE.TIM item-1 bank read from the user's files at convert time (never shipped), program lookup
  with record-0 fallback, layering, bend range, velocity rules, the drum remap of §7.3, 9 melodic
  voices. To keep loops and exit points in a pre-rendered file, render each song as intro + loop body
  ([jump target, jump tick)) and store the exit points as sample offsets so a queue can cut at the
  right place. Note this is the AdLib sound, which differs from the MT-32 original (missing programs
  25/73/97, drum keys 59/65/66 silent).
- **YM2149 PSG**: as for U6, fold to three voices; SE's songs are fuller (8 melodic parts + drums),
  so a rule is needed: e.g. melody track by name/priority, bass, and drums as noise on the third voice.
  Our own choice; no DOS reference exists (DOS has no PSG music).

## 9a. Validation method
Host side, done: a walker using §3.3 reaches meta 0x2F on every track of all 20 songs; every type-0
jump in every track lands on a delta-time boundary, and all tracks of a song share one jump tick and
one target tick; program numbers agree with the composer's track names under the MT-32 list (§6).
To do: compare a u6e MIDI capture (Hatari `--trace midi_raw`) with DOSBox + an MT-32 emulator on the
same song, and check the §5 situations in DOSBox (dosbox-u6-rig).

## 9. Open questions
- [ ] Song 5 queued over a looping place's song (0-3) never reaches an exit point, so as read it would
      not play until the song changes. Is that what DOS does? Observe in DOSBox (dosbox-u6-rig).
- [ ] Song 13 has no caller found; check SAVAGE.EXE's other screens and the ending.
- [x] Which SAVAGE.EXE screens play songs 0 and 19 (§5.4), and how its pictures follow the type-3 cues (2026-11-23:
  design/se_front_end.md "The intro", design/se_origin_fx.md).
- [ ] FM record bytes 0x0F-0x23 (vibrato / pitch envelope) and the timer routine (+8) that runs them.
- [ ] DS:0x27f2 = 0x10 skipping the death music; DS:0x2810 vs DS:0xd4 in every path.
- [ ] AdLib channel volume curve (DS:0x1a2[ch] from controller 7) in detail.
- [x] PC speaker driver and SAVAGE.TIM item 2 (sound effects, not music): rules/se_sound_effects.md §4.3.

## 10. Status
IN PROGRESS — format, player rules and song choice written from GAME.EXE and checked against all 20
songs. Built 2026-11-22: engine/smf.c (the sequencer of §3, 0xFE types 0-3, the 60 Hz scheduler of §3.7;
a host run gives the durations of §3.4); the pack carries the 20 songs unpacked (CSFE group 6); MIDI OUT
plays them as they are (controller 121 on 2-10 at the start; General MIDI through MT32_TO_GM), the PSG folds
the melodic channels to its three voices and leaves out the rhythm channel (our choice); DMA falls back to
the PSG until an AdLib render exists. Played: the menu and creation and the ending song 0, the scenes 14,
15, 16, 17, 12, the places (level 3 song 3, other levels 2, combat 8, else 0 or 1 at random). The same day: the queue
(the current song ends at its next exit point or its end, then the next; an interlude returns to the song it
cut), combat begun 6 then 8, the tide's 9 / 10 (only over 8), combat over 11 / 12 then the place's song,
falling unconscious 11 then 1, the morning wake-up 4 then 1, the "You hear something" interlude 5. Our
choices: no fades (DOS fades 15-120 ticks); combat is only begun by the player in u6e, so song 7 (combat forced
by a hostile's blow) is not played. Not yet: the intro's 18 and 19, the AdLib bank render, a listen against
DOS.
