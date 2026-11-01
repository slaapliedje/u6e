#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# make-st-disks.sh — build a set of Atari .ST floppy images (+ a hard-drive
# installer) for the u6e engine from YOUR OWN original game data.
#
# We ship NO copyrighted assets (see CLAUDE.md clean-room rules). This script,
# the installer source, and the docs are the only committed parts; the packs it
# reads (build/*.PAK) are produced from your original Ultima VI data in refdata/
# and the .ST images it emits are gitignored. Provide your data, run this, and
# you get floppy images that install and run on a real Atari (or a Gotek/HxC
# floppy emulator, or an emulator).
#
# The front-end pack U6FE.PAK (~1.8 MB) is bigger than a 1.44 MB floppy, so the
# script splits big packs across disks and the installer (INSTALL.TOS) reassembles
# them on the target drive. Every target ships the SAME data pair — U6.PAK (the
# gameplay core) + U6FE.PAK (front-end art the engine keeps resident or streams
# by section depending on the machine's RAM; docs/design/memory_streaming.md).
#
# Usage:
#   scripts/make-st-disks.sh --target falcon [options]
# Options:
#   --target falcon|st|tt   which machine to build for            (default falcon)
#   --out DIR               where to write the .ST images         (default dist/st)
#   --media DIR             also copy the images here (e.g. a Gotek SD folder)
#   --size 1440|720         floppy size in KB                     (default 1440)
#   --dest PATH             default install folder on the Atari   (default C:\U6)
#   --refdata DIR           your original game data dir            (default refdata/dos/u6)
#   --name PREFIX           image filename prefix                 (default u6)
#   --version STR           version string for the README         (default: git short hash)
#   --no-build              don't run make; require prebuilt artifacts
#   --keep                  keep the staging dir (for debugging)
#   -h|--help               this help
set -euo pipefail

# ---- config / args ---------------------------------------------------------
TARGET=falcon
OUT=dist/st
MEDIA=
SIZE=1440
DEST='C:\U6'
REFDATA=refdata/dos/u6
NAME=u6
VERSION=
DO_BUILD=1
KEEP=0

usage() { sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    --target) TARGET=$2; shift 2;;
    --out)    OUT=$2; shift 2;;
    --media)  MEDIA=$2; shift 2;;
    --size)   SIZE=$2; shift 2;;
    --dest)   DEST=$2; shift 2;;
    --refdata) REFDATA=$2; shift 2;;
    --name)   NAME=$2; shift 2;;
    --version) VERSION=$2; shift 2;;
    --no-build) DO_BUILD=0; shift;;
    --keep)   KEEP=1; shift;;
    -h|--help) usage 0;;
    *) echo "unknown option: $1" >&2; usage 1;;
  esac
done

# repo root = parent of this script's dir
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD=build
[ -n "$VERSION" ] || VERSION="$(git rev-parse --short HEAD 2>/dev/null || echo dev)"

# ---- tool checks -----------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || { echo "missing tool: $1" >&2; exit 3; }; }
need mformat; need mcopy; need dd
CC=m68k-atari-mint-gcc
command -v "$CC" >/dev/null 2>&1 || { echo "missing cross-compiler: $CC" >&2; exit 3; }

