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
- A missile hitting: 40 short steps, one in four a tone near 2 kHz (id 5). An explosion: 40 steps of tones 20-200 Hz (id 8).
- "dragged under": a rise 1200 to 2000 Hz over 40 ms (id 9). Something vanishing: id 12, the rise of 13.

(The timing of DOS's helper loops is CPU-calibrated; a step here is 5 ms, the 200 Hz clock.)

Instruments: U)se on a harp (157), harpsichord (156), lute (158), panpipes (153) or xylophone (296), then keys 0-9 play it and
any other key ends. The notes: 7851, 3116, 3497, 3926, 4159, 4668, 5240, 5882, 6231, 6995 (phase steps of DOS's 20 kHz speaker
synth, so x 5/16 is Hz; the panpipes play at a quarter of the number, for 200 ms), dying away over about 150 ms.

Not used: ids 6, 7, 10, 11, 16, 17, 21, 22 (their call sites read as a hit from far, a rumble, and the like, not yet matched).
