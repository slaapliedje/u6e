# Performance — measurement + roadmap

Date: 2026-06-23 · Tool: `make bench` (render/gamedemo.c -DBENCH) · Target: TT
68030 @32MHz, TT-low 320×480 256-colour, real TOS 3.06 under Hatari.

Policy: "correct first, optimise measured hot paths." We do NOT guess —
`make bench` times compose vs present so optimisation targets real numbers.
The bench writes per-frame ms for the two phases of a redraw.

## Measured — per-move redraw
| Phase            | Full-screen (M5/M6a) | +longword blit/branchless/-O2 | **Windowed (11×11 map)** |
|------------------|----------------------|-------------------------------|--------------------------|
| compose          | 111 ms               | 41 ms                         | **20 ms**                |
| present (c2p)    | 617 ms               | 583 ms                        | **117 ms**               |
| total/move       | ~728 ms (~1.4 fps)   | ~624 ms                       | **~140 ms (~7 fps)**     |

The **windowed map (176×176) + static panels** was the big win: per move we
recompose + `rdr_present_rect` only the map window (and the small status strip),
not the whole 320×480 screen — ~5× less c2p. This is both the authentic U6 layout
AND the perf fix. (Full-screen present is still ~583 ms; we just don't do it
per move anymore.)

## Cursor moves must NOT trigger a full present (animation/cycling stall)
The mouse pointer is composited at present time. Originally a bare mouse move took the
**full** present path (`present_frame`), so dragging the mouse ran a whole-screen c2p
every frame — which dwarfs a vblank. Worse, animation + colour cycling advanced once per
loop ITERATION, so the slow loop dragged them down: cycling visibly slowed/paused while
the mouse moved. Fixes (render/gamedemo main loop):
1. **Cursor-only present** (`present_cursor`): a bare mouse move repaints just the old +
   new cursor boxes (16-px-aligned), not the screen.
2. **Time-based cadence**: anim/cycle are paced by the 200Hz clock (`plat_ticks_200hz`),
   not a per-iteration counter — independent of loop/frame rate.
3. **One Vsync per iteration**: `present_frame` already Vsyncs, so the loop no longer
   Vsyncs again after it (the old double-Vsync halved the rate to 30Hz on every present).
Measured (Falcon, 1s of vbls): idle 51 iters/8 cyc; OLD full-present-on-move 6 iters/5 cyc;
NEW cursor-only-on-move 51 iters/8 cyc — i.e. mouse movement no longer costs cycling.

## The wall: chunky→planar present (c2p)
c2p is ~85% of a frame and does NOT improve with branchless code or -O2/-O3.
Cause: it converts the WHOLE 153600-pixel screen every redraw — ~8 shift/or ops
per pixel, writing to (contended, slower) ST-RAM. Plain C can't beat this.

### Options, best leverage first
1. **Smaller map WINDOW + static panels (M6 UI work).** ✅ DONE — present ~117 ms
   (~5×). The authentic U6 layout AND the perf fix.
2. **Incremental scroll** of the map window: on a 1-tile move, memmove the window
   + c2p only the new edge row/col (~1/11 the window) → present ~10–15 ms.
   Next big win; trickier with the avatar/object overlay at the centre.
3. **Hand-written asm c2p** (sanctioned hot path): a good 68030 c2p is ~10–20
   cycles/px → the 176×176 window in ~25–35 ms.
4. **Blitter-assisted** copy/expand where the TT/STE Blitter helps.

Next: (2) incremental window scroll and/or (3) asm c2p to reach a smooth frame
rate. Re-run `make bench` after each.

## Done this pass
- compose: longword (32-bit) tile rows; objects stay byte-wise (transparency).
- present: branchless 8-accumulator c2p (cleaner; byte-identical output).
- render-heavy .PRGs (rdemo/game/bench) build at -O2; selftest stays -Os.
- `make bench` added as the permanent perf-tracking tool.

## Done — smoothness pass (M6 animation era)
The UI-doubling (320×200 → 320×480) had regressed the windowed-present win:
`present_frame()` did a FULL `rdr_present_doubled` every present, and the new
per-tick animation made that the steady-state cost.
- `make bench` now: compose 50.5 ms · present FULL(c2p) **266 ms** · present
  MAP-WINDOW **119 ms** (per frame, TT-low 320×480) — full doubled present is the wall.
- **`rdr_present_rect_doubled(x,y,w,h,lh)`** + `present_map()`: present just the
  176×176 map window (panel/cmd-bar static) for clean walks + the anim tick. 266→119 ms.
- **Animation gate** (`g_view_has_anim`, set in compose_map): the per-tick recompose+
  present runs ONLY when an ANIMDATA tile (water/fountain) is in view — most scenes
  have none → ZERO per-tick cost (was a full 266 ms present every ~10 vblanks). Fire/
  glow uses colour cycling (CLUT, no present), animated for free. Net: idle near-free,
  walk ~1.9×, animated scenes ~2.2×. Validated (build/anim_engine.png): water animates
  via the windowed present, panel/letterbox intact; throne room clean.

## Diagnostic — the present is COMPUTE-bound, not write-bound (corrects earlier guess)
The bench now also times the map window NON-doubled (`rdr_present_rect`) vs doubled:
```
present MAP doubled 119 ms   vs   MAP 1x (half the writes) 117 ms
```
Doubling the planar writes costs only **~2 ms** → the ST-RAM writes are NOT the wall;
the **c2p bit-twiddling (compute) is**. So the levers that help are *fewer pixels*
(incremental scroll) or *faster per-pixel* (better algorithm / asm) — NOT avoiding
the doubling.

