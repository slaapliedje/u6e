# Savage Empire (DOS) front end — the "Origin FX" opening: behavioural spec

> Provenance
> - Derived from : SAVAGE.EXE load image (~/src/u6e-re/dos/savage.bin), module 0x6360-0x7420 read
>   instruction by instruction; the VGA drivers TM.DRV item 0 and SUPERTM.DRV item 0 (rotozoom entries);
>   TITLE.LZC items 0-12 (sizes and hot spots read from the shape headers); the tuning song's
>   cue markers (mus_19.bin, track 0 texts); DOS captures sedos/intro/f01..f20 (layout check only).
> - References   : none outside the original files (no reference engine has this sequence).
> - Tools        : objdump (i8086), own python scripts (hot spots, table decode, cue dump).
> - Validated by  : layout cross-checked against a DOS capture (DOSBox, window rows 24-151, globe/logo
>   placement); the engine's build (render/gamedemo.c se_origin_fx) recorded in Hatari (Falcon, 4 MB TT)
>   and compared by eye with the DOS captures. Frame timing NOT validated against a capture.
> - Confidence   : probable (control flow read exactly; meanings of driver arguments partly guessed,
>   marked below)
> - Last updated : 2026-11-23

All addresses are linear image offsets in savage.bin; "DS:x" = image 0xdaa0+x. They are given only so
the facts can be re-checked; nothing below needs them to be reimplemented.

## 0. Summary of the sequence

| # | routine | what is seen | ends when |
|---|---------|--------------|-----------|
| 1 | 0x63b1 | starfield + top of the globe + orchestra silhouettes and the conductor (all frame 0) fade in from black | fade done (≤ 64 retraces) |
| 2 | 0x640c | "tuning": the 9 orchestra sections and the conductor animate from 32-step scripts, ping-ponging | cue ≥ 1 ("Tap on music stand", 5.52 s into the song); without music after one forward + one backward pass |
| 3 | 0x65b5 | the conductor taps the stand 4 times (with a percussion note on the music device), then raises the baton | 20 steps, then (music) holds until cue ≥ 2 ("Start Piano intro", 10.00 s) |
| 4 | 0x695a | orchestra and conductor zoom toward the viewer and slide off the bottom/sides, revealing the globe | 17 accelerating steps |
| 5 | 0x6ac4 | ORIGIN FX logo grows from behind the globe while the globe sinks | 41 drawn steps + 1 final frame |
| – | main | (music) hold the logo until cue ≥ 3 ("Start fireworks", 17.37 s) | |
| 6 | 0x6c80 | sparks (fireworks) behind the logo: sparse, then continuous (cue 4 "More fireworks", 21.16 s), then a final burst of 30 (after cue 5, 23.24 s) | burst finished |
| – | main | music stopped at once, palette fade to black, screen cleared, palette restored | |

Any key aborts at the next check (every animation step and between scenes); the end sequence still runs.

## 1. Environment, surfaces and timing model

### 1.1 Entry and arguments
Entry 0x6fcd(back, screen, video_mode, song). Called with back = surface DS:0x2b52 (off-screen
320x200 buffer), screen = surface DS:0x2b1a (VGA memory), video_mode = DS:0x2b68 (0 = VGA), song = far
pointer to the loaded song, or null when music is off. SAVAGE.PAL palette 2 is set just before.
Every scene routine gets (back, screen, …) and draws into **back**, then "presents".

Surface struct (16 bytes): +0 segment, +2 near pointer to a row-offset table, +4 x0, +6 y0, +8 x1,
+0xA y1 (the clip/window rectangle, inclusive).

### 1.2 The display window (important)
On entry the screen struct is saved (16 bytes on the stack) and its window is set to **y0 = 24,
y1 = 151** (x stays 0..319). The back buffer window stays 0..319 x 0..199.
**Present** (driver entry TM+0x16, DS:0x2bba, args (src = back, dst = screen)) copies a rectangle of
width min(src width, dst width) and height min(src height, dst height) = 320 x 128 from the src window
origin to the dst window origin. So **back-buffer rows 0..127 appear on screen rows 24..151**; the rest
of the screen (rows 0-23 and 152-199) is never touched (stays black). All coordinates below are
back-buffer coordinates; add 24 for the screen row. (Checked against capture f12: picture band at
screen rows 24-152.) Anything drawn below back row 127 is invisible.
At the end the saved screen struct is restored.

