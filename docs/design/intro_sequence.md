# U6 Intro Cutscene — full sequence (reverse-engineered)

> Provenance
> - Derived from : the ORIGINAL DOS U6 intro OBSERVED running (DOSBox, U.EXE, 3000 cycles,
>                  captured frame-by-frame) + nuvie data/scripts/u6/intro.lua READ to understand
>                  the per-scene animation logic and asset (shape/index) mapping.
> - Clean-room   : this is our own description of the observed sequence + asset layout. NO nuvie
>                  code is transcribed; the copyrighted NARRATION TEXT is NOT reproduced here — it
>                  lives in the user's U.EXE and is extracted at build time (like the gypsy
>                  questions, character_creation.md). Page counts + scene structure only.
> - Validated by : side-by-side DOSBox capture (each scene + narration page identified).
> - Confidence   : confirmed (sequence/assets) / probable (exact frame timings).
> - Last updated : 2026-08-15 (doorway reveal + black silhouette, real-Falcon feedback)

## Advance model (IMPORTANT — differs from our current build)
The original is **input-advanced**, NOT time-advanced. Each narration page holds indefinitely
while its scene ANIMATES (loops); a **mouse click OR spacebar/key** advances to the next page.
Waiting ~30 s on a page lets its full animation cycle play. Our current build auto-advances after
6 s and only pans — we must switch to page-driven, input-advanced with the animation looping under
each page. (For headless capture, advance on the sentinel/injected input or a long fallback.)

## Scene order (each = static art + palette + a looping animation + N narration pages)