### Tried + REJECTED: uint64 bit-transpose c2p (in C)
An 8×8 bit-matrix transpose (Hacker's Delight) replaces ~128 shift-ors/group with
~24 and is byte-identical (proven: tools/c2ptest, exhaustive + 2M random groups).
But on the **32-bit 68020 the uint64 ops are emulated** (several instructions each),
and that overhead *beats the op-count win*: present FULL 266→**283 ms**, MAP 119→
**130 ms** — slower. Reverted. Lesson: stay native-width (≤32-bit) on the 68020.

## ✅ DONE — hand-asm c2p (render/c2p_asm.s): **2.3× present**
The C 8-accumulator loop needs 8 plane accumulators + the pixel + loop/pointer vars,
so gcc **spills** — that spill was the real cost. `c2p_span_asm` (the project's first
and only hand-asm, 68000-class so it serves both target builds) pins the work to
registers: a `lsr.b`/`roxl.w` **carry chain** — `lsr.b #1` drops the next plane bit
into X, `roxl.w` rotates it into that plane's accumulator (pixel 0 ends at bit 15).
Planes 0–6 live in d1–d7, the pixel in d0, plane 7 in dst[7] (memory) — 8 planes + a
pixel reg won't fit in 8 data registers. It converts a whole **span** of groups per
call, so the `movem` save/restore is amortised (once per row, not per group). It loops
(doesn't unroll) to stay in the 68030 I-cache.

| Phase (TT-low 320×480) | C 8-accum | **asm carry-chain** | speedup |
|------------------------|-----------|---------------------|---------|
| present FULL (doubled) | 266 ms    | **117 ms**          | 2.27×   |
| present MAP window (doubled) | 120 ms | **52 ms**         | 2.30×   |
| present MAP window (1×, probe) | 117 ms | **44 ms**        | 2.63×   |

Doubled present = c2p the even row via `c2p_span_asm`, then `memcpy` it into the odd
row (the duplication, ~free vs c2p). The C `c2p_group()` stays as the byte-identical
**spec + fallback** (`__attribute__((unused))`). VALIDATED: m5-svm throne-room capture
vs the C-c2p reference — every differing pixel is in the colour-cycle fire ramp (phase
drift, because the faster build advances more cycle steps in the fixed capture window);
zero structural/UI pixels changed → identical pixel indices.

**Steady-state now:** idle near-free (anim gate), clean walk ~**52 ms** present (~14 fps
during input), animated scenes similar. Combined with the windowed present + anim gate,
this is the smoothness target met for a CPU c2p.

## 2026-06-29 — compose is the new bottleneck; profiled + two wins
Since the asm-c2p pass, lighting + visibility + multi-tile + seated NPCs were added to
`compose_map`, so **compose now dwarfs present**: `make bench` (TT 68030, LB castle interior)
read compose **242 ms** vs present-map-window 50 ms. Added BENCH-only per-phase timers
(`g_phase[]`, printed by `make bench` with `--conout 2`); the breakdown:

| phase (interior, ms/compose) | before | after |
|------------------------------|--------|-------|
| ph0 base tiles               | 20     | 19    |
| ph1 objects+actors           | **135**| 110   |
| ph2 light + visibility       | 26     | 26    |
| ph3 lit composite (160×160)  | 72     | 74    |
| **total compose**            | ~242   | ~220  |

**Win 1 — object spatial bucket index (`g_obkt_*`).** ph1 scanned ALL ~8329 world objects
twice per compose (reading 75 KB of structs from ST-RAM) just to find the handful in the 11×11
view. A 64×64 grid of 16×16-tile buckets (CSR, rebuilt in `build_blocked`) lets `compose_map`
visit only the ≤2×2 buckets the view touches. Removes the scan (−24 ms here); scales with
in-view count, not world size, so the win is proportionally larger in sparse/wilderness scenes
where the scan dominated the few draws. The remaining ph1 (110 ms) is drawing the genuinely
in-view objects (dense castle) — that needs incremental scroll (below).

**Win 2 — uniform-light composite fast path.** The lit composite bilinearly interpolates
per-cell light to every pixel + `darklut`. Outdoors in daylight the light field is UNIFORM
(full ambient, no globes), so the bilinear is pure waste. When `min light == max light`
(all cells equal + fully visible) we take one `darklut` level with no per-pixel
bilinear/visibility — **byte-identical** to the slow path when uniform. Measured on an open
daytime scene: composite **74 → 14 ms**, and total compose **242 → 114 ms** (~2.1×). Interiors
(non-uniform) keep the slow path — real work.

Net: **outdoor walking** (the common case) compose ~114 ms; **interiors** ~220 ms (the spatial
index; composite is genuine non-uniform work). Both validated by `make bench`; the throne
render is unchanged (spatial index drops no objects).

## 2026-06-30 — incremental buffer-scroll DONE (+ two scans the work surfaced)
`make bench` now measures BOTH a forced full rebuild AND a 1-tile-walk **scroll** (it alternates
`px` by ±1 each iter), and prints the scroll phase breakdown via `--conout 2`.

**Incremental static-layer scroll.** `compose_map` was split into a persistent STATIC layer
(`g_tmp_map` = base tiles + lower-pass objects) and a per-frame DYNAMIC layer (actors + avatar,
foreground objects, roofs, lighting, composite). On a 1-tile walk the static buffer is
`memmove`-scrolled 16 px and only the lead **2-cell band** is recomposed (2-wide so multi-tile
bleed from the new edge lands); a cell-granular clip in `draw_cell` makes the band redraw
byte-identical (every object footprint cell in the band is drawn in full index order, all others
rejected). Actors/foreground/lighting stay full per frame, so there is **no actor-erase or
shifted-light artifact**. Validated headlessly by a **diff-test** (`COMBAT.BIN` byte 11):
scrolled `g_tmp_map` == full rebuild, **0 differing bytes in all 4 directions** in the dense
throne room. (Animated tiles: a walk leaves the scrolled interior 1 frame stale; the per-tick
refresh — armed via `mark_view_anim` — does a full rebuild, so animation keeps running.)

**Two object scans the profiling exposed** (both reused the `g_obkt_*` buckets):
- `obj_idx_at` (used by `npc_sit_tile` for seated-NPC detection) scanned ALL ~8329 objects
  **per in-view actor every frame** — O(actors×objects). Bucketed → scans one tile's bucket.
  This was the real ph1 cost: interior ph1 **91 → 7 ms**, full compose **226 → 140 ms**.
- `compute_light` / `compute_visibility` each scanned all objects for in-view emitters /
  sight-blockers → bucketed: ph2 interior **25 → 5 ms**, outdoor **31 → 9 ms**.

| compose (ms) | full (this turn start) | full now | **1-tile SCROLL now** |
|--------------|------------------------|----------|------------------------|
| interior (LB castle) | 242 | 119 | **100** (static 13 · actors 8 · light/vis 5 · composite 73) |
| outdoor (open)       | 242 | 44  | **34**  (static 9 · actors 1 · light/vis 9 · composite 13) |

Per move (scroll compose + present 49 ms): **interior ~149 ms (~6.7 fps), outdoor ~83 ms
(~12 fps)** — vs ~292 ms (~3.4 fps) at the start. Render unchanged (throne capture identical).

## Open (remaining levers)
- **Incremental present + composite — CONSIDERED, DEFERRED BY DECISION (2026-07-01).** Now that
  compose is cheap, the **present (c2p, ~49 ms) is the outdoor bottleneck** and the per-frame
  **composite (~73 ms) is the interior one**. The present-side mirror of the compose scroll would
  be: scroll the planar screen + c2p only the new edge. We evaluated it and chose NOT to build it,
  for concrete reasons:
  - It is correct-by-scroll ONLY for **uniform light** (outdoor daytime) — the shared region must
    be a pure pixel shift. Interiors/night have the avatar's CENTERED torch, so the lit backbuffer
    is not a pure shift; that needs dirty-region recompositing (re-composite the avatar-torch box +
    new edge), which is much hairier and artifact-prone.
  - It mainly speeds the **already-fast outdoor case** (~12→~23 fps); interiors (the slow ~7 fps
    case) benefit little and are the risky part.
  - **Validation gap**: the headless SHOT dump writes the *backbuffer* (chunky), not the planar
    screen, so unlike the compose work (byte-identical diff-test, COMBAT.BIN byte 11) the planar
    present cannot be byte-verified headlessly — only via an auto-walk AVI capture (visual).
  Current per-move: outdoor ~83 ms (~12 fps), interior ~149 ms (~7 fps) — playable for a
  turn-based RPG. If revisited: do the uniform/outdoor case first (add a backend `rdr_scroll` that
  memmoves the planar map window by one tile, then `rdr_present_rect` the new edge), gated on the
  uniform-light flag + 1-tile delta, validated by an auto-walk AVI diff.

## 2026-07-11 — INTRO cutscene is slow: compose AND present, ~equally (measured)
`make bench-intro` runs the living-room scene (0) through the REAL `run_intro` and reports
per-frame cost, split HOLD vs PAN × {compose (`draw_intro_frame`), c2p+double, output
(letterbox clear + flip/Vsync)}. Timing is BENCH-only, coarse 200Hz boundaries (the c2p/output
split comes from the backend `rdr_bench_present`, so the hot c2p loop isn't perturbed):

| phase (TT-low 320×480, n frames) | compose | c2p+double | output | total → fps |
|----------------------------------|---------|-----------|--------|-------------|
| HOLD (static, n=26)              | **124 ms** | 101 ms | 13 ms | **238 ms → 4.2 fps** |
| PAN  (scrolling, n=108)          | **114 ms** | 100 ms | 15 ms | **230 ms → 4.3 fps** |

**Key result (corrects the earlier guess that present alone was the wall):** the intro's
**compose (`draw_intro_frame`) is ~120 ms — as large as the c2p present (~100 ms)**, not the
~20–40 ms first estimated. `run_intro` calls the FULL-screen doubled present (`rdr_present_doubled`)
AND re-blits the entire 640-wide panorama + 320×200 avatar layer EVERY frame — even during the
static hold, where only a 17×19 TV mouth (~7 fps) and 4 clock digits change. Note HOLD compose ≈
PAN compose: the recomposite is the same full-screen work regardless of how little actually
changed — the direct evidence for dirty-rect.

**Implication for fix ordering: this is a TWO-part fix, not one.** Dirty-rect present alone
(~101→~5 ms) leaves compose (~120 ms) as the new wall → ~7.5 fps; fixing compose alone leaves
c2p → ~8.8 fps. Both together (compose the static background once + dirty-rect present just the
mouth/clock during holds; incremental scroll for the pan) is what reaches a smooth frame. Output
(letterbox clear + Vsync) is ~13–15 ms — the letterbox is re-cleared every frame though constant,
a cheap secondary win. Instrumentation: `render/rdr_tt.c` (`rdr_bench_present`, BENCH) +
`render/gamedemo.c` (`g_iph[phase][bucket]`, `intro_compose`/`intro_present`, BENCH) + `make
bench-intro`.

### ✅ DONE — static-hold dirty-rect: HOLD 238 ms → 5.3 ms/redraw (~45×)
The living-room hold now composes the background ONCE (`draw_intro_frame` gated by `g_hold_part`:
`IF_BG` omits the TV mouth + LED clock), snapshots the small animation box, presents the scene
once, then per frame redraws ONLY the mouth+clock box (`IF_ANIM` over a `restore_rect`'d clean
background) and presents just that rect (`rdr_present_rect_doubled`, x/w aligned to 16). It redraws
only when the mouth (~7 fps) or clock (1/s) actually ticks; idle frames cost a bare Vsync. The hold
drops to single-buffer (`rdr_flip_end`) so the rect present hits the visible screen, then back to
double-buffer for the pan.

| phase (after fix) | compose | c2p+double | output | total → fps |
|-------------------|---------|-----------|--------|-------------|
| **HOLD** (n=47 redraws) | **0.3 ms** | **5.0 ms** | 0.0 ms | **5.3 ms/redraw** (idle = 1 Vsync) |
| PAN (unchanged) | 107 ms | 101 ms | 14 ms | 221 ms → 4.5 fps |

Also: static scenes (stones/moongate, nothing animates) now present once then just Vsync-wait,
instead of a full recompose+present every frame. Validated on TT: bench (above) + AVI captures of
the living-room hold (mouth/clock composite cleanly, no dirty-rect garbage; static room intact) and
mid-pan (room scrolls correctly through the flip-mode transition). Falcon links (rect present →
`rdr_present_rect`, flip = no-op → rect hits the visible screen); on-rig Falcon check pending.

### ✅ DONE — incremental scroll: PAN 221 ms → 24 ms/step (~9×)
`compose_wide` bakes the 640-wide living-room frame once (1x, no parallax, frozen mouth/clock),
the pan presents the pan_x=0 window once, then each 16-px step calls `rdr_scroll_left16` (render.h):
memmove the planar screen LEFT one c2p group in place + c2p ONLY the newly-revealed rightmost
group from the wide chunky buffer + duplicate to the odd row (TT doubling). ~1/20 the c2p of a
full present; the cost is now the ST-RAM block-shift, not c2p. Single-buffer (the pan sits between
the hold's `rdr_flip_end` and a `rdr_flip_begin` for the next scene); `Vsync()` before each step
starts the shift at vblank to limit tearing. Backend prims: `rdr_scroll_left16` in rdr_tt.c (doubled
+letterboxed) and rdr_falcon.c (1:1).

| phase (after both fixes) | compose | c2p+double | output | per-step |
|--------------------------|---------|-----------|--------|----------|
| HOLD (dirty-rect, mouth+clock only) | 0.3 ms | 5.0 ms | 0.0 ms | **5.3 ms/redraw** (idle = 1 Vsync) |
| HOLD (+ TV channel cycler + static) | 4.9 ms | 9.1 ms | 0.0 ms | **14.0 ms/redraw → ~72 fps** |
| **PAN (incremental)** | 0.0 ms | **24.4 ms** | 0.0 ms | **24.4 ms/step → ~41 fps** (was 221) |

The TV channel cycler (news anchor / talk-show / drama programs + generated static, `tv_state` +
`draw_tv_static` in gamedemo.c; alt-channel frames shipped in the pack) expanded the hold's dirty
box to the whole TV screen (57×37) + clock and repaints it whenever the channel/frame/static ticks
(static every frame). Still 14 ms/redraw (~17× under the old 238 ms full-frame) — headroom for the
remaining planned anims (lightning). This is the intended payoff of the dirty-rect hold: new
animated boxes cost ~their own area, not a full frame.

20×16-px steps → the full 320-px pan in ~0.5–0.7 s (was ~24 s). Validated on TT: bench + a mid-pan
AVI capture (room scrolls cleanly to the window/2nd-poster, no edge garbage/smearing) + Scene-1
capture (clean single-buffer→flip transition to the next scene). game/gamef/bench build clean.

Trade-offs / notes:
- **16-px step granularity** is inherent on TT-low (a c2p group = 16 px; sub-16 fine scroll needs
  HW fine-scroll, verified unavailable, or a full c2p). Faster pacing looks smoother than slow with
  16-px jumps, so the pan runs near frame-rate.
- **No caption during the pan** (compose_wide bakes none) and **no parallax** (uniform 1x) — the old
  slow pan had a fixed caption + 2x avatar parallax. Minor fidelity trade for the 9× win; both
  restorable (redraw the fixed caption strip after each step; parallax needs a 2nd scroll layer).
- **Single-buffer → possible minor tearing** on the fast scroll (shift-in-place can't double-buffer,
  since the incremental shift needs the immediately-previous frame). Tear-free would need a 2-frame
  double-buffer scroll (shift the hidden buffer 32 px + c2p 2 edges, then flip) — a follow-up if the
  tear is objectionable.

**Still open (cheap):** clear the letterbox once, not every full present. **Falcon:** real VIDEL HW
scroll ($FF8210 is a WORD line-width reg, unlike TT-low) would make the pan HW-driven — the next
"take full advantage of the Falcon" step.

## 2026-07-19 — ST hand-asm 4-plane c2p (`render/c2p_st.s`): present 678 → 444 ms (1.53×)
The ST backend's `c2p4_lut` (serial C bit-scatter, 16 shift/OR per group × 4 planes) is now the C
reference; the present path calls the byte-identical hand-asm `c2p4_lut_asm` — the ST sibling of the
TT's `c2p_span_asm`. Method: per pixel, LUT the index to a nibble, then a `lsr.b #1` / `roxl.w #1`
carry chain drops each of the 4 plane bits into its 16-bit accumulator (pixel 0 → bit 15). Only the
low 4 bits are consumed, so the C's `& 0x0F` needs no mask. **Byte-identical** to the C reference:
`rdr_c2p_selftest()` (BENCH, `make bench-st`) runs both on a 256-index / 16-position pattern through
the live LUT and reports `c2p asm-vs-C diff wd=0`. Wired into all three ST targets (game.prg unified,
gamest.prg, benchst.prg); TT keeps its own 8-plane `c2p_asm.s`.

Measured `make bench-st` (night throne room, full 176×176 map-rect present): **678 → 444 ms/frame**.
Only 1.53×, not the 3–4× a table c2p suggests, because the **plain ST is RAM-bandwidth bound**: the
screen is chip RAM, whose writes contend with the shifter DMA (~half the bus in ST-low), so the
carry-chain is already near the memory floor. A 128 KB byte-pair table would fold the LUT but not
beat the memory wall, and it breaks the per-row band-raster LUT (M7) — rejected. Walking frame
(PREDIM flat): SCROLL compose 551 ms + present 444 ms ≈ **995 ms → ~1.0 fps** (was 1226 ms / 0.82).

## 2026-07-19 — incremental planar present (the 2026-07-01 deferred lever): re-opened by PREDIM
The **incremental present** deferred above ("Open remaining levers") was blocked on reason (1): the
lit backbuffer isn't a pure shift (the avatar's centered torch), so a present-side scroll needs
dirty-region recompositing that was "hairier and artifact-prone." **PREDIM incremental scroll now
solves exactly that** — `g_dimmed_map` shifts on a walk and only the edge + light-changed cells are
re-dimmed (byte-identical, mode==11 self-test). So the on-screen planar could shift in lockstep
(Blitter block-move on STE/Mega/TT, CPU memmove on plain ST — the `rdr_scroll_left16` mechanism) and
re-c2p ONLY those same dirty cells: present would drop from a full 176×176 c2p (444 ms) toward
~dirty-cells-worth. **Reason (2) is closable after all**: while the SHOT dumps the chunky backbuffer,
a BENCH selftest can diff the planar `g_screen` (incremental-present vs full-present) *in memory* —
same trick as the c2p selftest — so it IS byte-verifiable headlessly. **The real caps are geometry +
Amdahl**: the 8 px `MAP_INSET` offsets composed cells from the 16 px-aligned c2p groups (so the clean
version is block-move + re-c2p the outer border ring + the light-changed bounding box, not per-cell),
and compose (below) is now the dominant term — so a *perfect* present caps the frame at ~1.8 fps and
a realistic one buys ~1.0 → ~1.35 fps. STATUS: designed + costed, **deferred in favour of compose**
(higher ceiling). Requires plumbing the compose dirty-set + delta to a backend `rdr_present_map_scroll`.

## 2026-07-19 — compose profiled + memcpy'd ph3 (551 → 490 ms); now distributed / near-floor
With the present asm'd, compose (~551 ms) became the ST walking bottleneck. `make bench-st` now
splits the SCROLL compose into ph0 sub-phases + ph1/ph2/ph3. PREDIM-flat, night throne room:

| sub-phase | ms | what |
|-----------|---:|------|
| compute_predim_light (pre-ph0) | ~75 | compute_light globes + compute_visibility flood (unattributed) |
| ph0 scroll_static + mark_view_anim | ~109 | shift g_tmp_map (31k memmove) + edge recompose; mark_view_anim only ~8 |
| ph0 shift g_dimmed_map + g_dm_light | ~33 | two memmoves in lockstep with the undimmed cache |
| ph0 re-dim dirty cells | ~40 | dim_cell_into over only **~20 dirty cells/frame** (incremental tracking working) |
| ph0 copy g_dimmed_map → tmp | ~55 | 31k ST-RAM→ST-RAM memcpy (memory-bound) |
| ph1 actors + fg objects | ~99 | masked sprite blits + dim-at-draw (inherent) |
| ph3 composite (tmp → backbuffer) | **94 → 33** | **was a byte loop; now `memcpy` per row (long-word moves)** |

**Landed:** ph3 was a per-pixel byte copy — a plain move, so `memcpy` (long-word) took it **94 → 33 ms**
(~60 ms, byte-identical; mode==11 still 0, frame visually identical). Compose **551 → 490 ms**; ST
PREDIM-flat walking frame **995 → ~934 ms → ~1.07 fps**.

**Conclusion — compose is near its floor.** After incremental PREDIM (ph0) + this, the remaining cost
is **distributed, memory-bound, and mostly inherent**: two 31k buffer shifts, a 31k dimmed→tmp copy,
the masked actor blits, and the light/vis compute — no single dominant lever. Eliminating the `tmp`
buffer (compose straight to the backbuffer, drop the copy + ph3, ~48 ms) was **evaluated and rejected**:
the VT=11 compose overruns the 176-px map region into the panel (x>176), and `tmp` exists precisely to
isolate that — direct-to-backbuffer would need extra per-cell clipping and risks panel corruption for
a small win. The instrumentation (`g_ph0_*`, BENCH) stays as the profiling map for any future pass.

## 2026-07-19 — cross-machine frame rate MEASURED: plain ST is the floor, Falcon/TT crush 2 fps
Same harness, same scene (PREDIM night throne room, walking), three backends. `make bench-st`
(plain ST, rdr_st 4-plane c2p), `make bench` (TT, rdr_tt 8-plane c2p, `--ttram 14`), the new
`make bench-falcon` (rdr_falcon 16-bit truecolor = **no c2p**):

| machine | CPU | compose | present | frame | **fps** |
|---------|-----|--------:|--------:|------:|--------:|
| plain ST | 68000 @ 8 MHz  | 490 ms | 444 ms | 934 ms | **1.07** |
| Falcon   | 68030 @ 16 MHz | 106 ms | 91 ms (truecolor expand, no c2p) | 198 ms | **5.06** |
| TT030    | 68030 @ 32 MHz | 39 ms  | 49 ms  | 88 ms  | **11.4** |

The plain **ST 8 MHz is the extreme worst case** (slowest CPU in the line, no blitter, code+screen in
ST-RAM contended ~50% with the shifter). The 68030 machines are ~5–11× faster (clock + 32-bit bus +
cache + no shifter steal). So the ~1 fps we optimized on the ST is NOT the product's frame rate —
**Falcon and TT already run 5–11 fps**; 2 fps is only ever in question on the 8 MHz ST. Getting the
plain ST *itself* to 2 fps needs native planar (eliminate c2p; the Blitter pays off on STE/Mega) — a
milestone, not a lever. Note: the TT bench's fast pool outgrew a 4 MB config once the M9 core sections
(CACT/CFAC/CTLK) grew U6.PAK to 2.36 MB under EmuTOS — run it with `--ttram 14` (fast-pool-tight-on-TT).

## 2026-07-19 — native-planar ST milestone: present 444 → 81 ms, frame 933 → 619 ms (1.51×)
The "native planar" milestone (docs/design/native-planar-st.md) attacks the 444 ms c2p directly:
composite the map in ST-low planar and present with NO per-frame c2p. Phases 0–2 done and byte-verified
vs c2p (`rdr_planar_selftest` + `rdr_pverify`, both `diff=0`). Measured `make bench-st` (PREDIM night
throne room walking, `LIGHTSTY.REQ=3`), plain ST 8 MHz:

| path | compose | present | frame | **fps** |
|------|--------:|--------:|------:|--------:|
| baseline PREDIM (chunky + c2p) | 489 ms | **444 ms** | 933 ms | 1.07 |
| native planar-only            | 538 ms | **81 ms**  | **619 ms** | **1.62** |

Present **5.5× cheaper**; frame **1.51×**, byte-identical. Mechanism: an incrementally-maintained planar
STATIC layer (`g_pdimmed` = planar mirror of `g_dimmed_map`, per-dirty-cell c2p + planar shift), planar
dynamic blits (`draw_cell`/roofs → `g_ptmp` via a (tile,level) planar cache), and a single shifted
planar block-copy present. Residual +49 ms compose = the planar-static per-cell c2p on the lit walk
(~20 dirty cells/frame); dimming DIRECTLY in planar (a later phase) removes even that.

**Wired LIVE (Phase 2c + 4a):** `present_map` uses the planar present in the ST game (ST + PREDIM +
single-LUT map — band rasters only swap hardware registers, orthogonal to the framebuffer). Phase 4a
was REQUIRED for this to apply at all: the game's mouse cursor is always on, so `present_map` always
took its cursor branch (chunky c2p) — the planar present was dormant. A planar mouse cursor (drawn into
the 4-plane screen with save-under) + planar-aware `present_map`/`present_cursor`/`present_frame` fixed
it. Pixel A/B on a real 8 MHz ST (planar vs chunky, same scene, cursor on) is identical bar the debug label.

**Phase 4b — dropped the chunky composite on walk frames → 1.62 fps live.** `g_chunky_dyn=0` in
production skips the chunky DYNAMIC composite (actors/fg/roofs → `tmp`, the ph0 `tmp` copy, the ph3
`g_back` memcpy); `g_back`'s inner map goes stale but every gameplay present of it is planar
(`present_frame` now overlays `rdr_ppresent` after its panel c2p; modal views write their own `g_back`).
Live walk drops from 739 to **538 + 81 = 619 ms / 1.62 fps** — the bench planar-only ceiling, now realized
in the game. Pixel A/B (incl. the hybrid `present_frame`) identical bar the debug glyph. The residual
compose is the ~88 ms planar-static per-cell c2p on the lit walk; **planar dimming** (compose the static
layer in planar so it needs no c2p) is the last step toward ~2 fps. NOTE: native planar is PREDIM-only and
the ST defaults to SMOOTH, so it's opt-in (`LIGHTSTY.REQ=3`) until PREDIM becomes the ST default. Also:
the Blitter path (STE/Mega).

## 2026-07-20 — planar dimming (Phase 4c): compose 538 → 339 ms, frame → 420 ms / 2.38 fps (2 fps cleared)
The last residual in the planar compose was the per-cell **c2p** (`rdr_pstatic_cell`, ~88 ms/walk) that
converted the chunky-dimmed static to planar, plus the chunky edge recompose (~118 ms). Phase 4c composes
the STATIC layer DIRECTLY from pre-dimmed planar tiles into `g_pdimmed` — `dim_tile_base` (255→0 then
darklut) for the opaque base + `dim_tile` for masked objects, blitted via `rdr_pstatic_blit`; no chunky
`g_tmp_map`/`g_dimmed_map`, no `dim_cell_into`, no c2p. Rests on darklut COMMUTING with tile overlay
(`blit(dim base)+blit(dim obj) == c2p(dim(base⊕obj))`, proven). On a scroll only the DIRTY cells' base is
re-blitted + all objects redrawn ONCE (per-cell `draw_view_objects` was 5× costlier — O(objects)).

| path | compose | present | frame | **fps** |
|------|--------:|--------:|------:|--------:|
| Phase 4b (planar + c2p static) | 538 ms | 81 ms | 619 ms | 1.62 |
| **Phase 4c (planar-dimmed static)** | **339 ms** | 81 ms | **420 ms** | **2.38** |

It beat the ~2 fps projection because it drops the WHOLE chunky static maintenance, not just the c2p.
Byte-verified: `PLANAR static-direct diff=0` (full-rebuild INV), `INV2 diff=0` (walk), `dim-commute diff=0`;
a pixel A/B on a real 8 MHz ST differs by only the debug glyph. **Native-planar milestone complete: plain
ST 1.07 → 2.38 fps (2.2×), shipping as the ST default.** Remaining headroom: the Blitter path (STE/Mega).

## 2026-07-20 — Phase 3 Blitter: the PRESENT, not the blit (STE/Mega 2.37 → 2.83 fps)
Two Blitter targets, measured on an emulated STE (8 MHz + BLiTTER; `make bench-ste`):

- **Masked blit in the compose — a WASH.** The cookie-cut masked blit is ~1.2–2× faster as a primitive
  (`BLITTER blit-vs-cpu diff=0`), but the compose A/B is flat (full 410→395 ms, scroll 342↔344 ms): the
  blit is only a few ms of the ~340 ms compose — the **per-pixel darklut dim dominates**, and the Blitter
  can't do LUTs. Left in but **gated OFF** (`g_blit_compose`), so the walking path never pays for it.
- **The present — the win.** `rdr_ppresent`'s +8-px shifted planar copy is exactly a Blitter skew-8 (right
  shift 1 byte), one blit per plane, edge endmasks preserving the border; row-banded (4 rows) so each HOG
  stays < 1 scanline and the Timer-B raster keeps its splits.

| config | compose | present | frame | **fps** |
|--------|--------:|--------:|------:|--------:|
| plain 8 MHz ST (no Blitter) | 341 ms | 82 ms | 423 ms | 2.37 |
| **STE (Blitter present)**   | 342 ms | **11 ms** | **354 ms** | **2.83** |

`PRESENT selftest diff=0` (byte-identical incl. borders), live STE shot clean (map/panel/border, raster
intact). **present 81 → 11 ms (7×), STE 2.37 → 2.83 fps (+19%);** the plain ST (no Blitter) is unchanged.
The Blitter can't touch the compose dim (LUT) — the ST floor stays 2.38 fps; the STE/Mega ceiling is now ~2.83.

## 2026-07-20 — incremental static-object draw: compose 341 → 272 ms (both machines)
Profiling the plain-ST scroll compose (`--conout 2` to capture the sub-phases) put the fat in one place:
`ph0 re-dim dirty = 121 ms`, of which the ~20 dirty base fills are ~1 ms — the rest was `draw_view_objects`
redrawing **ALL ~100+ lower objects into `g_pdimmed` every scroll frame**, even though `rdr_pstatic_shift`
already panned the whole static layer (terrain + tile-aligned static objects) by one tile. Only the cells
whose base was re-blitted (the newly-revealed lead band + any light-changed cells) actually need their
objects re-composited.

Fix: a per-cell dirty mask (`g_pd_dirty`) + a gate (`g_pd_objgate`) in `draw_cell` — during the incremental
scroll's object pass, `draw_cell` composites ONLY dirty cells; clean cells keep their already-shifted object
pixels. Iterating ALL objects (with a cheap per-object footprint skip; objects are ≤2×2) keeps a multi-tile
object straddling clean/dirty correct, and confining WRITES to dirty cells preserves z-order there. No
hardware needed — helps every machine. (Hardware fine-scroll was considered and **rejected**: HSCROLL
`$FF8264` is real + ST-Low-compatible, but it only saves the shift + present, not the object *compositing*,
and is STE-only.)

| phase | before | after |
|-------|-------:|------:|
| ph0 re-dim dirty | 121 ms | 51 ms |
| **scroll compose** | 341 ms | **272 ms** |

Byte-verified `INV2 diff=0` by day (uniform light → lead band only) AND at night (`STARTMIN.REQ=180` →
scattered light-dirty cells), plus a clean live plain-ST shot. **Plain ST 2.38 → 2.83 fps (+19%); STE (with
the Blitter present) 2.83 → 3.56 fps (+26%)** — so the STE is 2.37 → 3.56 fps (+50%) across the two sessions.
Remaining: `ph1 actors+fg` (~90 ms) is the dynamic layer, redrawn every frame (actors move; fg draws over
them for z-order) — harder to incrementalize; and the plain-ST 81 ms CPU present.

## 2026-07-22 — ph1 actor sit-cache: compose 272 → 241 ms (both machines)
Sub-profiled `ph1` (new `g_ph1_actors`/`g_ph1_fg` timers, `--conout 2`): **55 ms fields+actors / 36 ms
foreground**. A bisect of the 55 ms (only 7 actors in view!) put 32 ms in `npc_tile` → `npc_sit_tile`, not
the blit (16 ms) — it ran two `obj_idx_at()` chair/throne searches PER DRAWN ACTOR EVERY render frame, to
decide if the NPC is seated (Lord British on his throne). That answer only changes when the actor moves.

Fix: memoize `npc_sit_tile` per actor keyed on its `(x,y)` (`npc_sit_tile_c`) — a stationary NPC pays the
search once, not every frame; recomputes when it moves. INV2 can't see this (both compose paths call the
same `npc_tile`), so a dedicated bench gate `SITCACHE mismatch=0` proves the cache is byte-identical to the
uncached search, + a live shot (scene/NPCs correct).

| ph1 sub-phase | before | after |
|---|--:|--:|
| fields+actors | 55 ms | **24 ms** |
| scroll compose | 272 ms | **241 ms** |

**Plain ST 2.83 → 3.11 fps; STE 3.56 → 4.02 fps** — so across three sessions the STE is **2.37 → 4.02 fps
(+70%)**, the plain ST 2.38 → 3.11 (+31%). Remaining ph1: `foreground objects` (36 ms) and the actor blits
(16 ms) are the full-`g_ptmp`-rebuild tax — reducing them needs an incremental dynamic layer (shift g_ptmp +
sprite save-under), a bigger restructure. Plus the plain-ST 81 ms CPU present (Blitter-solved on STE).

## 2026-07-24 — ph1 foreground: sparse fg-only bucket index, scroll compose 241 → 221 ms
Sub-profiled the 36 ms `foreground` pass (`draw_view_objects` pass 1, redrawn OVER the actors every frame)
with new counters (`g_fg_iter`/`g_fg_draw`/`g_fg_blit`, planar-cache `g_pcache_miss`, and a `noblit` A/B).
The finding: pass 1 **visited 77 in-view objects to blit only 4 foreground cells** — it re-scanned the whole
`g_obkt` bucket set every frame to find the handful of top-pass tiles. The 4 blits themselves are real work
(~3 ms each on the cacheless 68000; caches hit — planar miss ≈ 3/frame), but the **77-object scan+dispatch
was ~22 ms of pure waste**. (Both dim and planar caches hit fine, so it was never a c2p/regeneration cost.)

Fix: a second CSR bucket index `g_fgbkt_*` holding ONLY objects that can draw a Foreground tile, built once
per objects-change in `build_blocked` via `obj_is_fg()` (complete over multi-tile decrements + ANIMDATA/
objanim frames). Pass 1 scans `g_fgbkt`; the lower pass still scans full `g_obkt` (it draws their non-fg
cells). Z-order is unchanged (foreground still drawn last), so **INV2 stays 0** — but INV2 can't see a
*dropped* fg tile (both paths would use the same scan), so a dedicated `fg_index_selftest` composes pass 1
via full-`g_obkt` vs sparse-`g_fgbkt` across the scroll walk and proves **FG-INDEX diff = 0** (byte-identical).

| ph1 sub-phase | before | after |
|---|--:|--:|
| foreground obj | 36 ms | **15 ms** |
| fg objects scanned/frame | 77 | **5** |
| scroll compose | 241 ms | **221 ms (ST) / 217 ms (STE)** |

**Plain ST ~3.11 → ~3.31 fps; STE ~4.02 → ~4.39 fps.** The residual 15 ms is the 4 genuine foreground
masked-blits; cutting those needs the same incremental-dynamic-layer (bake fg into the shifting static layer
+ actor-overlap fix-up) restructure noted above. ph0 static (104 ms) and the plain-ST 81 ms present remain
the two largest levers.

## 2026-07-24 — IDLE recompose was a FULL rebuild: 372 → 211 ms (the standing-still frame)
Adding an IDLE bench (view FIXED, animation advancing — the frame the game loop composes every ~33 ms anim
tick while the avatar stands still) exposed the real prize: **idle frames cost a full `build_static` rebuild
(372 ms — worse than a scroll)**, because `predim_compose_static`'s `can_scroll` requires `(dx||dy)`, so a
zero-delta recompose falls straight through to the full-rebuild `else`. Standing still watching water/torches/
NPCs animate is the *most common* real-gameplay frame, and it was the worst-performing (~2.2 fps on ST).

Fix (planar path only — the shipping ST renderer): a `can_idle` branch (`dx==dy==0`) that recomposes ONLY
the cells whose STATIC content animates this tick (`mark_anim_cells`: animated base tile OR an animated
object's footprint) plus any light-changed cell — no shift, no band. The dynamic layer (ph1) still redraws.

**Correctness (two gates):**
- The idle path is fired ONLY on the animation tick, via a `g_idle_allow` flag the game loop sets around that
  one compose. U6 is turn-based, so on an anim tick nothing but animation has changed since the last compose.
  Every action/scroll recompose leaves `g_idle_allow=0` → keeps the full rebuild, so a stationary state change
  (open a door, pick up an item — which do NOT invalidate the compose cache) still refreshes correctly.
- INV2 now runs an IDLE verify pass (fixed view + advancing anim): the planar idle-increment vs the chunky
  FULL rebuild — **diff 0**, proving the incremental idle static is byte-identical to a rebuild. (The scroll
  walk never exercised `dx==dy==0`, so this is its own gate.)

| idle sub-phase | before | after |
|---|--:|--:|
| idle compose (full rebuild → incremental) | 372 ms | **211 ms** |
| ↳ ph0 static | ~330 ms | 92 ms (11 dirty cells) |
| ↳ ph1 dynamic | ~40 ms | 40 ms |

The chunky path (TT/Falcon + the INV2 reference) keeps the full idle rebuild — no regression there.

## 2026-07-24 — idle light cache: idle compose 211 → 134 ms
Timing `compute_predim_light` (per-cell LOS flood + light globes, run before ph0 and untimed by the phase
split) showed it is **~77 ms every frame** — the single biggest per-frame cost, on scroll AND idle. On an idle
anim tick it is INVARIANT: LOS depends on walls/position (avatar fixed), globes on emitter positions (nothing
moves on an anim tick — U6 is turn-based, and the game clock only advances on turns). So cache it: gated on the
same `g_idle_allow` anim-tick flag, the idle path reuses the last `plight`/`pvis`; every move/action recomputes.

Object emitters resolve through `obj_tile` (anim-aware), so a flickering-emission emitter *could* drift — a bench
**IDLE-LIGHT drift** check recomputes fresh vs cached over the idle walk (animation advancing): **drift 0**, so
light is provably invariant across animation. (INV2 can't see this — both compose paths share `plight` — so the
drift check is its own gate.)

| idle sub-phase | before | after |
|---|--:|--:|
| compute_predim_light | 77 ms | **0 (cached)** |
| idle compose | 211 ms | **134 ms** |

**Idle compose across the session: 372 → 211 → 134 ms (2.8×).** With the present, standing still goes ~2.2 →
~4.6 fps (ST) / ~6.9 fps (STE). Remaining idle floor (134 ms): ph0 static 92 ms (the `g_ptmp=g_pdimmed` copy +
the mark/scan passes over 11 dirty cells) + ph1 dynamic 40 ms — both need the persistent-`g_ptmp` dynamic layer.

## 2026-07-24 — scroll light: sparse light/sight object index, light+vis 77 → 57 ms
`compute_predim_light` is ~77 ms/frame; cached on idle but recomputed every scroll frame. Split-timed it:
**compute_light 33 ms / compute_visibility 44 ms**. Visibility (LOS flood from the view-centre) dominates and
is *not* shiftable — walls move relative to the fixed centre, so the flood must re-run. But both do a redundant
full ~77-object bucket scan (~10 ms each) to find the handful of emitters / sight-blockers — the same waste the
fg-index fixed. Added a sparse `g_lsbkt` index (objects that emit light OR block sight, built once in
`build_blocked` via `obj_is_lightsight`, complete over ANIMDATA/objanim frames); `compute_light` and
`compute_visibility` scan it instead of `g_obkt`. Proven complete: a `light_index_selftest` composes the light
field via sparse vs full scan across the scroll walk — **LS-INDEX diff 0** (INV2 can't see it: both compose
paths share `plight`).

| | before | after |
|---|--:|--:|
| compute_light | 33 ms | **22 ms** |
| compute_visibility | 44 ms | **34 ms** |
| scroll compose | 213 ms | **193 ms** |

**Plain ST scroll ~3.4 → ~3.65 fps; STE ~4.5 → ~4.9 fps.** The remaining 57 ms is per-cell terrain lookups
(`world_tile`/`tile_is_indoor`/`blocks_sight` × 121, world-anchored → shiftable) + the LOS flood + wall-expand
(not shiftable). A shiftable terrain-input cache (ambient/emitter/sight per cell, shift + lead-band like the
static layer) is the next lever for the world-anchored part; the LOS flood is the floor.

## 2026-07-24 — plain-ST present: hand-asm (render/ppresent_st.s), 82 → 70 ms (bus-bound)
The +8px-shifted planar present (`ppresent_cpu`, ~82 ms) is the biggest single plain-ST cost (STE Blitter-solved
at 11 ms). Hand-asm'd it (`ppresent_st_asm`, sibling of `c2p_st.s`): per-plane byte-merge with the previous
group carried in registers, dbra'd row/group loops. **82 → 70 ms**, byte-identical (`rdr_ppresent_asm_selftest`
CPU-C vs asm **diff 0**, no Blitter needed so it validates on the plain ST too).

Only ~15% though — a C group-at-a-time rewrite first gave *zero* speedup, and the asm the rest. The present is
**contended-video-RAM bound, not CPU-compute bound**: it moves ~28 KB (14 KB read `g_ptmp` + 14 KB write screen),
both in ST-RAM the shifter also reads, so every CPU word access waits on the shifter. The Blitter does the *same*
28 KB in 11 ms because its DMA has the bus priority the CPU lacks — that 6× gap is the Blitter, not the code, so
the plain-ST present has little CPU headroom left (70 ms is near the floor). `ppresent_cpu` is kept as the
`#ifdef BENCH` C reference; the asm ships on every native-planar ST/STE-CPU present.

Plain ST scroll ~3.65 → ~3.8 fps; idle ~4.65 → ~4.9 fps (present is every-frame, so it helps both).

## 2026-07-24 — idle mark cache: idle compose 135 → 107 ms
Profiling the 134 ms idle floor split ph0 into **mark 29 ms** (`mark_view_anim` + `mark_anim_cells`, two whole-
view `world_tile` passes) / **dirty-recompose 53 ms** (the ~11 animated cells: their tile changes each tick so
dim+c2p miss the caches — inherent) / **copy 10 ms** (`g_ptmp=g_pdimmed`) / **ph1 40 ms** (dynamic redraw). On an
idle tick the view AND objects are fixed (turn-based), so WHICH cells animate is invariant — `mark_anim_cells`
recomputed the identical set every tick. Cache it (`g_pd_anim` + `g_pd_anim_valid`, cleared on any non-idle
compose so a scroll/action/door-open recomputes), and drop the now-redundant `mark_view_anim` whole-view pass
(`g_pd_has_anim` keeps the per-tick refresh armed). **mark 29 → 1 ms, idle compose 135 → 107 ms.** INV2 idle-
verify = 0 proves the cached set is complete (a missed animated cell would go stale vs the chunky full rebuild).
Zero added BSS, scroll path untouched.

**Idle compose across the session: 372 → 211 → 134 → 107 ms.** Standing still: ST ~4.9 → ~5.6 fps, STE ~6.9 →
~8.5 fps. Remaining idle floor (107 ms): dirty-recompose 53 ms (animation regen — inherent) + ph1 40 ms + copy
9 ms (the last two = the idle-only persistent-`g_ptmp` restructure, deferred: needs a bespoke patch-vs-full gate).

### Idle dirty-recompose (53 ms) is a FLOOR, not a missed win (measured)
Split it: **fill 24 ms** (the 11 animated cells' base, ~2.2 ms/cell) + **pass-0 28 ms** (~9 ms scanning 77 objects
+ ~19 ms redrawing the animated objects in those cells). Crucially the dim/planar caches **HIT** — only 2 dim /
3 planar misses per frame — because the animation frames *cycle*, so it is NOT regeneration/thrash. ~43 ms is
genuine per-tick work: animation requires redrawing those cells' base+objects. The only reducible slice is the
~9 ms pass-0 object scan, and it can't be narrowed to a sparse animated-object index (a static item on an
animated tile must still redraw when the tile's frame changes) — it'd need a cached per-idle-view object list
for ~9 ms (idle 107 → ~98). Low ROI; treated as the floor.

## 2026-07-24 — idle persistent-g_ptmp: idle compose 107 → 79 ms (−28 ms), INV2 = 0
The idle floor's last two reducible slices were the **copy 9 ms** (`rdr_pframe_begin`: whole `g_pdimmed`→`g_ptmp`)
+ **ph1 40 ms** (redraw ALL 14 actors + 10 fg objects). On an idle tick U6 is turn-based, so actors don't move —
only the handful whose sprite ANIMDATA-cycles change, yet we re-copied the whole buffer and re-blitted every
actor/fg every tick. **Persistent g_ptmp:** keep `g_ptmp` across idle frames and PATCH only the dynamic-dirty
cells. D = `g_pd_dirty` (static cells `predim_compose_static` just refreshed — already includes every animated
*object* footprint, lower+fg, via `mark_anim_cells` over `g_obkt`) ∪ `g_dyn_anim` (the animated ACTOR/AVATAR/FIELD
footprints — the dynamic elements NOT in `g_obkt`, marked by the new `mark_dyn_anim`, cached per idle-entry like
`g_pd_anim`). Per idle frame: `rdr_ptmp_restore(D)` copies just D's cells `g_pdimmed`→`g_ptmp` (replacing the full
copy), then ph1 runs gated by `g_pd_dyngate=D` (a planar-ONLY per-cell gate in `draw_cell`/roofs — the chunky copy
stays full so the verify still builds a whole `tmp`). Clean cells keep last frame's composite bytes (static AND
dynamic both unchanged there → correct by induction).

