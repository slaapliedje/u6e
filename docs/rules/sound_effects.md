# Sound effects (U6, as DOS plays them)

> Provenance: READING GAME.EXE 4.5, the routine at 26e2:65 and its PC-speaker helpers, and its call sites. Re-expressed, no
> code transcribed. DOS has them regardless of the music card; the ST original has none.

The setting: Ctrl-X turns them on and off (second byte of MIDI.CFG), on by default; Ctrl-Z silences everything as ever.

Each effect is a tone on one voice, its frequency stepped through time; DOS holds the game while it plays:
- Bump, walking into something: 311 Hz for 10 ms.
- A spell that fails (`Failed`, `No effect`, `Blocked`, not enough level or points or reagents), "Stop Thief!!!", a key
  that breaks: a rise from 800 to 2000 Hz over 50 ms (id 13).
- A spell that succeeds: 2000 Hz for 10 ms (id 14).
- A blow landing: noise under 10 kHz for 40 ms (id 4).
- Something broken or killed: noise bursts rising 2-20 kHz (id 15).
- An explosion: 40 steps of tones 20-200 Hz (id 8).
- "dragged under": a rise 1200 to 2000 Hz over 40 ms (id 9).

(The timing of DOS's helper loops is CPU-calibrated; a step here is 5 ms, the 200 Hz clock.)

Instruments: U)se on a harp (157), harpsichord (156), lute (158), panpipes (153) or xylophone (296), then keys 0-9 play it and
any other key ends. The notes: 7851, 3116, 3497, 3926, 4159, 4668, 5240, 5882, 6231, 6995 (phase steps of DOS's 20 kHz speaker
synth, so x 5/16 is Hz; the panpipes play at a quarter of the number, for 200 ms), dying away over about 150 ms.

- A blow struck within 6 tiles of the party, melee only: a rise 400 to 750 Hz over 150 ms (id 17), then the hit's noise.
- A weapon's missile: id 5 per step. A spell's missile: id 16, sparse noise under 1 kHz (DOS 40 steps of a loop much shorter than
  ours; here 4). Hail Storm: id 7 for each stone. Tremor: the explosion's id 8.
- A slug dissolving a thing: 20 short noises 600-4800 Hz (id 11). Regurgitated: noise 600-7000 Hz (id 10). Robbed: id 12.
- The rubber ducky, `Squeak!`: a rise 5 to 8 kHz over 100 ms (id 22).
- The party's death: a fall of 50 notes from about 3 kHz, then one more (id 21).

Id 6, a variant of 16, has no caller in the program.
