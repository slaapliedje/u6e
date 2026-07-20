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