**copy 9 → 4 ms, ph1 40 → 16 ms; idle compose 107 → 79 ms (ST) / 107 → 77 ms (STE).** `mark_dyn_anim` is the one
expensive piece (`npc_tile` runs a seated-NPC search per actor) — cached per idle-entry, it amortises to ~0.
**INV2 (g_ptmp vs a full-rebuilt c2p(tmp)) = 0 over the idle-verify walk** is the correctness gate here (unlike the
static-shortcut cases, the persistent g_ptmp shares NO state with the per-frame chunky reference, so a single stale
cell — an un-marked animated actor, an un-erased footprint — shows as a nonzero diff). Live ST shot clean.

Scroll path is byte-for-byte untouched (`g_pd_idle_patch=0` on any dx/dy → full `rdr_pframe_begin` + ungated ph1);
scroll compose 193.8 ms unchanged, INV2 = 0 over the scroll frames too. (Also fixed a pre-existing bench mislabel:
`BENCH.BIN[5..8]` is written after the idle loop, so the Makefile's "scroll phases" line had been printing idle
`g_phase[]` — now snapshotted before the idle loop overwrites it. True scroll ph1 = 40 ms, idle ph1 = 16 ms.)

**Idle compose across the session: 372 → 211 → 134 → 107 → 79 ms.** Standing still (compose+present): plain ST
107+70=177 → 79+70=149 ms (~5.6 → ~6.7 fps); STE 107+11=118 → 77+11=88 ms (~8.5 → ~11.4 fps). Remaining idle floor
(79 ms) = dirty-recompose 53 ms (animation regen — inherent) + restore 4 + ph1 16 + mark/overhead ~6.

