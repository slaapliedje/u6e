# Renderer backend unification (M8)

Goal (M8 acceptance): the whole hardware matrix driven by ONE detection routine — "goals #2 & #3
collapsed into one code path." Previously each target was its own binary linking one backend
(`rdr_st.c` / `rdr_tt.c` / `rdr_falcon.c` / `rdr_vcard.c`), because every backend defined the same
`rdr_*` symbols and only one could link at a time.

## Design: a vtable + a runtime dispatcher
- **`render/render_backend.h`** declares `rdr_vtable` — the full public renderer surface (25 entry
  points) as function pointers — plus a per-backend **namespacing** mechanism. A backend does, after
  its includes:
  ```c
  #define RDR_BACKEND st        // st | tt | falcon | vcard
  #include "render_backend.h"
  ```
  which macro-renames its entry points to `st_rdr_*` (unique file globals) and provides
  `RDR_MAKE_VTABLE(st)` — emitted at the bottom of the file as `const rdr_vtable rdr_be_st`. So all
  four backends can link into the SAME binary with no symbol clashes; each exposes exactly one
  global (its vtable). (The three `scroll_*` vtable fields are named `scrl_*` on purpose — the
  namespace macros `#define scroll_begin …`, which would otherwise rewrite the field designators.)
- **`render/render.c`** implements the public `rdr_*` / `scroll_*` interface (what the engine calls)
  as a thin forwarder over one selected backend, chosen once at startup:
  ```
  EdDI cookie present     -> rdr_be_vcard     (graphics card / extended VDI: fVDI, NVDI)
  else _VDO (platform_detect):  Falcon -> rdr_be_falcon,  TT -> rdr_be_tt,  ST/STE -> rdr_be_st
  ```
  **EdDI-first** because an extended VDI (a card, or NVDI on built-in video under AES/MiNT) means we
  must present cooperatively *through* the VDI, not `Setscreen` the display out from under it. Bare
  single-TOS video has no EdDI, so we take the direct-hardware path. Selection is lazy + idempotent
  (`rdr_prefer_truecolor` may run before `rdr_init`; both trigger it). `rdr_active_backend()` reports
  the choice for diagnostics.
- Which backends are compiled in is a per-binary choice via `-DHAVE_RDR_*`; the SAME `render.c`
  selection code serves any subset (guarded by the defines). One `#error` guards the empty set.

## Why it is not literally one .prg for the entire line
Two constraints are fundamental and NOT renderer concerns, so they still fork the *binary* (never the
renderer code):
1. **CPU tier.** The flagship is the `-m68020-60`-codegen hybrid (68020 integer ops in the hot paths);
   those opcodes fault on a plain 68000. So the 68000 line (ST/STE, and 68000+card) needs a separate
   `-m68000` image. You cannot have one image that is both 68000- and 68020-optimised.
2. **Memory + runtime model.** A 4 MB ST cannot hold the full pack, so it streams a slim pack
   (`ST_STREAM`, `U6ST.PAK`); the card path additionally links `libgem16` and runs as an AES app.

So the binaries are:

| binary        | CPU        | backends linked (HAVE_RDR_*)        | covers                              |
|---------------|------------|-------------------------------------|-------------------------------------|
| **game.prg**  | 68020 hyb  | st + tt + falcon + vcard (+libgem16)| **TT, Falcon (VIDEL), AND a card** — one image, runtime-selected |
| gamest.prg    | 68000      | st                                  | plain ST / STE (4 MB slim pack)     |
| gamevc.prg    | 68000      | vcard (+libgem16)                   | any 68000 Atari with a card         |
| gamef.prg     | 68020 hyb  | falcon                              | Falcon-only isolated test image     |

Every one of them uses the **same `render.c` dispatcher + detection** — that is the "one code path"
the acceptance asks for. `game.prg` is the real collapse: it folds the former separate TT / Falcon /
card binaries into a single image that picks the backend at runtime.

## Memory: vcard buffers are pool-allocated, not static
`rdr_vcard`'s present buffers (backbuffer + c2p + device raster ≈ 460 KB) were static BSS. In the
unified binary that BSS would exist on every build and tip the tight TT pack pool into "no renderer".
They are now `plat_alloc_fast`'d in `rdr_init` (like `rdr_tt`'s backbuffer) — spent only when the
vcard backend is actually selected (a card machine has the RAM). The other three backends already
allocate their big buffers dynamically, so `game.prg`'s BSS is unchanged from the old TT build
(~1.05 MB) — st+tt+falcon add ~5.5 KB of static state between them.

## Validation
- `game.prg` (the ONE unified image) on **ARAnyM+fVDI** → dispatcher selects **vcard**; the full U6
  game renders through the card (640×480×8). `rdr_active_backend()` = 3.
- `game.prg` on **Hatari TT** (no EdDI) → dispatcher selects **tt**; the game renders via the direct
  TT-low shifter path, unchanged from before unification.
- All prior single-backend validations still hold (gamest ST-low, gamevc card, Falcon VIDEL) — each
  now reaches its backend through `render.c`.

## Follow-ups
- The st/tt/falcon backends still keep their own backbuffers; a shared backbuffer would shave a little
  more. Not needed for correctness.