### 1. Living room  (INTRO_1.SHP, PALETTES.INT 1)   — WE HAVE THIS (partly)
- Art: room panorama (0 + 1@320) + field/window behind (15) + avatar/chair (13) + remote (14).
- Animation (loops under the pages): the **TV** (a 57×37 screen at (0xE5,0x32)) runs a little
  PROGRAM INTERPRETER (nuvie `display_tv`, one step per 50 ms — every 2nd 25 ms canvas update);
  ticking **LED clock** (starts ~**4:02**). Implemented 2026-08-16 (real-Falcon report: "the TV
  doesn't have all its animations, nor the Avatar pressing the remote"). Sprites = INTRO_1.SHP
  shapes **16 + image_num** (image 0..52 → shapes 16..68), each placed at
  (base_x + XOFF[img], base_y + YOFF[img]) and clipped to the screen rect; up to 5 per step. The
  Avatar's **FINGER on the remote (shape 14 @143,91)** is visible ONLY while the set shows static
  (= a channel change) — it is NOT part of the room art.
  - Program bytecode (7 programs, played in order 3,4,5,6,7,1,2,… — the interpreter starts on
    program 3 at position 1; item < 0x80 = draw image `item`; **0x80 n** = loop start with count n;
    **0x81** = loop end (jump back while count > 0), ends the step; **0x82** = static (noise image +
    finger visible), ends the step; **0x83** = "endless road": road_offset−− (wraps 14→1),
    YOFF[0x22] = 0x15−road_offset → draw 0x22, YOFF[0x22] = 0x24−road_offset → draw 0x22, draw 0x21;
    **0x84 n a₁..aₙ** = draw a random one of the n images; **0x86** = draw the current news
    image; **0x87** = pick the news image at random from {5,6,7,0x0D,0x0E,0x18,0x19,0x1F,0x20};
    **0x88** = pledge: counter++, every 4th draw the pledge image (37, alternating with 38 every 16);
    **0x89** = draw a random one of images 50..52; **0x8A** = end of step; **0x8B** = rotate
    palette entries 224..228 by one. When a program runs out the next one starts (7 wraps to 1).
    1 breathing `82 82 80 03 02 8A 02 8A 01 8A 01 8A 00 8A 00 8A 01 8A 01 81`
    2 intestines `82 82 80 28 03 8B 81`
    3 vacuum `82 82 80 04 04 81 80 04 08 81 80 04 09 81 80 04 0A 81 80 04 0B 81 80 04 0C 81`
    4 anchor `82 82 87 80 46 0F 86 84 09 10 10 10 11 11 11 12 12 12 81`
    5 endless road `82 82 80 32 83 81`
    6 rock band `82 82 80 05 27 8A 28 8A 29 81 80 06 2A 8A 2A 8A 2B 8A 2B 8A 2C 8A 2C 8A 2D 8A 2D 81 80 0A 2E 8A 2F 8A 30 8A 84 09 2E 2E 2E 2F 2F 2F 30 30 30 8A 2E 8A 2F 8A 30 81`
    7 pledge now `82 82 80 55 16 17 84 0C 13 13 13 13 14 14 14 14 15 15 15 15 88 81 80 0F 16 84 02 1A 1B 89 88 81 80 03 16 1A 89 88 81 80 03 16 1C 88 81 80 03 16 1D 88 81 80 03 16 1E 88 81 80 03 16 23 88 81 80 03 16 24 88 81 80 32 16 88 81`
  - Sprite offsets (image 0..54): XOFF = `0 0 0 0 0 1F 1F 1F 0 0 0 0 0 1F 1F 0 9 9 9 0C 0C 0C 0 4 1F 1F 4 0 4 4 4 1F 1F 0 0 6 6 8 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`;
    YOFF = `0 0 0 0 0 2 2 2 0 0 0 0 0 2 2 0 7 7 7 3 3 3 0 2 2 2 0 0 0 0 1 2 2 0 0 3 8 1D 1C 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`
    (YOFF[0x22] is rewritten by the road op).
  (Tables = the original's TV script data as transcribed in nuvie's intro.lua; the interpreter
  above is implemented from THIS description.)
- **2 narration pages** (page 1 "five seasons…", page 2 "traded the Avatar's life…/television
  supermen…"). We have only page 1.
- **PAN**: after the pages, the room pans right (avatar 2× parallax) to reveal the window — this is
  our existing pan; the user confirmed it's correct, it just triggers after a couple pages.

### 2. Window / storm  (INTRO_2.SHP, PALETTES.INT 2)   — MISSING (the "lightning" scene)
Close-up of a curtained, mullioned window onto a stormy twilight pine forest. Layers (nuvie
load_window / intro_2.shp indices, in DRAW ORDER — the ground is drawn BEFORE the trees, whose
lower rows cover the ground image's opaque black top band; drawn the other way round that band
shows as a black gap between forest and ground — real-Falcon report 2026-08-16, fixed in mkworld):
sky(1), scrolling clouds(0,2,3), ground(10@y0x4c), trees(8),
rain drops(4-7, spawn+fall), random **lightning flashes**(11-18, brief, random x/y), big **strike**(19-23
@158,114), window glass(26 normal / **27 = white flash** on a strike), window frame/mullions(28),
and the two **door leaves** left(24, 66×200)/right(25, 67×200) — NOT curtains: they are the leaves of
the house's DOORWAY, parked off-screen right (x=320 / x=573) until the pan. The whole window
**flashes white** (glass 26→27) when lightning hits. **4 narration pages** ("…storm is upon you",
"Tongues of lightning lash the sky…", "…bolt of searing blue fire strikes the earth!" [strike
appears], "Lightning among the stones!…"). **Implemented as nuvie's layered animation (2026-08-16,
real-Falcon report "no rain or clouds; the lightning flashes behind the trees and lights the
ground"):** one `display_window` step per 50 ms (nuvie runs it every 2nd 25 ms update): the big cloud
bank (img 0) as two copies scrolling +1 px/step wrapping at 320, 5 small clouds (2/3) at +2 px/step
re-spawned off the left edge, a lightning bolt (11–18, 1-in-7 per step, 1–4 steps, jittering 0..3 px)
drawn BEFORE ground+trees so it shows among the treetops, the strike (19–23, image re-rolled every
other step) whose sprite HOTSPOT is its ground point so (158,114) is where it hits, up to 100 rain
drops (4–7, +2 x +8 y per step, spawn delay ramping 20→1, recycled from the top), the glass white-
flash (26→27 for 1–5 steps, 1-in-6 per step or while the strike is up) and palette 0x58/0x5a/0x5c
(ground highlights) bright blue while a bolt is up (1 in 4) or the strike shows. Bolts/strikes are
the only front-end shapes with a non-zero hotspot (leftX,upperY) — mkworld ships it and the engine
draws at (x−hx, y−hy) (shapes.md). Layers ship raw; the scene composites per step; steps by wall
clock. On the 4th page the view **pans** 320 px left (2 px per
frame pair): frame + glass slide off and the two leaves trail them, so the pan lands on the DOORWAY
(leaves at the screen edges, 0..66 and 253..320, the storm forest between them; the strike goes out
halfway). Hold for input. Then the leaves slide APART (68 px each side, 1 px per frame pair) onto the
clear forest → §3.

### 3. Forest path  (twilight pines)   — ✅ DONE (doorway reveal, corrected 2026-08-15)
The Avatar runs through the storm to the stones. Confirmed on decode: this is NOT a separate asset —
it's the storm window's own bg (INTRO_2.SHP sky 1 + trees 8 + ground 10) revealed once the window
frame slides off. `forest_reveal()` (tail of storm_scene): the frameless forest (a twilight pine
forest with a **path winding to the horizon**) is drawn static while the window overlay (glass 26 +
frame 28) pans left off-screen (8 px steps) with the two DOOR LEAVES (24/25) trailing it — the pan
lands on the doorway (leaves at both screen edges, forest between), holds for input with the
"Lightning among the stones!" caption still up, then the leaves slide apart (4 px steps, 68 px each)
and the "You bolt from your house… into the forest, down the path… to the stones." narration
(U.EXE page 7) holds over the clear forest. Pace is TIME-BASED (2026-08-16): the pan runs at
nuvie's 40 px/s and the leaves at 20 px/s by wall clock however many frames the machine draws. Real-Falcon feedback 2026-08-15: our first cut slid the
frame straight off onto open forest — the doorway beat was missing (the user remembered it). mkworld
ships the forest base (opaque) + the window overlay (255 = transparent) + the two leaves. Lightning
keeps flashing behind the frame/leaves during the holds. Validated by Falcon capture (INTRO.REQ 's'
= storm scene alone).

### 4. The stones  (INTRO_3.SHP, PALETTES.INT 3)   — ✅ DONE (obsidian + hand)
Ring of standing stones (angled/overhead). Beats: glimpse the tiny **obsidian stone** in the circle
→ a **hand reaches in and picks it up** ("Wondering, you pick it up…."). ~2 pages. stones_pickup():
page 0 glimpse obsidian (img 3 @150,100), page 1 the hand (img 1) rises 0xc7→0x54 (dirty-rect) then
the stone is gone (bg img 5). Validated.

### 5. Moongate  (INTRO_3.SHP, PALETTES.INT 3, crimson palette-cycle 144-159)   — ✅ DONE
The **red** moongate rises from the stones, then 4 input-advanced narration pages (crimson-pulsing
throughout): (0) "…a softly glowing door ascends in silence!", (1) the **clutch-the-stone memory** —
"…as you clutch the stone… last cast down by Lord British to banish Blackthorn!" — during which
(nuvie @911–955, implemented 2026-08-16) the hand holding the obsidian (INTRO_3 shape 6, 114×134) rises
from y=0xC7 to 0x44 at x=0x9B while the WHOLE scene (bg, ground cover, gate) scrolls up 65 px (1 px per
25 ms step, the hand 2 px), the caption holds on the scrolled view, then it all comes back down;
(2) apprehension — "The gate to Britannia was ever blue…", (3) "The portal quivers and sinks away…"
and (4) "Desperation makes the choice an easy one." — on these last two pages the gate SHAKES
(nuvie stones_shake_moongate: x 0x7C+rand(0,1), y 5+rand(0,3) every 25 ms step; the crimson ramp
rotates every 5th step) — the "shimmer/shake once it's up" seen in ScummVM (implemented 2026-08-16 as a
gate-rect dirty present) — then the **Avatar walks into the gate** (a silhouette that steps up into
the doorway and dissolves as it enters). 5 moongate pages (global 10..14 in the g_intro_text list;
g_intro_npages[moongate]=5). We have: the rise ✅, the crimson glow cycle ✅ (intro_crimson_cycle
144..159), all 5 narration pages ✅ (validated on TT), and the
**Avatar-into-gate climax ✅** (avatar_climax(): the figure = INTRO_3.SHP frames 7..26 walks up at
(-2, 0x12-3*i), dissolves via a Bayer dither, then the gate sinks 0x5→0x64). The "figure" is the
AVATAR, not a gargoyle. **It is a BLACK silhouette** (real-Falcon feedback 2026-08-15): the frames
are drawn entirely in palette index 25, which PALETTES.INT 3 holds as a mid grey; nuvie sets that
entry to black for the walk + the dissolve and restores the grey (0x74) afterwards. We do the same
(palette copy with 25 blacked on 256-colour targets; LUT 25 → the scene's darkest slot on the ST 16). Validated by TT capture (walk → dither dissolve into the crimson gate → gate
sinks). **Real-Falcon correction (2026-08-15):** (a) the gate sprite is **clipped to rows 0..0x65**
(nuvie moon_gate clip_h = 0x66) — it is only ever visible ABOVE the ground line, which is what makes
it ascend from the earth; unclipped, its foot showed below the ground band during the rise. (b) The
gate-rise scene's background is INTRO_3 **shape 5** (the ring re-lit by the gate's red glow, obsidian
gone — nuvie sets bg.image = img[5] before the rise), NOT shape 0: shape 0's teal lightning-lit ground
under the warm-brown gate_cover strip (a cut-out of shape 5) read as a mismatched "box" on hardware.
The gate itself IS an opaque red rectangle (74×98, fully block-covered) — that is the original art.
NB the source frames are full 320x200 canvases that are ~all transparent (uncovered) except
the small figure, so mkworld CROPS each to its bbox + a per-frame (ox,oy) offset — else 20×64000B
blew the ~4 MB TT-RAM fast pool (U6.PAK failed to load). CONVERSE/PORTRAIT also moved to ST-RAM to
keep the fast pool for the pack + backbuffer.

→ fade to black → character creation (gypsy). (character_creation.md)

## Narration text — ✅ DONE (extracted from U.EXE)
All 15 narration pages are EXTRACTED from the user's **U.EXE** at build time (mkworld
`extract_intro()`, clean-room; the copyrighted text is never committed) and re-wrapped to 320px by
the engine. Located STRUCTURALLY, not by any hardcoded copyrighted anchor: the narration is the
contiguous run of prose fragments right after the intro asset-filename block (`intro1.cga`…
`intro_3.shp`, at ~0x17772), grouped into pages by the rule "join NUL-terminated word-wrap fragments
until one whose LAST byte is sentence punctuation (`.`/`!`/`?`)"; internal `\n` → space; stops at the
gypsy region (first `%c` fragment) or 15 pages. Page order = living(0..2, incl "Outside, a chill wind
rises…") · storm(3..6) · forest(7) · stones(8..9) · moongate(10..14). Packed as `[u8 np]+np×[u16
len][bytes]`; engine loads into `g_intro_text[15]`/`g_intro_tlen[15]` (or falls back to a committed
paraphrase `g_intro_fb[]` if U.EXE is absent). Captions render with a PROPORTIONAL U6.CH renderer
(`pglyph_w`/`ptext`/`pwrap` — the original trims each glyph to its used columns) so the long pages
pack into few lines and fit the 48px BLOCKS.SHP[2] parchment, matching the DOS layout. Validated on
TT (living page B / "Outside…" / storm / forest / stones / moongate all show the real text).

## Build plan (phased)
1. **Living-room match** — page-driven input-advance (space/click), add page 2, clock→4:02, keep pan.
   ✅ DONE + validated on TT. run_intro is now PAGE-DRIVEN: `g_intro_pages[scene][page][line]` +
   `g_intro_npages[]` + `g_intro_page`; `intro_advance()` advances on any key OR a mouse-button
   press (edge), Esc skips; auto-advance after `PAGE_MAXTICKS`=~10s (attract/headless). Living
   room = 2 pages (TV surfs + clock ticks across both), then the pan. Static scenes (stones/
   moongate) are 1 page each, same input model. Clock base 4:02. Perf unchanged (hold 13ms/76fps,
   pan 25ms/40fps). Page text is still OUR paraphrase (page 2 added) — real text = Phase 4.
   Note: clock ticks 1 min/game-sec (faster than the original's ~1.5 min/min) — cosmetic, tune later.
2. **Window/storm scene** — ✅ DONE + validated on TT. `storm_scene()` in gamedemo.c, inserted in
   run_intro after the living room (`if(s==0) storm_scene()`). mkworld bakes the static window
   scene (INTRO_2.SHP sky 1 + trees 8 + ground 10@76 + glass 26@57 + frame 28) under PALETTES.INT 2
   + ships lightning-flash frames 11-18 and big-strike frames 19-23 (@140,6). Animation: present the
   static scene, then randomly draw a bright bolt (11-18) at a random position with a whole-scene
   PALETTE FLASH (brighten +14, ~classic lightning illumination), held a few frames then recomposed
   away; the big strike (19) shows from page 3. 4 input-advanced pages (our paraphrase text). Runs
   in flip mode (full presents are tear-free; lightning is infrequent so cost is fine). Validated:
   base scene matches the original, all 4 pages advance, a dramatic blue bolt renders through the
   window, transitions cleanly living-room→storm→stones→moongate.
   UPDATE: lightning is now correctly layered BEHIND the window (see Phase 3 "Lightning layering
   fix"), and the scene now ends by SLIDING the window frame off to the forest (Phase 3 "Forest
   reveal"), not a fade. TODO polish (Phase 2b): scrolling clouds + rain (deferred — full-screen
   animated, secondary); animate the strike frames; the curtain (24/25) side-jamb slide (we slide the
   whole frame overlay instead — reads the same).
3. **Forest** scene + **stones obsidian/hand** beats + **moongate figure** dissolve + crimson cycle.
   PARTIAL: ✅ **crimson glow-cycle** DONE + validated — `intro_crimson_cycle()` rotates palette
   144..159 every ~5 frames while the moongate (type-2) scene holds, so the red gate pulses (two
   captures show the gradient shifting; no corruption). Gated to type 2 (stones unaffected).
   ✅ **obsidian pickup** DONE + validated — `stones_pickup()` replaces the stones scene's generic
   hold: page 0 glimpse the obsidian (INTRO_3.SHP img 3 @150,100) on the stones bg; page 1
   "Wondering, you pick it up…." the hand (img 1) rises 0xc7→0x54 (dirty-rect the hand column) to
   grab it, then swaps to bg5 (stone gone / red gate-glow beginning). Assets shipped in the pack
   (hand/obsidian/bg5). Validated: the hand rises with the stone on the teal bg, transitions to the
   red bg5 → moongate. (Minor: a small highlight line in the hand shape; the caption lingers on bg5
   a moment before the moongate scene — cosmetic.)
   ✅ **Avatar-into-gate climax** DONE + validated — `avatar_climax()` (gated to the type-2 moongate
   scene, runs after its page holds): the "figure" is the AVATAR (INTRO_3.SHP frames 7..26) walking
   up at (-2, 0x12-3*i) into the risen crimson gate while the gate jitters + the crimson ramp cycles,
   then it **dissolves** into the gate (blit_dither Bayer 4x4, keep 14→0), then the gate **sinks**
   (0x5→0x64). TT capture confirms the whole beat. IMPLEMENTATION NOTE: the 20 source frames are full
   320×200 canvases (index 0 transparent) with only a small figure, so mkworld CROPS each to its
   non-zero bbox and stores a per-frame (ox,oy) offset — the uncropped 20×64000B pushed U6.PAK to
   ~3 MB and it FAILED to load (exceeded the ~4 MB TT-RAM fast pool). Cropped avatar = ~131 KB;
   U6.PAK back to ~1.98 MB. Also moved CONVERSE.A/.B + PORTRAIT.A/.B (~0.57 MB) from the fast pool to
   ST-RAM (chip) — they're read-mostly + untouched by the intro — so the fast pool stays free for the
   pack + backbuffer (else rdr_init's fast backbuffer alloc failed → "no renderer").
   ✅ **Forest reveal** DONE + validated — `forest_reveal()` at the tail of storm_scene: the window
   frame overlay slides left off-screen (16px steps) over the STATIC frameless forest (revealed
   seamlessly since the bg stays put), then the "You bolt from thy house… into the forest, down the
   path, to the stones." narration holds over the forest path. mkworld ships the forest base
   (sky 1 + trees 8 + ground 10 — no glass/frame, opaque) + the window overlay (glass 26 + frame 28,
   index-0 transparent). NOT a separate asset (confirmed: the "forest path" IS the storm bg minus
   the frame). TT capture confirms the reveal + the path bg.
   ✅ **Clutch-the-stone memory pages** DONE + validated — the moongate scene grew from 1 to 4
   input-advanced pages (g_intro_pages expanded to `INTRO_NPG=4`, `g_intro_npages[moongate]=4`),
   crimson-pulsing throughout: (0) rise, (1) "…as you clutch the stone… cast down by Lord British to
   banish Blackthorn!", (2) "The gate to Britannia was ever blue…", (3) "The portal quivers and sinks
   away… Desperation makes the choice an easy one." TT capture confirms all 4 render then flow into
   avatar_climax. → **Phase 3 COMPLETE.**
   ✅ **Lightning layering fix** — the storm bolts + big strike were painting OVER the baked window
   (frame in the bg). Now that the frameless forest + window overlay are separate assets, storm_compose
   is LAYERED (`storm_compose_ex`): forest bg → strike/flash bolt → window overlay ON TOP, so the
   frame's mullions correctly occlude the lightning (matching nuvie, where the strike sprite is drawn
   before the frame). TT capture confirms the mullions pass in front of the bolts.
4. **Narration extraction** from U.EXE for all pages (build-time, clean-room). ✅ DONE + validated —
   `extract_intro()` pulls all 15 real pages STRUCTURALLY (filename-block landmark + punctuation-
   grouping rule; see "Narration text" above); engine renders them with a proportional U6.CH font
   (`pwrap`) and a paraphrase fallback. The 3 hardcoded caption arrays (g_intro_pages/g_storm_pages/
   g_forest_page) were replaced by one 15-page model (g_intro_text) + scene base/count tables
   (g_intro_base/g_intro_npages). → **The intro is COMPLETE — full DOS text, all beats.**

## Perf note
Scenes 1/4/5 are near-static + localized animation → the dirty-rect hold model (done). Scene 2
(storm) is the one genuinely full-screen-animated; budget for it with the fast c2p + animating only
the through-the-window area (frame/curtains static), and do the lightning flash as a window-image
swap (26→27), not a per-pixel repaint.