### 1.3 "Present step" = 0x6363
Wait for the start of the next vertical retrace (0x7d78: wait while the VGA status bit 3 is set, then
wait until it is set again), then Present. At 70 Hz VGA this makes one drawn step last one retrace
on a machine that can draw a frame in under 1/70 s.

### 1.4 Speed factor (frame skipping)
0x6f89 computes `speed` once: a timer-calibrated benchmark (0xca2e: counts driver draw calls per
18.2 Hz PIT tick, takes the best of 9 ticks, divided by 256) gives r; speed = 2 − r/8, but at least 1.
So **speed = 1 on a fast machine, 2 on a slow one** (slow machines draw only every other step).
Scenes 2-5 keep a countdown that starts at 1: each step it is decremented; when it drops below 1 it
is reset to `speed` and the step is drawn and presented. Steps that are not drawn take no time at all
(no wait). For a reimplementation on a capable target, **use speed = 1** (draw every step, one
retrace per step).

### 1.5 Abort
Byte at absolute 0x7d9:0x21 (image 0x7db1) is the abort flag, cleared at entry. Every animation step
(drawn or not) polls "key pressed?" (0x1bfd, nonzero = yes) and sets the flag to 0xFF; the scene then
returns. main checks the flag after every scene and while waiting for cues, and jumps to the end
sequence. The flag is cleared again before the end sequence.

### 1.6 Music and cues
- DS:0x353e = music enabled; DS:0x20ca = music device (1 = Roland MT-32 = STRAX.DRV item 0,
  2 = AdLib = item 1; 3 = a third driver, item 2, probably PC speaker — unverified).
- The song is started (0x8e4:0x19ff) right after loading, before scene 1, if the song pointer is non-null.
- DS:0x20ce = **the current cue number**: the sequencer resets it to 0 at song start and sets it to the
  data byte of each Origin meta event FE type 3. It is not a counter (it is "the last cue passed").
- The tuning song (mus_19.bin; track 0 texts) has these cues (at its own tempo, 480 ticks/beat):

| cue | time | text in the song |
|-----|------|------------------|
| 0 | 0.00 s | Start Tuning |
| 1 | 5.52 s | Tap on music stand (quiet!) |
| 2 | 10.00 s | Start Piano intro |
| 3 | 17.37 s | Start fireworks |
| 4 | 21.16 s | More fireworks |
| 5 | 23.24 s | (no text) |
| 6 | 25.18 s | (no text; not used here) |

- "Music synced" below means: DS:0x353e ≠ 0 and device is 1 or 2. Scene 2 uses a slightly different
  test (music on and device ≠ 3); see §4.
- Caveat (faithful detail): scene 2 tests "cue ≥ 1" even when music is off. With no song ever played
  the cue is 0, so it never fires; a stale value would end scene 2 at once.

### 1.7 Random numbers
0xd257 random(a, b): uniform integer in [min(a,b), max(a,b)] inclusive, computed as
(rng mod (hi−lo+1)) + lo. The generator (0xd23a) is a 32-bit multiplicative LCG
S ← S × 0x10003 (mod 2^32), output = high 16 bits of the new S (seeded elsewhere; low word forced odd).
Note random(0,0) is always 0.

## 2. Assets (TITLE.LZC) and hot spots

Shapes are the standard U6/SE shape format (docs/formats/shapes.md): a sprite drawn "at (x,y)" puts
its hot spot on (x,y); pixels outside blocks are transparent. Hot spots read from the shape headers:

