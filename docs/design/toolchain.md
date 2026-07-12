# Toolchain & Build (resolved)

Resolves the `<<FILL IN>>` toolchain placeholder in the root `CLAUDE.md`.
All values below were probed from the installed cross-compiler on 2026-06-23;
re-run the probes in "Verification" if the toolchain is upgraded.

## Cross-compiler (verified)
| Property            | Value                                              |
|---------------------|----------------------------------------------------|
| C compiler          | `m68k-atari-mint-gcc`  (`/usr/bin/`)               |
| C++ compiler        | `m68k-atari-mint-g++`  (present; engine stays C)   |
| Version             | GCC 15.2.0 (MiNT 20250810)                          |
| Target triple       | `m68k-atari-mint`  (`gcc -dumpmachine`)            |
| Default CPU / arch  | **68000** (`-march=68000`) — our base-ST floor     |
| Endianness          | big-endian (`__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__`) |
| `int` / pointer     | 32-bit each (`__SIZEOF_INT__ 4`, `__SIZEOF_POINTER__ 4`) |
| FPU                 | none assumed (consistent with the no-FPU rule)     |

Key builtin defines available to code: `__mc68000__`, `__MINT__`,
`__ATARI__` (via MiNT libc), `__BYTE_ORDER__`. Guard Atari-specific code on
`__mc68000__` / `__MINT__`.

## System include paths (verified)
The cross-gcc searches, in order:
1. `/usr/lib64/gcc/m68k-atari-mint/15/include`        (compiler headers: stddef, stdint, stdbool, …)
2. `/usr/lib64/gcc/m68k-atari-mint/15/include-fixed`
3. `/usr/m68k-atari-mint/sys-root/usr/include`        (MiNT libc + Atari bindings)

Atari/TOS bindings we rely on, confirmed present in the sys-root:
- `osbind.h` — XBIOS/BIOS/GEMDOS C bindings: `Setscreen`, `Physbase`,
  `Logbase`, `Setpalette`, `Getrez`, `Vsync`, etc. (see /render, /platform).
- `mint/cookie.h` — cookie jar access (`_VDO`, `_MCH`, `_CPU`, `EdDI`).
- `mint/falcon.h` — VIDEL bindings: `VsetMode`, `VgetMonitor`, `VgetSize`.
- `mint/sysvars.h` — system variables (`_hz_200`, VBL vector, etc.).
- `mint/osbind.h`, `mint/sysbind.h`, `mint/ostruct.h`, `mint/linea.h`.