# ---- per-target file set ---------------------------------------------------
# PRG_SRC:PRG_DOS  = engine binary (built) and its 8.3 name on the disk
# MAKE_TGT         = make targets that produce PRG + packs from your refdata
# PACKS            = data packs (built into build/, split across disks as needed)
# AUX              = extra original data files copied verbatim from REFDATA — the
#                    engine loads these as separate files (NPC dialogue + faces).
AUX=(CONVERSE.A CONVERSE.B PORTRAIT.A PORTRAIT.B)   # U6; the engine loads these by name
# Music (M10): the .M songs + the MT-32 map are TINY (~11 KB total). Shipping them
# enables the YM2149 PSG chiptune backend on ANY Atari (the default when no digital
# PCM is present), and MT-32 / MIDI-OUT if the player adds a 1-byte MIDI.CFG='1'.
# The DMA "AdLib" PCM path is host-pre-rendered and huge (~20 MB) — too big for a
# floppy set; drop build/music/*.PCM into the install folder on a CF/HD to use it.
for _m in "$REFDATA"/*.M; do [ -e "$_m" ] && AUX+=("$(basename "$_m")"); done
[ -e "$REFDATA/MIDI.DAT" ] && AUX+=(MIDI.DAT)
case "$TARGET" in
  falcon)
    PRG_SRC=$BUILD/gamef.prg;  PRG_DOS=GAMEF.PRG
    MAKE_TGT=(game-falcon world)
    PACKS=(U6.PAK U6FE.PAK)
    TITLE="Ultima VI for the Atari Falcon030 ($VERSION)"
    ;;
  st)
    PRG_SRC=$BUILD/gamest.prg; PRG_DOS=GAMEST.PRG
    MAKE_TGT=(game-st world)
    PACKS=(U6.PAK U6FE.PAK)
    TITLE="Ultima VI for the Atari ST/STE ($VERSION)"
    ;;
  tt)
    PRG_SRC=$BUILD/gamett.prg; PRG_DOS=GAMETT.PRG
    MAKE_TGT=(game-tt world)
    PACKS=(U6.PAK U6FE.PAK)
    TITLE="Ultima VI for the Atari TT030 ($VERSION)"
    ;;
  *) echo "unknown --target: $TARGET (use falcon|st|tt)" >&2; exit 1;;
esac

# ---- build artifacts if needed --------------------------------------------
if [ "$DO_BUILD" = 1 ]; then
  echo ">> building engine + packs for '$TARGET' (make ${MAKE_TGT[*]})"
  if ! make "${MAKE_TGT[@]}"; then
    echo "!! make failed. Put your original Ultima VI data in refdata/ (see" >&2
    echo "   refdata/README.md) and ensure the toolchain is set up, then retry." >&2
    exit 4
  fi
fi

# verify everything we need exists now
miss=0
[ -f "$PRG_SRC" ] || { echo "!! missing engine binary: $PRG_SRC" >&2; miss=1; }
[ -f "$BUILD/SAVE.000" ] || { echo "!! missing save template: $BUILD/SAVE.000 (make save-template)" >&2; miss=1; }
for p in "${PACKS[@]}"; do [ -f "$BUILD/$p" ] || { echo "!! missing pack: $BUILD/$p" >&2; miss=1; }; done
for a in "${AUX[@]}"; do [ -f "$REFDATA/$a" ] || { echo "!! missing data file: $REFDATA/$a (set --refdata)" >&2; miss=1; }; done
[ "$miss" = 0 ] || { echo "   (run without --no-build, or build the make targets first)" >&2; exit 4; }

# ---- disk geometry ---------------------------------------------------------
case "$SIZE" in
  1440) SECTORS=2880; CAP=1455000; VOLENG=U6ATARI;;
  720)  SECTORS=1440; CAP=728000;  VOLENG=U6ATARI;;
  *) echo "unsupported --size: $SIZE (use 1440 or 720)" >&2; exit 1;;
esac
MINFILL=8192          # don't start a split part with less than this free on a disk
rup() { echo $(( ( ($1 + 511) / 512) * 512 )); }   # round up to a 512-byte cluster
fsize() { stat -c %s "$1"; }

# ---- staging ---------------------------------------------------------------
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/u6stdisk.XXXXXX")"
cleanup() { [ "$KEEP" = 1 ] || rm -rf "$STAGE"; }
trap cleanup EXIT
mkdir -p "$OUT"

# compile the installer fresh (pure installer logic, no game data)
echo ">> compiling installer -> INSTALL.TOS"
"$CC" -m68000 -std=gnu11 -Os -Wall -Wextra -o "$STAGE/INSTALL.TOS" installer/install.c
m68k-atari-mint-strip "$STAGE/INSTALL.TOS" 2>/dev/null || true

# stage the engine binary + README under their DOS names
cp "$PRG_SRC" "$STAGE/$PRG_DOS"

README="$STAGE/README.TXT"
{
  printf '%s\r\n' "$TITLE"
  printf '%s\r\n' "======================================================"
  printf '\r\n'
  printf '%s\r\n' "A clean-room Ultima VI engine for the Atari line, built"
  printf '%s\r\n' "from YOUR original game data. Ships no copyrighted assets."
  printf '\r\n'
  printf '%s\r\n' "INSTALL"
  printf '%s\r\n' "  1. Boot to the desktop.  Insert disk 1."
  printf '%s\r\n' "  2. Run INSTALL.TOS.  Choose a folder (default $DEST)."
  printf '%s\r\n' "  3. Swap disks when asked; it reassembles the data packs."
  printf '\r\n'
  printf '%s\r\n' "PLAY"
  printf '%s\r\n' "  Open the install folder and run $PRG_DOS."
  printf '%s\r\n' "  You boot to the title screen: watch the Introduction,"
  printf '%s\r\n' "  Create a Character, or Journey Onward to start playing."
  printf '%s\r\n' "  Arrow keys move; letters are commands (S=save, L=look,"
  printf '%s\r\n' "  U=use, G=get, T=talk ...).  Enjoy!"
  printf '\r\n'
  printf '%s\r\n' "MUSIC"
  printf '%s\r\n' "  Plays as YM2149 chiptune by default (any Atari)."
  printf '%s\r\n' "  Press F1 (title menu or in game) to step through the music"
  printf '%s\r\n' "  sources - automatic / MIDI out (Roland MT-32) / MIDI out"
  printf '%s\r\n' "  (General MIDI keyboard or module) / DMA sound / PSG - the"
  printf '%s\r\n' "  choice is shown and saved to MIDI.CFG (one byte: 0 auto,"
  printf '%s\r\n' "  1 MT-32, 2 DMA, 3 PSG, 4 General MIDI)."
  printf '%s\r\n' "  AdLib digital audio (STE/TT/Falcon): copy the pre-rendered"
  printf '%s\r\n' "  *.PCM files into this folder (see the project docs)."
  printf '%s\r\n' "  MIDITEST.PRG checks the MIDI port + cables: it plays a"
  printf '%s\r\n' "  piano scale out (Atari OUT -> instrument IN) and prints"
  printf '%s\r\n' "  what the instrument sends back (instrument OUT -> Atari IN)."
} > "$README"

# MENU.REQ: presence (non-empty) tells the engine to show the U6 title/main menu
# on boot (Introduction / Create a Character / Journey Onward) instead of dropping
# straight into gameplay. This is the authentic interactive boot.
printf '1' > "$STAGE/MENU.REQ"

# ---- packing state ---------------------------------------------------------
declare -a DISKFILES   # DISKFILES[d] = newline "localpath|dosname" list
declare -a USED        # USED[d]      = bytes consumed on disk d
CUR=1; USED[1]=0
place() { local d=$1 lp=$2 dn=$3; DISKFILES[$d]="${DISKFILES[$d]:-}$lp|$dn"$'\n'; USED[$d]=$(( ${USED[$d]:-0} + $(rup "$(fsize "$lp")") )); }
# place a whole (non-split) file, advancing to a fresh disk if it won't fit
place_whole() {
  local lp=$1 dn=$2 sz; sz=$(rup "$(fsize "$lp")")
  if [ "$sz" -gt "$(( CAP - ${USED[$CUR]:-0} ))" ]; then CUR=$((CUR+1)); USED[$CUR]=0; fi
  place "$CUR" "$lp" "$dn"
}

# disk 1 mandatory: installer, README, the engine PRG, and a reservation for
# INSTALL.INF (generated last, once disk numbers are known).
RES_INF=4096
place 1 "$STAGE/INSTALL.TOS" INSTALL.TOS
place 1 "$README"            README.TXT
place 1 "$STAGE/MENU.REQ"    MENU.REQ
place 1 "$STAGE/$PRG_DOS"    "$PRG_DOS"
USED[1]=$(( USED[1] + RES_INF ))

# manifest (built as we place parts). PRG + README + MENU.REQ are on disk 1.
MAN="$STAGE/manifest.txt"
{
  printf 'TITLE %s\n' "$TITLE"
  printf 'DEST %s\n'  "$DEST"
  printf 'OUT %s\nSRC 1 %s\n' "$PRG_DOS" "$PRG_DOS"
  printf 'OUT README.TXT\nSRC 1 README.TXT\n'
  printf 'OUT MENU.REQ\nSRC 1 MENU.REQ\n'
} > "$MAN"

# ---- split each pack across disks, filling greedily ------------------------
for pak in "${PACKS[@]}"; do
  src="$BUILD/$pak"
  total=$(fsize "$src")
  base="$(echo "$pak" | tr -d '.' | cut -c1-8 | tr '[:lower:]' '[:upper:]')"   # 8.3 part base
  printf 'OUT %s\n' "$pak" >> "$MAN"
  off=0; idx=1
  while [ "$off" -lt "$total" ]; do
    avail=$(( CAP - ${USED[$CUR]:-0} ))
    if [ "$avail" -lt "$MINFILL" ]; then CUR=$((CUR+1)); USED[$CUR]=0; avail=$CAP; fi
    remain=$(( total - off ))
    take=$avail; [ "$take" -le "$remain" ] || take=$remain
    if [ "$take" -lt "$remain" ]; then take=$(( (take/512)*512 )); fi   # keep mid-parts cluster-aligned
    part="$(printf '%s.%03d' "$base" "$idx")"
    dd if="$src" of="$STAGE/$part" bs=1M iflag=skip_bytes,count_bytes skip="$off" count="$take" status=none
    place "$CUR" "$STAGE/$part" "$part"
    printf 'SRC %d %s\n' "$CUR" "$part" >> "$MAN"
    off=$(( off + take )); idx=$((idx+1))
  done
done

# extra original data files copied verbatim (small enough to place whole)
for a in "${AUX[@]}"; do
  cp "$REFDATA/$a" "$STAGE/$a"
  place_whole "$STAGE/$a" "$a"
  printf 'OUT %s\nSRC %d %s\n' "$a" "$CUR" "$a" >> "$MAN"
done

# SAVE.000: the PRISTINE new-game save (ScummVM layout, synthesized by tools/mksave from the
# player's own LZOBJBLK + LZDNGBLK). The engine patches it in place when the player S)aves
# (-> SAVED.000, which Journey Onward resumes); without it there is no saving at all.
cp "$BUILD/SAVE.000" "$STAGE/SAVE.000"
place_whole "$STAGE/SAVE.000" SAVE.000
printf 'OUT SAVE.000\nSRC %d SAVE.000\n' "$CUR" >> "$MAN"

# MIDITEST.PRG (platform/miditest.c, `make miditest`): stand-alone MIDI port/cable check —
# tiny, ships whole when built, installed next to the game.
if [ -f "$BUILD/MIDITEST.PRG" ]; then
  cp "$BUILD/MIDITEST.PRG" "$STAGE/MIDITEST.PRG"
  place_whole "$STAGE/MIDITEST.PRG" MIDITEST.PRG
  printf 'OUT MIDITEST.PRG\nSRC %d MIDITEST.PRG\n' "$CUR" >> "$MAN"
fi

printf 'END\n' >> "$MAN"
NDISKS=$CUR

# finalize INSTALL.INF (fits inside the reservation) and put it on disk 1
cp "$MAN" "$STAGE/INSTALL.INF"
infsz=$(fsize "$STAGE/INSTALL.INF")
[ "$infsz" -le "$RES_INF" ] || { echo "!! INSTALL.INF ($infsz B) exceeds reservation" >&2; exit 5; }
DISKFILES[1]="${DISKFILES[1]}$STAGE/INSTALL.INF|INSTALL.INF"$'\n'

# ---- integrity check: reassembling the parts must reproduce each pack -------
echo ">> verifying split integrity (host-side reassembly == original pack)"
for pak in "${PACKS[@]}"; do
  base="$(echo "$pak" | tr -d '.' | cut -c1-8 | tr '[:lower:]' '[:upper:]')"
  cat "$STAGE/$base".[0-9][0-9][0-9] > "$STAGE/reassembled.tmp"
  if ! cmp -s "$STAGE/reassembled.tmp" "$BUILD/$pak"; then
    echo "!! reassembly mismatch for $pak — aborting" >&2; exit 6
  fi
  rm -f "$STAGE/reassembled.tmp"
done
echo "   ok: all packs reassemble byte-exact"

# ---- write the .ST images --------------------------------------------------
echo ">> writing $NDISKS disk image(s) ($SIZE KB each) to $OUT"
declare -a IMAGES
for d in $(seq 1 "$NDISKS"); do
  img="$OUT/${NAME}-${TARGET}-disk${d}.st"
  vol=$VOLENG; [ "$d" = 1 ] || vol="U6DATA$d"
  dd if=/dev/zero of="$img" bs=512 count="$SECTORS" status=none
  mformat -i "$img" -v "$vol" ::
  # mcopy every file assigned to this disk (basenames already carry DOS names)
  while IFS='|' read -r lp dn; do
    [ -n "$lp" ] || continue
    mcopy -i "$img" "$lp" "::/$dn"
  done <<< "${DISKFILES[$d]:-}"
  IMAGES+=("$img")
  used=${USED[$d]:-0}
  printf '   disk %d: %-40s %6d KB used\n' "$d" "$(basename "$img")" "$(( used/1024 ))"
done

# ---- optional: copy to the media (e.g. Gotek SD) ---------------------------
if [ -n "$MEDIA" ]; then
  if [ -d "$MEDIA" ]; then
    echo ">> copying images to $MEDIA"
    cp -v "${IMAGES[@]}" "$MEDIA"/
    sync
  else
    echo "!! --media dir not found: $MEDIA (images left in $OUT)" >&2
  fi
fi

echo ">> done: $NDISKS disk(s) for $TARGET in $OUT"
echo "   contents of disk 1:"; mdir -i "${IMAGES[0]}" ::/ | sed 's/^/     /'