| item | shapes | size (WxH) | hot spot (from left, from top) | content |
|------|--------|-----------|-------------------------------|---------|
| 0 | 0 | 320x128 | (0,0) | dark-blue starfield background |
| 0 | 1 | 86x58 | (85,25) — right edge | "OR" + "Sound and " (left third of the logo) |
| 0 | 2 | 118x61 | (57,27) — centre | "IGIN" + "Graphics System" (middle of the logo) |
| 0 | 3 | 78x82 | (0,41) — left edge | red "FX" + "tm" (right part of the logo) |
| 1 | 13 | 114x52 | (58,17) | orchestra section: string players at music stands |
| 2 | 9 | 121-123x57 | (64-66,23) | section: double bass / cellos group |
| 3 | 3 | 71x51 | (29,17) | the grand piano and pianist |
| 4 | 7 | 120x40 | (58,13) | section: string players (scrolls of instruments visible) |
| 5 | 5 | 129x34-35 | (66,8-9) | section: players (brass/strings, silhouettes) |
| 6 | 3 | 71x29-30 | (29,3-4) | section: small group of heads |
| 7 | 5 | 116-119x23-24 | (58,4-5) | front row: woodwinds/flutes (instruments angled) |
| 8 | 5 | 129x22-23 | (66,4-5) | front row: players with long instruments |
| 9 | 4 | 47x23 | (29,5) | front row: small group |
| 10 | 21 | 74-142 x 103-115 | (35-72, 37-49) | the conductor, back view (arms down → arms up with baton) |
| 11 | 24 | 12x10 … 68x73 | roughly centred | sparks: 3 colour types × 8 growth frames (shape = type*8 + frame) |
| 12 | 1 | 182x128 | (85,0) — top centre | the globe (planet) |

(The orchestra-section names are visual guesses from the silhouettes; only the numbers matter.)
Item 11 is used through a copy of its pointer (DS:0x3574) because item 12 is loaded into DS:0x3570.
In video mode 3 items 7-9 are not loaded (irrelevant for VGA).

### 2.1 Layout table (DS:0x1e69, 10 records × 4 signed words: x, y, dx, dy)

| record | item | x | y | dx | dy | row |
|--------|------|---|---|----|----|-----|
| 0 | 1 | 58 | 94 | −1 | 1 | back row, left |
| 1 | 2 | 186 | 94 | 0 | 1 | back row, centre |
| 2 | 3 | 278 | 94 | 1 | 1 | back row, right |
| 3 | 4 | 58 | 102 | −3 | 2 | middle row, left |
| 4 | 5 | 186 | 102 | 0 | 2 | middle row, centre |
| 5 | 6 | 278 | 102 | 3 | 2 | middle row, right |
| 6 | 7 | 58 | 110 | −5 | 3 | front row, left |
| 7 | 8 | 186 | 110 | 0 | 3 | front row, centre |
| 8 | 9 | 278 | 110 | 5 | 3 | front row, right |
| 9 | 10 | 158 | 74 | 0 | 4 | conductor |

Records are always drawn in this order (0→9), so lower rows overlap higher ones and the conductor is on top.
dx, dy are used only in scene 4.

### 2.2 Animation scripts (DS:0x1e3e-0x2002)
Strings of letters; **frame = letter − 'a'**. A table of 10 near pointers at DS:0x1e55 gives one
32-letter string per record (record i → item i+1); the strings are NUL-terminated, 33 bytes apart.

| record/item | string (frames) |
|---|---|
| 0 / 1 | 0 1 2 3 4 5 6 7 8 9 10 0 0 10 10 10 10 0 0 0 0 11 11 11 11 11 11 11 12 12 11 11 |
| 1 / 2 | 0 0 0 0 0 0 0 0 1 2 3 3 4 4 3 3 2 2 5 5 6 6 7 7 6 6 7 7 8 8 8 7 |
| 2 / 3 | 0 0 0 0 0 0 0 0 0 0 1 1 2 2 1 1 0 0 2 2 2 2 2 2 2 2 2 2 0 2 2 2 |
| 3 / 4 | 0 0 0 0 1 1 1 2 2 2 3 3 4 4 3 3 2 2 5 5 6 6 0 0 6 6 5 5 2 0 0 0 |
| 4 / 5 | 0 0 1 1 1 0 0 2 2 2 3 3 3 3 3 3 2 2 3 3 4 4 4 4 4 4 3 3 2 2 2 0 |
| 5 / 6 | 0 0 0 0 0 0 1 1 1 0 0 0 1 1 0 0 0 0 1 1 1 1 0 0 1 1 1 1 0 2 2 2 |
| 6 / 7 | 0 0 1 1 2 3 1 1 1 4 0 0 0 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 1 0 0 0 |
| 7 / 8 | 0 1 1 1 0 0 2 2 2 3 4 4 0 0 4 4 3 3 0 0 0 0 0 0 0 0 0 0 3 0 2 2 |
| 8 / 9 | 0 0 0 0 0 0 0 0 0 1 1 1 2 2 1 1 1 1 2 2 3 3 3 3 3 3 2 2 0 0 0 0 |
| 9 / 10 (conductor) | 0 1 2 2 3 4 5 6 6 7 8 8 9 9 8 8 7 7 10 10 11 11 12 12 11 11 10 10 7 13 13 13 |