## 2026-07-24 — TT/Falcon: default to PREDIM_DITHER — Falcon scroll compose 190 → 103 ms
TT/Falcon defaulted to LSTY_SMOOTH (per-pixel bilinear darkness LUT), the one lighting style with NO cache:
every frame re-lit all 160×160 px in ph3 (Falcon **131 ms**), and the static layer couldn't scroll
incrementally because dimming wasn't baked in. Profiling Falcon (bench-falcon --conout 2): scroll ph0 79 /
ph1 7 / ph2 7 / **ph3 131** / present 89 → 190+89 = 279 ms = 3.58 fps; idle 225 ms.

Switch the TT/Falcon default to **PREDIM_DITHER** (the ST dim-at-draw style): draw_cell sources each tile's
pre-dimmed variant (dim_tile_d, per-cell light + 4×4 Bayer dither), so dimming folds into the tile blit and
ph3 becomes a plain copy. The chunky `predim_compose_static` path then also scroll-shifts g_dimmed_map and
re-dims only the lead band + light-changed cells (was full-rebuild-every-frame). dim_tile/g_darklut already
work at 256 colours (SMOOTH used the same luts), and the dither smooths the per-cell steps for high-colour
targets. **Falcon scroll compose 190 → 103 ms (ph3 131 → 9), frame 279 → 192 ms = 3.58 → 5.19 fps.** TT
benefits identically (same chunky path; its bench harness isn't emitting BENCH.BIN — separate TT-run issue).
ST is untouched (backend 0 still defaults PREDIM + native-planar; INV2 still 0). LIGHTSTY.REQ '0' restores
SMOOTH for an A/B or the fidelity path. Falcon shot verified clean.

**Caveat (next):** the chunky path only handles `can_scroll` incrementally — on an IDLE tick (dx=dy=0) it
still full-rebuilds + re-dims all cells (Falcon idle ph0 193 ms, total 209), so standing still is now the
worst case. Porting the ST idle-incremental (mark_anim_cells + the animated-cell dirty gate) to the chunky
path is the follow-up.

## 2026-07-24 — TT/Falcon: chunky idle-incremental — Falcon idle compose 209 → 50 ms
After the PREDIM switch, the chunky `predim_compose_static` still only handled `can_scroll` incrementally,
so an IDLE tick (dx=dy=0) full-rebuilt + re-dimmed all 121 cells every frame (Falcon idle ph0 193 ms, total
209) — standing still became the worst case. Port the ST native-planar idle path to the chunky layer: on
`can_idle`, keep g_dimmed_map and re-dim ONLY the cells whose STATIC content animates (mark_anim_cells →
g_pd_anim) or whose light changed — refresh their undimmed g_tmp_map (fill_tiles + gated
draw_view_objects pass-0), then dim_cell_into just those. The idle-recompose bookkeeping (g_pd_anim /
g_pd_dirty / mark_anim_cells) is now SHARED (moved out of the HAVE_RDR_ST guard); the persistent-g_ptmp bits
stay ST-only.

**Falcon idle compose 209 → 50 ms (ph0 193 → 34).** Gated `!g_native_planar` so the ST INV2 verify keeps its
chunky reference full-rebuilt (INV2 still 0). Validated by a new **chunky_idle_selftest** (the TT/Falcon
analogue of INV2 idle-verify): for f=1..4 anim ticks, prime full → chain f idle-incrementals → snapshot
g_dimmed_map → full-rebuild at the same state → byte-compare. **CHUNKY-IDLE diff = 0** on both the Falcon
bench and the ST bench (which forces the chunky path). ST native-planar path untouched.

### TT/Falcon frame budget after this pass (Falcon 68030, VIDEL truecolor)
| | before (SMOOTH) | after (PREDIM + chunky idle) |
|---|--:|--:|
| scroll compose | 190 | 103 |
| idle compose | 225 | 50 |
| present (8→16-bit) | 89 | 89 |
| **scroll frame** | 279 ms (3.58 fps) | **192 ms (5.2 fps)** |
| **idle frame** | 314 ms (3.2 fps) | **139 ms (7.2 fps)** |

TT benefits identically (same chunky path; its bench harness still isn't emitting BENCH.BIN). The remaining
Falcon cost is the **89 ms VIDEL 8→16-bit present** — now the dominant per-frame cost and the next lever
(rdr_falcon.c, Falcon-specific).

## 2026-07-24 — TT bench harness: fit the full pack (--ttram 4 → 16), real TT numbers
`make bench` (TT) had never emitted BENCH.BIN — it aborted with "cannot load asset pack (unknown game magic)".
Root cause: NOT a timing/TOS issue but OOM — the full 256-colour U6.PAK (~2.5 MB) + TPA overflow a 4 MB TT
fast pool, so plat_load_file returns NULL, gamedef_for_magic(NULL) fails, and the bench aborts before the
timed loops. `--ttram 4 → 16` (8 MB is the threshold; ST-RAM left at its larger default — `--memsize 4` also
fails) + `--run-vbls 9000 → 12000`. This is the [[fast-pool-tight-on-tt]] concern: a real 4 MB TT needs a
slim/streamed pack; the bench measures COMPUTE, so extra RAM is fine. (The same --ttram 4 limit affects the
other full-pack TT targets — run-game, the map/read/status dumps — a separate sweep.)

**Real TT numbers (TT030 32 MHz 68030, 256-colour), n=20:** SCROLL compose 40.8 ms, FULL 102.5 ms, IDLE
compose 20 ms, present (8-bit copy+double) 49.8 ms; CHUNKY-IDLE diff = 0. So **scroll 40.8+49.8 = 90.6 ms
(11.0 fps), idle 20+49.8 = 69.8 ms (14.3 fps)** — TT is the fastest target: 32 MHz (2× Falcon's 16 MHz) makes
compose ~2.5× Falcon's, and the 8-bit 256-colour present (49.8 ms) beats Falcon's 89 ms truecolor expand.

### Full per-target picture after the M7 render campaign (compose + present, per frame)
| target | clock | scroll | idle |
|---|---|--:|--:|
| ST shifter | 8 MHz | 264 ms (3.8 fps) | 149 ms (6.7 fps) |
| STE +Blitter | 8 MHz | 201 ms (5.0 fps) | 88 ms (11.4 fps) |
| Falcon VIDEL truecolor | 16 MHz | 192 ms (5.2 fps) | 139 ms (7.2 fps) |
| TT 256-colour | 32 MHz | 91 ms (11.0 fps) | 70 ms (14.3 fps) |

## 2026-07-24 — TT 4 MB fit: slim pack (U6TT.PAK) + fast→chip pack-load fallback
> (Historical. Since 2026-08-16 there is no slim pack / `ST_STREAM` build: one `U6.PAK` + sectioned
> `U6FE.PAK` for all targets, resident-vs-streamed decided at boot — [memory_streaming.md](memory_streaming.md).)
The full 256-colour U6.PAK (~2.5 MB) can't run on a stock 4 MB TT (the "unknown game magic" OOM). Two parts:
1. **Slim TT pack** — `world-tt` runs `MKWORLD_SLIM` (same front-end split as the ST's U6ST.PAK) but keeps the
   FULL 256-colour tileset the TT renders directly → **U6TT.PAK 1.08 MB**. New `gamett.prg` build loads it
   (NOT ST_STREAM: that frees U6FE via plat_chip_reset which only reclaims CHIP RAM, but on TT plat_load_file
   uses FAST RAM, so U6FE would never free — the TT slim build drops the front-end and boots to the baked
   start, gated on the absent INTRO/CHARGEN/MENU.REQ).
2. **fast→chip fallback** — the pack loaded `plat_load_file(...,fast=1)` into TT-RAM, but a 4 MB TT's ~2.7 MB
   fast pool can't hold pack + full-engine buffers while 13 MB of ST-RAM sits idle. Retry with `fast=0` (CHIP)
   on NULL → the pack lands in ST-RAM and the game FITS. General robustness win (ST/Falcon unaffected — their
   fast alloc succeeds or falls back to the single pool).

**Fits every stock-4 MB split** (2+2, 4+0 ST-only, 0+4): all load + render (256-colour shot verified). Cost of
the ST-RAM fallback: tiles read slower, so **scroll compose 40→51 ms, idle 20→25 ms (~25%, not a cliff — the
030 cache absorbs most tile reads)**. Even the ST-RAM path is **scroll 102 ms (9.8 fps) / idle 76 ms (13 fps)**;
a big-RAM TT keeps tiles in fast RAM (40/20). Front-end (intro/chargen) is the one TT-slim gap — needs ST_STREAM
taught to free TT-RAM, or a 256-colour resident front-end.

## 2026-07-24 — TT-slim front-end restored: stream U6FE into CHIP so it frees on TT
The TT slim build had dropped the front-end (intro/chargen/menu) because ST_STREAM loaded U6FE with
`plat_load_file(...,fast=1)` (TT-RAM) but freed it with `plat_chip_reset` (CHIP only) — so on TT the 1.35 MB
U6FE leaked and the gameplay core OOM'd. Fix: load U6FE with `fast=0` (CHIP) — gamedemo.c ~5205 — so
plat_chip_reset actually reclaims it (no-op on the ST, where fast==chip). Re-enabled `-DST_STREAM=1` for
gamett.prg. The front-end art is native 256-colour (the ST merely quantises to 16 per scene; `rdr_set_lowcolor`
is a no-op on TT/Falcon), rendered via the standard `rdr_set_palette(256)` per-scene path — so it renders
directly on the TT. Verified: chargen text + gypsy scene render on TT, gameplay core boots after the streamed
U6FE frees (256-colour shots), and the ST front-end is unchanged (fast=0 is a no-op there). The TT slim build
now has the full front-end AND fits a stock 4 MB TT.

## 2026-07-24 — Falcon: default to 16-bit truecolor present — 89 → 27 ms
The default Falcon present was 8-bit INDEXED (VIDEL planar) via per-row `c2p_span_asm` — chunky→8-plane bit
manipulation, **89 ms**. But the Falcon also has a 16-bit TRUECOLOR mode whose present is just `tc_span` (8-bit
index → RGB565 via a 256-entry LUT, NO c2p): **27 ms, a 3.3× win**. Flip the default (VIDEO.CFG='I' forces the
indexed A/B). Quality is ~VGA-identical: it shows the same 256-colour palette, RGB565 only drops 1 LSB on R and
B vs the 6-6-6 CLUT (green exact) — imperceptible; if anything truecolor is the higher-fidelity Falcon mode.
The expand LUT (g_pal16) rebuilds on every rdr_set_palette, and PREDIM bakes lighting into tile INDICES (not
palette rotations), so the LUT is stable per scene. Falcon truecolor gameplay shot verified clean.

**Falcon frame after the full pass: scroll 103+27 = 130 ms (7.7 fps), idle 50+27 = 77 ms (13 fps)** — from the
3.58 fps we started at. Final per-target picture (compose+present):
| target | clock | scroll | idle |
|---|---|--:|--:|
| ST shifter | 8 MHz | 264 ms (3.8 fps) | 149 ms (6.7 fps) |
| STE +Blitter | 8 MHz | 201 ms (5.0 fps) | 88 ms (11.4 fps) |
| Falcon truecolor | 16 MHz | 130 ms (7.7 fps) | 77 ms (13.0 fps) |
| TT 256-colour | 32 MHz | 91 ms (11.0 fps) | 70 ms (14.3 fps) |

## 2026-07-24 — TT/Falcon default back to SMOOTH (VGA fidelity) — PREDIM now an opt-in fast mode
A night A/B (midnight interior, TT) showed PREDIM_DITHER's ordered dither IS visible in torch/night lighting
gradients vs SMOOTH's clean per-pixel falloff — SMOOTH matches the VGA/Nuvie model (smooth per-pixel globes,
lighting.md §5), PREDIM_DITHER reads closer to the old ST port. Per the "indistinguishable from VGA" bar,
default TT/Falcon back to **SMOOTH**. PREDIM_DITHER stays available as a FAST MODE via LIGHTSTY.REQ '4'
(daytime/uniform light is byte-identical either way). The truecolor present (27 ms) is kept — it helps SMOOTH
too. Honest SMOOTH numbers (compose+present): TT scroll 140 ms (7.1 fps) / idle 159 ms (6.3 fps); Falcon
scroll 218 ms (4.6 fps) / idle 253 ms (4.0 fps).

The cost is the per-frame lighting SMOOTH can't cache (idle re-lights + full-rebuilds every frame). The
follow-up that recovers PREDIM's speed AT VGA quality is **smooth-PREDIM**: bake the per-pixel bilinear light
into the cached static layer (dim per-pixel, not per-cell) so lighting is smooth AND incremental — deferred.

## 2026-07-27 — smooth-PREDIM: VGA-faithful lighting at PREDIM speed (new TT/Falcon default)
Built the deferred follow-up. `LSTY_SMOOTH_PREDIM` (style 5) bakes the **per-PIXEL bilinear** SMOOTH light
into the cached static layer (`g_dimmed_map`) instead of a per-cell darklut level. The static is maintained
incrementally exactly like PREDIM — scroll shifts `g_dimmed_map`/`g_dm_light` and re-dims only the revealed
band + light-changed cells; idle re-dims only animated cells (light is invariant on an idle tick, so the whole
cached static is reused). Actors, fields and roofs dim **at draw** per-pixel (`draw_cell`/roof loop via the
shared `smooth_lp`), so the dynamic layer matches SMOOTH's ph3 too. Chunky-only (per-pixel ⇒ never
native-planar). Two wrinkles vs per-cell PREDIM, both surfaced by the diagonal-scroll selftest:
- a cell's per-pixel light reads its 3×3 light neighbourhood, so a scroll light-change is **dilated by one
  cell** (and the revealed band is folded into the change mask, since a band cell's shifted `g_dm_light` is
  stale garbage the delta test can't trust);
- `smooth_lp` **clamps** at the view's left/top edges (`sx=max(0,X-8)`), so those edge cells' `Lp` is NOT
  translation-invariant — they're re-dimmed unconditionally each scroll (they aren't always in the lead band).
`dim_cell_smooth` also has a uniform-neighbourhood fast path (flat darklut level, byte-identical) covering
ambient edges at night and all of daytime — the bulk of re-dimmed cells on a walk.

**Byte-identical to plain SMOOTH.** `smooth_predim_selftest` composes SMOOTH-PREDIM (full / idle-tick /
6-step mixed h-v-**diagonal** scroll walk with accumulation) and byte-compares the presented map region to a
full SMOOTH rebuild: **diff = 0 at night (STARTMIN 1320 & 300) and day (720)**. Since SMOOTH is the validated
VGA-faithful reference, the visual is proven identical by construction — no fidelity cost. (The diagonal walk
was essential — an axis-aligned-only walk missed both wrinkles above.)

Measured (compose ms; night = the non-uniform worst case, day = uniform light):
| target | SMOOTH night s/i | **SMOOTH-PREDIM** night s/i | **SMOOTH-PREDIM** day s/i | PREDIM_DITHER night s/i |
|---|--:|--:|--:|--:|
| TT (32 MHz, +49 ms present) | 94 / 114 | **84 / 26** | **50 / 26** | 44 / 20 |
| Falcon (16 MHz, +27 ms present) | 196 / 231 | **194 / 65** | **125 / 65** | 111 / 50 |

Idle (the common standing/reading/menu/combat frame) is **~4× faster** than SMOOTH at the SAME pixels — TT
idle frame 163→75 ms (6.1→13.3 fps), Falcon idle frame 258→92 ms (3.9→10.9 fps). Scroll is a TT win and a
Falcon wash at night (the moving globe arc's per-pixel bilinear is inherently costly at 16 MHz), a clear win
by day. So **SMOOTH-PREDIM is the new TT/Falcon default** (`gamedemo.c` ~6629): VGA quality, idle transformed
and walk no worse than the SMOOTH it replaces. Plain SMOOTH ('0') stays the fidelity reference; PREDIM_DITHER
('4') the low-colour fast mode. ST unchanged (native-planar PREDIM). Final per-target picture (night; +present):
| target | clock | scroll | idle |
|---|---|--:|--:|
| ST shifter | 8 MHz | 264 ms (3.8 fps) | 149 ms (6.7 fps) |
| STE +Blitter | 8 MHz | 201 ms (5.0 fps) | 88 ms (11.4 fps) |
| Falcon truecolor | 16 MHz | 221 ms (4.5 fps) | 92 ms (10.9 fps) |
| TT 256-colour | 32 MHz | 133 ms (7.5 fps) | 75 ms (13.3 fps) |

## 2026-10-28 — world turn at dusk: mover-type memo + cached walker routes, TT 36.5 s → 0.18–0.7 s

A turn at 20:00–21:00 was dominated by `step_npcs`: ~157 schedule walkers each ran the 33x33 BFS
(`npc_pathstep`), which floods its whole window whenever the goal is outside it. Two changes:

1. **`mover_type()` memo** (behaviour-identical). `mover_can_enter` called
   `monster_movetype(actor_objn(g_mover))` for every neighbour the BFS tested, and `actor_objn` scans
   the 1024-entry base-tile table: ~4,000 table scans per search. The type is now kept while the mover,
   its `g_abase` and `g_basetile` are unchanged. All actor positions after the REST.REQ '4' sequence
   hash the same as before (52428615) on TT and Falcon.
2. **Per-walker route cache** (`npc_routestep`, schedule walkers in `step_npcs` only; hostile, coward,
   ally, hatched and party pathing unchanged). The BFS now also records the direction into each cell,
   so the route to the cell it picked is kept (first `RT_MAX`=16 steps) and followed on later turns. It
   is searched again when it runs out, when the walker isn't where the route left it, when the goal,
   level or `build_blocked` epoch changed, or when the next tile is now impassable or holds the Avatar.
   A search with no step is not repeated for `RT_RETRY`=8 turns. Deterministic: TT and Falcon positions
   hash identically (79803404). Routes differ in detail from the per-turn search, but arrival matches:
   after the sequence plus 200 turns, 38 at goal / 66 within the wander radius vs 37 / 63 before
   (157 walkers); 66 vs 67 at goal at the first check.

REST.REQ '4', ms per plain turn (Hatari, TT 32 MHz `--ttram 4`; Falcon `--memsize 4 --ttram 0`):
| case | TT before | TT memo | **TT both** | Falcon before | Falcon memo | **Falcon both** |
|---|--:|--:|--:|--:|--:|--:|
| castle 08h | 5718 | 505 | **155** | 12482 | 1392 | **388** |
| 21h, first 12 turns (all routes new) | 36492 | 2852 | **706** | 79603 | 8003 | **1870** |
| 21h, next 12 | — | 2444 | **305** | — | 6883 | **776** |
| 21h, next 12 | — | 2152 | **183** | — | 6066 | **449** |

(The "before" 21h runs were only timed for the first block. These are far above the ~1.7 s TT figure
measured earlier, so that figure presumably came from a different case. Both use the same hook.)

(Contributed by the u7port session on 2026-10-28, applied and re-verified here: REST.REQ '4' and EGG.REQ '4'
identical on TT and the 4 MB Falcon with the cached routes; completion-checklist item 17.)

## 2026-11-02 — LZW in hand-asm (`dataformats/u6lzw_68k.s`); ST game start 11.8 → 5.9 s

- **LZW decode** (converter and in-game conversations/portraits): the C loop now copies each string
  forward from where it was first decoded; the Atari builds run the same loop in 68000 asm (3-byte code
  read at a bit offset, entry added before the lookup so KwKwK needs no branch, separate loops for the
  first code after a clear and a full dictionary). ST, the game's 100 LZW files: old C 244 s, new C
  198 s, asm 68 s. Details and the equivalence checks: first_boot_convert.md §2a.
- **Game start on a 4 MB ST**, program start to the main loop (Hatari, TOS 1.04): 11.8 → 5.9 s.
  `npc_base_fc` scanned all 1024 BASETILE entries per actor (2.6 s; now a table); `build_blocked`
  classified every object's tile four times over eight passes (3.9 s; now a per-tile cache built once
  and two passes, 1.3 s — this is also the stall when a door opens or a torch is lit); the indoor
  bitmap was unpacked a byte at a time (1 s; now `memset` per run). FG-INDEX / LS-INDEX full-vs-sparse
  self-tests in the ST bench: 0.

## 2026-11-13 — ST combat: from seconds a key to under one (KEYPROF)

A Mega STe owner (TOS 2.06, 4 MB) reported "literally some few seconds between Attack - direction - target"
and full top-to-bottom redraws between rounds. `make game-st-prof` builds `gamestp.prg` with `-DKEYPROF`:
every main-loop pass over 50 ms prints its time split by phase (world turn parts, compose, light, static,
full and map present, panels) on the console (`--conout 2`). Measured in Hatari on an 8 MHz Mega STe, 4 MB,
in the throne room at 08:00 with combat on (B), the crosshair (A, two steps), Enter at nothing, Space, steps:

| key | before | after |
|---|--:|--:|
| B (combat on) | 1165 ms | 335 ms |
| A (crosshair up) | 1695 | 980 |
| crosshair step | 1685 | 450 |
| Enter (attack) | 8895 | 1785 |
| Space (pass) | — | 1035 |
| step in combat | 5245–6180 | 850–2415 |

What it was, in order of size:
1. **`pick_foe`, 5.7 s of Enter's 8.9.** The three companions on assault looked for a foe every turn, testing
   `can_target` (and so `npc_tile`) on all 255 actors before the cheap 8-tile reach, and `npc_tile` reached
   `actor_objn`, which scanned all 1024 BASETILE entries per call. `actor_objn` is now a table (base tile ->
   smallest obj_n, built once per BASETILE) and the reach is tested first. Same results (no side effects, the
   same candidates and tie rolls).
2. **The full present, 950 ms on every key.** `present_frame` c2p'd the whole 320x200 for a line of text.
   rdr_st now keeps a chunky copy of what is on screen (`g_shadow`, 64000 bytes) and `rdr_present_diff`
   converts only the 16-pixel groups that differ (runs per row); every present keeps the copy in step and a
   LUT change, flipping, the intro scroll or the dissolve make it full again. 950 -> 180-210 ms.
3. **The compose for a crosshair step, 345 ms**: light (85) and the static layer (200) recomputed though only
   the crosshair moved. `compose_cursor` takes the idle path; the planar idle patch now restores the
   crosshair's old and new cells (`xhair_cell`, `g_xh_cell`). 345 -> 40 ms.
4. **The panels.** `ui_text` and `blit_tile` went through `ui_px` (bounds and row multiply per pixel): row
   pointers when wholly on screen. The map border's 40 masked tiles were drawn on every compose: each strip is
   kept and not drawn while it is still on screen (the date line overwrites the bottom one). The library
   `memcmp` compares a byte at a time on the 68000 (its `memcpy` uses movem): compare by longs.

Still to do: the rest of an action's compose (light + full static rebuild, ~285 ms), `sched_pick` (~150-280 ms
a key), the step that took a full present (a LUT change), and the panel draws (status twice per key).