NOT present here (install if/when needed):
- GEM AES/VDI bindings (`aes.h`/`vdi.h`/`gem.h`) ship in a separate **gemlib**
  package. /render's graphics-card present path (vq_extnd, v_opnbm, vro_cpyfm)
  needs it (for the `vcard` backend, M8). NOT required for M0–M7 (those use
  XBIOS/BIOS + cookie jar from osbind.h/mint/*).

## gemlib (INSTALLED) — GEM AES/VDI bindings
gemlib is installed into the sys-root and verified:
- Header: `/usr/m68k-atari-mint/sys-root/usr/include/gem.h` (on the default
  include path). It's a single umbrella header — VDI + AES calls (graf_handle,
  v_opnvwk, vq_extnd, vro_cpyfm, v_opnbm, v_clsbm, …) come in via its
  sub-headers, not by including `vdi.h`/`aes.h` directly. Just `#include <gem.h>`.
- Library: `libgem.a` present for the default multilib AND `m68020-60`
  (TT/Falcon) and the others — so both target builds can link the VDI path.
- Link flag: `-lgem`.
- Verified: `clangd --check` parses a `<gem.h>` + graf_handle/v_opnvwk/vq_extnd
  snippet with 0 errors (resolved via the same --query-driver path).

> Re-install note (other machines / rebuild): this toolchain was hand-installed
> (no pacman package owns `m68k-atari-mint-gcc`), so build gemlib from source
> against the cross-compiler into the same sys-root — NOT via a distro package:
> `git clone https://github.com/freemint/gemlib && cd gemlib && make CROSS=yes && sudo make CROSS=yes install`

## Multilib variants (verified: `gcc -print-multi-lib`)
| CFLAG            | Use for                                            |
|------------------|----------------------------------------------------|
| *(default)*      | base ST / STE — plain 68000, 32-bit int            |
| `-m68020-60`     | TT030 / Falcon030 — HARD-FLOAT libc (compile-only; link 68000 libc, see ⚠️) |
| `-m5475`         | ColdFire — **not a target**, ignore                |
| `-mshort`        | 16-bit-int ABI variant — **not used** (see caveat) |
| `-mfastcall`     | register-argument ABI variant — not used           |

## Recommended CFLAGS (starting point)
Common (all targets):
```
-std=gnu11 -Os -Wall -Wextra
```
> NOTE: this m68k gcc does NOT support `-ffunction-sections` / `-fdata-sections`
> / `-Wl,--gc-sections` (it warns "not supported for this target"). They were
> dropped from the Makefile — `-Os` already does the size work. `-ffreestanding`
> is used for clangd analysis but the actual builds are hosted on mintlib (crt0
> + GEMDOS libc), so the Makefile omits it for now.

Per target arch:
- base ST / STE:  `-m68000`
- TT / Falcon:    `-m68020-60`  (build a second variant; runtime detection in
  /platform still gates feature use, but 68020+ codegen needs the right binary)

> The engine ships as (at least) two binaries — a 68000 build that runs on the
> whole line, and a `-m68020-60` build for TT/Falcon speed. Both are driven by
> the SAME source + the SAME runtime detection routine (per milestones M8).

> ⚠️ **`-m68020-60` REQUIRES AN FPU — conflicts with the no-FPU rule.**
> Verified 2026-06-28 on a stock (FPU-less) Falcon030 under Hatari + real TOS
> 4.04: a `-m68020-60` binary aborts at startup with *"This program requires a
> 68881 or higher arithmetic coprocessor and cannot be run on this machine."*
> The `m68020-60` libc multilib is **hard-float** and its C startup gates on the
> `_FPU` cookie; `gcc -print-multi-lib` ships **no soft-float `m68020-60`
> variant** (`-msoft-float` still resolves to the same hard-float dir). 68040/68060
> have integral FPUs, so the multilib assumes one — but a base TT030 and a stock
> Falcon030 do NOT. Our earlier TT captures only ran because Hatari's emulated TT
> happens to include a 68882.
> **RESOLVED 2026-06-28 — the hybrid build.** The FPU abort comes solely from
> `checkcpu.o` inside the `m68020-60` libc startup (found via `strings` of
> `m68020-60/libc.a`); our engine emits **zero FPU instructions** under `-m68020-60`
> (`gcc -S | grep fmove…` = 0, since it uses no floats). So:
> - **Compile** every source with `-m68020-60` → 020 integer codegen (bitfield
>   `bfextu`/`bfins`, 32-bit `muls.l`/`divs.l`, scaled addressing).
> - **Link** against the **default 68000 soft-float libc** (no `checkcpu` gate, no
>   FPU instructions anywhere) — i.e. run the final `gcc` link step WITHOUT
>   `-m68020-60` so the driver picks the default multilib.
>
> Result: 020-fast hot paths + a libc that runs on a stock Falcon030 / FPU-less
> TT030. Same ABI (32-bit int) so the mixed objects link cleanly. Encoded as the
> `HYBRID_BUILD` make macro; **both** `game.prg` (TT) and `gamef.prg` (Falcon) use
> it. Verified: `gamef.prg` boots + renders on Hatari Falcon + real TOS 4.04 with
> no `--fpu`, and our objects contain 020-only opcodes (the plain-68000 build has
> none). (A fully 020-optimized *libc* would still need a soft-float 020-60 multilib
> — not built here — but libc is cold for a tile engine, so the hybrid captures the
> win. The hand-asm c2p stays 68000.)
>
> **2026-06-28 — the on-target DEV TOOLS use the hybrid build too.** `rdemo.prg`,
> `bench.prg`, and the TT/Falcon `selftest_020.prg` were still pure `-m68020-60`
> (FPU-gated). `rdemo`/`bench` now go through `HYBRID_BUILD` (`bench` passes
> `-DBENCH` via the macro's 4th arg); `selftest_020` keeps its per-ARCH object
> compile but **links without the arch flag** (default 68000 libc). All three:
> `strings | grep 68881` = 0. Runtime-verified on a no-FPU TT (no `--fpu`): `bench`
> writes BENCH.BIN with real numbers, `selftest_020` prints `RESULT: ALL PASS`.
> (Also fixed two pre-existing `bench` bugs surfaced by the rebuild: `BENCH_SRC` was
> missing `engine/combat.c` since the combat work, and the `make bench` parse hit an
> `od` 16-byte line-wrap + too-short run-vbls.)

## clangd setup (why it's split across two files)
clangd is the IDE C IntelliSense engine. Configuration lives in:
- `.clangd` (repo root, committed) — target/flags/diagnostics.
- `.vscode/settings.json` (committed) — the `--query-driver` launch arg.

**Why analysis works even though `clang -c` for m68k fails:** this host's clang
has no m68k *LLVM codegen backend* (so `clang -c`/compilation errors with "No
available targets are compatible with triple"). clangd never generates code — it
only runs the *frontend* (parse + semantic analysis), and clang 22's frontend
**does** ship the m68k `TargetInfo`. So the frontend analyses m68k cleanly, with
correct big-endian layout, 32-bit int/pointer, and `__mc68000__`/`__MINT__`
defined.

**The triple is inferred from the driver — do NOT set `--target` in `.clangd`.**
Because the `Compiler:` is the cross-gcc, `--query-driver` gets both the system
includes AND the triple from one query; clangd then drives the frontend with
`-triple m68k-atari-mint -target-cpu M68000` on its own. If you *also* add
`--target=...` to `.clangd`, clangd forwards it to gcc as `-target` (a clang-only
flag gcc rejects), the system-include query fails, and `<osbind.h>` stops
resolving. Verified with `clangd --check` (clean: "All checks completed, 0
errors", system includes extracted, triple `m68k-atari-mint`).

**Why `--query-driver` is in settings.json, not `.clangd`:** it authorizes
clangd to *execute* the named compiler to harvest the include paths and triple.
clangd refuses to take that authorization from a checked-in project file (a
cloned repo could otherwise run arbitrary binaries), so it is a launch argument
only.

**Verify the IDE setup yourself:**
```sh
clangd --check=<some-file.c> "--query-driver=/usr/bin/m68k-atari-mint-*"
# expect: "System includes extractor: successfully executed ...m68k-atari-mint-gcc"
#         "-triple m68k-atari-mint" in the cc1 args, and "0 errors".
```

**`Remove:` list in `.clangd`** strips gcc m68k/Atari flags the stock clang
frontend rejects (`-m68020-60`, `-m5475`, `-mshort`, `-mfastcall`, `-mpcrel`,
`-mbaserel`, `-fleading-underscore`). This matters once a real
`compile_commands.json` (below) feeds clangd the actual build flags.

### compile_commands.json (recommended, generated)
clangd is best with a compile database. It's gitignored (absolute, per-machine
paths). Generate it one of two ways:
- Make-based build:  `bear -- make`   (produces ./compile_commands.json)
- CMake-based build: set `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, symlink the file
  from the build dir to the repo root.
Until one exists, clangd falls back to the flags in `.clangd` (good enough to
edit, with system headers resolved via `--query-driver`).

### `-mshort` caveat
If the build ever adopts `-mshort` (16-bit `int`, the classic Atari ABI), clangd
cannot mirror it — clang's m68k frontend has no `-mshort`, and `.clangd` strips
it, so clangd keeps modelling 32-bit `int`. That would desync `sizeof(int)`,
integer promotion, and struct layout between editor and build. We currently use
the **default 32-bit-int multilib**, so this is a non-issue; revisit only if
`-mshort` is deliberately adopted (and prefer fixed-width `<stdint.h>` types in
shared code regardless).

## Emulator / runtime (dev)
- Hatari + EmuTOS, GEMDOS hard-disk emulation (no real floppies needed).
- Configure ST / STE / TT / Falcon machine variants to exercise every
  detection branch (see /platform and milestone M0).

## Verification (re-run after any toolchain upgrade)
```sh
m68k-atari-mint-gcc -dumpmachine                  # expect: m68k-atari-mint
m68k-atari-mint-gcc -print-multi-lib              # confirm multilib list above
echo | m68k-atari-mint-gcc -E -Wp,-v -            # confirm include search dirs
# clangd frontend sanity (must parse with no error):
echo 'int a[__BYTE_ORDER__==__ORDER_BIG_ENDIAN__?1:-1];' \
  | clang --target=m68k-atari-mint -fsyntax-only -xc -
```