The "last frame" of a record means its string index 31 (items 1-10: 11, 7, 2, 0, 0, 2, 0, 2, 0, 13).

Conductor tap script (DS:0x1e3e, 22 letters "opoqopoqopoqopoqqrstrq"):
index 0-19 = frames 14 15 14 16 14 15 14 16 14 15 14 16 14 15 14 16 16 17 18 19; index 20 = 17 ('r',
scene 3's final frame); index 21 = 16 ('q', scene 4's conductor frame). Letter 'p' (frame 15) and 'q'
(frame 16) also trigger the tap sound in scene 3.

## 3. Common drawing helpers

- **draw(surf, x, y, item, frame)** = driver TM+0x0E (DS:0x2bc6): plain sprite draw with hot spot,
  clipped to the surface window.
- **Backdrop(y)** = 0x6379: draw item 0 shape 0 (starfield) at (0,0), then the globe (item 12 shape 0) at
  (160, y). Always called with y = 24 in scenes 1-4 → globe top edge at back row 24, horizontally
  centred (x 75..256), bottom 128 rows → visible rows 24..127 only.
- **rotozoom(surf, x, y, item, frame, angle, scale, flags)** = SUPERTM+0x02 (DS:0x2bdc): draws a
  shape rotated by `angle` degrees and scaled by `scale`/256 (256 = 1:1), hot spot on (x,y)
  (hot spot scaled with the shape — probable). Here angle = 0 and flags = 0 always, so it is a pure
  scale. **rotozoom_bbox(…, &rect)** = SUPERTM+0x06 (DS:0x2b6a): same arguments plus a pointer;
  draws nothing, returns the on-screen bounding box x0,y0,x1,y1 the shape would cover.
- **Logo(surf, y, scale)** = 0x68c4: rect = bbox of item 0 shape 2 at (162, y, scale); rotozoom shape 1
  at (rect.x0, y), shape 2 at (162, y), shape 3 at (rect.x1, y), all with that scale. I.e. the middle
  piece is centred on x = 162 and the two outer pieces butt against its left/right edges (their hot
  spots are at their inner edges), so the logo stays joined at any scale. At scale 256 and y = 61 the
  middle piece spans x 105..222, y 34..94; "OR" ends at x 105, "FX" starts at x 222.

## 4. Scene 1 — fade in (0x63b1(back, screen))

1. Backdrop(24).
2. For records 0..9: draw item (record+1), frame 0, at the record's (x, y).
3. Fade in (0x8ca6) while showing the result: the routine lists the colours used in the back buffer,
   reads the colour of the pixel at the screen window's top-left (screen (0,24), black) and sets every
   used palette entry to that colour, waits one retrace, presents back → screen, then each retrace
   moves all used entries toward their palette-2 targets with a Bresenham-style interpolation:
   D = the largest difference of any component (6-bit units), the fade takes D steps, every component
   reaching its target on the last step (smaller differences advance proportionally). One step per
   retrace → about D+1 retraces (≤ 64, ≈ 0.9 s at most).
No abort check inside (only main's check afterwards).

## 5. Scene 2 — orchestra tuning (0x640c(back, screen, speed))

State: `start` = 0, `steps` = 0, skip countdown = 1.

Loop:
1. **Forward pass:** for i = start .. 31: one step (below).
2. start = random(0,13) + 9 (9..22).
3. **Backward pass:** for i = 31 down to start (inclusive): one step.
4. If music on and device ≠ 3: go to 1 (forward again from the new `start`).
   Otherwise: go to 1 only if steps ≤ 30, else end the scene (in practice: without music the scene is
   exactly one forward pass 0..31 and one backward pass 31..start, 42..55 steps).

One step (index i): countdown; if drawn: Backdrop(24); for records 0..9 draw item (r+1),
frame = string_r[i], at (x_r, y_r); Present step. Then (every step): poll key → abort flag;
**if cue ≥ 1 → end scene**; if abort → end scene; steps += 1.

Note the turnaround index (31, and `start`) is shown twice in a row. With music the ping-pong repeats
until the "Tap on music stand" cue (5.52 s after song start; song start was before scene 1's fade).

## 6. Scene 3 — the conductor taps (0x65b5(back, screen, speed))

For k = 0..19, c = tap script letter k:
1. If c = 'p': on device 2 (AdLib) send note-on (channel 18, note 48, velocity 96); on device 1 (Roland)
   (channel 10, note 37, velocity 96) — through the music driver's note entry STRAX+0x0A (DS:0x2b9e).
   Force this step to be drawn (countdown := 1).
   If c = 'q': same channel/note per device with velocity 0 (note off); force draw.
   (Argument roles channel/note/velocity are inferred from how the sequencer and the sound-effect code
   call the same entry — probable. On the MT-32, channel 10 note 37 is the rim-shot/side-stick: the
   "tap on the music stand".) No sound on other devices.
2. If drawn: Backdrop(24); records 0..8 at their (x,y) with their **last** frame (index 31); the
   conductor (item 10) at (158, 74) with frame c − 'a'; Present step.
3. Poll key / abort → return.

After the loop: Backdrop(24), records 0..8 last frame, conductor frame 17 ('r'), drawn into back but
**presented only if music synced**: then wait (polling the key) until cue ≥ 2 ("Start Piano intro",
10.00 s), then Present step. Without sync this frame is never shown (scene 4 overdraws it).
So with music the screen holds the step-19 frame (conductor frame 19, baton raised) until cue 2,
then shows frame 17.

Tap rhythm at speed 1: frames 14,15,14,16 repeated 4 times = a tap ('p') every 4 retraces, release
('q') 2 retraces later; the whole 20-step script takes 20 retraces (≈ 0.29 s) on a fast machine. The
pace is tied to draw speed (see §9).

## 7. Scene 4 — orchestra zooms out of frame (0x695a(back, screen, speed))

n = 1; while n < 120: one step; then n := n + n/4 + 1 (integer division).
n takes the 17 values 1, 2, 3, 4, 6, 8, 11, 14, 18, 23, 29, 37, 47, 59, 74, 93, 117.

One step (if drawn): Backdrop(24); for records 0..8: rotozoom item (r+1), its last frame, at
(x_r + dx_r·n, y_r + dy_r·n), angle 0, scale 256 + 4·n·dy_r, flags 0; conductor: rotozoom item 10
frame 16 ('q') at (158, 74 + 4n), scale 256 + 16n; Present step. Then poll key / abort → return.

So the back row grows to 2.8× and moves 1 px/unit down, the front row to 6.5× moving 3 px down and
5 px outward, the conductor to 8.3× moving 4 px down; everything leaves through the bottom and sides of
the 128-row window, uncovering the globe. 17 drawn steps ≈ 0.24 s at speed 1.

## 8. Scene 5 — logo rises from behind the globe (0x6ac4(back, screen, speed)) → returns logo_y

Counters: t = 5000; ycount = 0; scount = 0; logo_y = 59; front = false; skip countdown = 1.
While t ≥ 1000:
1. changed = false.
2. If ycount = 0: globe_y = 120000 / t (integer); ycount = 200; changed = true.
3. If scount = 0: scale = 256000 / t (integer); logo_y += (t > 3000 ? −2 : +2); scount = 100;
   changed = true.
4. If changed and the skip countdown fires:
   - draw starfield (item 0 shape 0) at (0,0);
   - rect = bbox of logo middle piece at (162, logo_y, scale);
     if rect.y1 < globe_y: front = true (sticky: once set it stays set);
   - if not front: Logo(logo_y, scale), then globe (item 12) at (160, globe_y) — the globe covers the
     logo; if front: globe first, then Logo on top;
   - Present step.
5. Poll key / abort → return. t −= 1; ycount −= 1; scount −= 1.

Steps that change nothing are skipped with no delay, so only 41 iterations draw (speed 1):

| k | t | scale | logo_y | globe_y |
|---|---|-------|--------|---------|
| 0 | 5000 | 51 | 57 | 24 |
| 1 | 4900 | 52 | 55 | 24 |
| 2 | 4800 | 53 | 53 | 25 |
| … | 5000−100k | 256000/t | 59−2(k+1) for k ≤ 19 | 120000/t at even k, held at odd k |
| 19 | 3100 | 82 | 19 | 37 |
| 20 | 3000 | 85 | 21 | 40 |
| … | | | 19+2(k−19) for k ≥ 20 | |
| 40 | 1000 | 256 | 61 | 120 |

(The table generator: for k in 0..40, t = 5000 − 100k; scale = floor(256000/t); globe_y =
floor(120000/(5000 − 200·floor(k/2))); logo_y as given.)
After the loop: starfield, Logo(logo_y = 61, scale 256), Present step — **no globe in this final
frame** (its top 8 rows, still visible at y = 120, vanish). Returns logo_y (61).

Effect: a tiny logo (≈ 1/5 size) appears hidden behind the planet, rises to y = 19 while growing, the
planet sinks; when the planet's top passes below the logo's bottom the logo moves in front; then the
logo settles down to y = 61 at full size. ≈ 0.6 s at speed 1.

Between scenes 5 and 6 (main): if music synced, wait (polling the key) until cue ≥ 3 ("Start
fireworks", 17.37 s); the full-size logo stays on screen meanwhile.

## 9. Scene 6 — fireworks (0x6c80(back, screen, logo_y))

No speed skipping here: every iteration draws and presents (one retrace each).

Spark slots: 30 records of 12 bytes at DS:0x3578: frame (−1 = free), x, y, type (0..2), sound handle.
All are set free at the start (0x71c6); the music driver's sound-effect voices are reset (0x8e4:0x8f0).

**Spark update/draw (0x71e6(surf, n)), returns the number of free slots:**
for slot = n−1 down to 0: if free, count it; otherwise draw item 11 shape (type·8 + frame) at (x, y),
then frame += 1; if the frame just drawn was 7 the slot becomes free and its sound is stopped
(0x8e4:0x8a7); else if it was 0 (first frame) start its sound: sound-effect descriptor DS:0x200e via
0x8e4:0x4da with parameters (0, 127, 127 − 127·x/319, slot, 1). The x-dependent value falls from 127
at the left edge to 0 at the right — probably stereo pan (sense unverified). Sounds only exist with a
music device. A slot freed during the scan is not counted in that scan's free count.
So each spark shows frames 0..7 on 8 consecutive iterations (growing burst, shapes from 12x10 to
68x73) and is drawn under the logo; slot 0 is drawn last (on top of other sparks).

**Spawn:** first free slot among the allowed ones gets frame 0, x = random(0,319), y = random(0,127),
type = random(0,2).

**Phase A** (only slots 0..4 used), per iteration:
1. If phase A still spawning, decide spawn:
   - music synced: cue ≥ 4 → spawn always; cue < 4 → spawn when random(0,5) = 0 (1 in 6).
   - otherwise: spawn always.
   If spawn and a slot 0..4 is free: spawn into the lowest free one.
2. Draw starfield at (0,0); sparks (n = 5); Logo(logo_y, 256); Present step. iterations += 1.
3. If spawning has stopped and the spark call returned 5 free → go to phase B.
   Else stop spawning when: music synced → cue ≥ 5; otherwise → iterations > 10 (after 11 iterations).
4. Poll key / abort → return.

**Phase B** (grand finale):
1. Slots 0..28 all spawn at once (random x, y, type each).
2. Repeat until the previous spark call returned 30 free: j += 1; when j = 4 spawn slot 29;
   draw starfield, sparks (n = 30), Logo(logo_y, 256), Present step; poll key / abort → return.
   This is 12 iterations: 29 sparks on iterations 1-8, slot 29 on iterations 4-11, a clean logo on 12.
3. Stop the sound of any slot not free (none, normally).

With music: sparse sparks 17.37-21.16 s, continuous 5-at-a-time 21.16-23.24 s, then ≤ 9 iterations for
the last ones to finish and the 12-iteration burst (≈ 0.3 s). Without music: 11 iterations of
continuous spawning, then the wind-down and the burst (≈ 0.5 s total).

## 10. End sequence (main)

Always (also after an abort):
1. Clear the abort flag.
2. If a song was given: stop it immediately (0x8e4:0x1478 with fade length 0 = cut). Note this
   happens right after the burst (~23.6 s), before the song's own end.
3. Fade out (0x8964, args: 1 retrace per step, target = palette entry 0's colour (black), surface = the
   saved full-screen struct): the colours used anywhere on the 320x200 screen move to black with the
   same Bresenham interpolation as the fade-in (D steps, D = largest component value difference),
   one retrace wait per step.
4. Fill the whole screen (full-window struct) with colour 0 (driver TM+0x18, DS:0x3516).
5. Restore all 256 palette entries from the stored copy at DS:0x36e0 (0x739e: one entry at a time, a
   retrace wait before every 16th) — invisible, the screen is black.
6. Free TITLE items 0-12 (and the song); restore the screen struct (window back to 0..199).
The caller then sets palette 1 and continues with the next front-end screen.

## 11. Timing for a reimplementation

The original's only clock is the VGA retrace (70 Hz) in the present step, plus the music cues. A
reimplementation should:
- run scenes 2-6 at one step per 1/70 s (speed = 1), and treat steps that draw nothing as free;
- with music: use the song's cue markers (FE type 3) exactly as in §5-§9; the scene lengths then come
  from the song (scene 2 until 5.52 s, hold to 10.00 s, logo hold to 17.37 s, sparks to ~23.6 s);
- without music: scene lengths are the step counts given above (whole sequence ≈ 3 s plus fades).
- On a machine whose drawing takes longer than a retrace, each drawn step takes 2+ retraces in the
  original too (the wait is after drawing); scene 3's tapping and scene 4/5's motion would be slower.
  Nothing in the code compensates for this except the 1-or-2 speed factor.

## 12. Open questions
- Exact meanings of the sound-effect call parameters (DS:0x200e descriptor, the 127 and the
  x-derived value) and of the AdLib channel number 18 in scene 3.
- Device 3 identity (STRAX.DRV item 2).
- Whether rotozoom scales the hot spot (assumed; check with a capture of scene 4/5).
- Frame-accurate timing not yet compared with a DOS capture (the existing captures are ~1 s apart).

## 13. In u6e (render/gamedemo.c `se_origin_fx`, 2026-11-23)
- Built as above, from the pack's CSFE block (TITLE items 0-12, palette 2), song 19 through our sequencer; the cue
  is the sequencer's last FE type-3 number. "Music synced" = a song we can follow is playing (MIDI or the PSG).
- Pace: a step every 73 ms, not a retrace (slip 30 in rules/se_deviations.md); steps we fall behind on are not drawn.
- The scaled draw is nearest-pixel with the hot spot scaled.
- The conductor's tap is sent only on MIDI (MT-32 channel 10, note 37). The fireworks' sounds play through the effect
  engine (rules/se_sound_effects.md §6): descriptor DS:0x200e, a handle per spark, priority 1, panned by x; heard on the
  MT-32, and with DMA music as the AdLib's spark mixed in (mono); not on the YM2149 (no speaker record for program 127,
  as in DOS).
- A key ends the opening (the end sequence runs) and the film follows, as in DOS (the film first empties the keys).
