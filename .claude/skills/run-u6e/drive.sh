#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# drive.sh — headless harness for the u6e Atari engine.
#
# The "app" is a 68000 Atari program that only runs under an emulator. This
# script builds it, boots it on an emulated Atari TT (Hatari, TOS 3.06,
# fast-forward), records the screen to an AVI, and extracts one PNG frame so an
# agent can SEE what the engine drew. It parameterises the two knobs the engine
# exposes for headless capture: the world START POSITION (GX/GY) and the
# scenario SENTINEL files the game looks for on its C: drive (NPCWALK.BIN walks
# NPCs N turns; COMBAT.BIN spawns/sequences a fight).
#
# It drives builds through `make` so edits to engine/ render/ dataformats/ are
# picked up automatically (dependency tracking). The emulator + ffmpeg capture
# is run directly here so position/scenario are not hard-coded the way the
# Makefile's m5/m6-* targets are.
#
# Usage:
#   drive.sh scene [X Y] [-o OUT.png] [--walk N] [--combat 1|2] [--vbls N]
#   drive.sh check                 # headless TEXT self-test (PASS/FAIL), ST + TT
#   drive.sh logic                 # host-side engine/conversation/combat tests (no emulator)
#   drive.sh -h
#
# Examples:
#   drive.sh scene                       # default start (315,367 = Lord British's throne)
#   drive.sh scene 64 32 -o water.png    # open water — shows animated water tiles
#   drive.sh scene 332 434 --walk 74 -o npc.png   # advance 74 turns, NPC mid-commute
#   drive.sh scene --combat 2 -o fight.png
set -u

# repo root = three levels up from .claude/skills/run-u6e/
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SELF/../../.." && pwd)"
cd "$ROOT" || { echo "cannot cd to repo root $ROOT" >&2; exit 1; }

BUILD=build
HATARI=${HATARI:-hatari}
# TT ROM the games shipped against; Makefile default resolves here in this container.
TOS_TT=${TOS_TT:-/usr/share/hatari/TOS v3.06 (1991)(Atari)(GB)[TT].img}

die(){ echo "drive.sh: $*" >&2; exit 1; }

usage(){ cat <<'EOF'
drive.sh — headless harness for the u6e Atari engine (boots game.prg on an
emulated TT under Hatari, captures one PNG frame).

Usage:
  drive.sh scene [X Y] [-o OUT.png] [--walk N] [--combat 1|2] [--vbls N]
  drive.sh check                 # headless TEXT self-test (PASS/FAIL), ST + TT
  drive.sh logic                 # host-side engine/conversation/combat tests (no emulator)
  drive.sh -h

Examples:
  drive.sh scene                       # default start (315,367 = Lord British's throne)
  drive.sh scene 64 32 -o water.png    # open water — shows animated water tiles
  drive.sh scene 332 434 --walk 74 -o npc.png   # advance 74 turns, NPC mid-commute
  drive.sh scene --combat 2 -o fight.png
EOF
}

# --- capture one PNG frame from a headless TT run of game.prg -------------------
# args: <out.png> <vbls>
# NOTE: this Hatari build does NOT reliably self-terminate at --run-vbls (it keeps
# recording, so the process must be hard-killed). We bound each boot with
# `timeout -s KILL` (SIGTERM is ignored) + a short retry, then extract the LAST
# decoded AVI frame (the held UI). SDL_RENDER_DRIVER=software dodges GLX flakiness.
capture(){
  local out="$1" vbls="$2" wall
  # wall-clock ceiling for the boot: fast-forward covers ~2500 vbls in well under 45s.
  wall=$(( vbls/60 + 30 )); [ "$wall" -gt 55 ] && wall=55
  rm -f "$out"
  echo ">> booting game.prg on emulated TT (${vbls} VBLs, fast-forward, <=${wall}s)..."
  for try in 1 2 3; do
    pkill -9 hatari 2>/dev/null; sleep 1; rm -f "$BUILD/game.avi"
    timeout -s KILL "$wall" env SDL_RENDER_DRIVER=software "$HATARI" \
            --machine tt --ttram 4 --addr24 off --monitor vga \
            --tos "$TOS_TT" --harddrive "$BUILD" --auto 'c:\game.prg' \
            --fast-boot on --fast-forward on --run-vbls "$vbls" \
            --avirecord --avi-vcodec png --avi-file "$BUILD/game.avi" \
            </dev/null >/dev/null 2>&1
    pkill -9 hatari 2>/dev/null
    [ -s "$BUILD/game.avi" ] && break
    echo ">> attempt $try produced no AVI — retrying"
  done
  [ -s "$BUILD/game.avi" ] || die "no AVI produced — emulator failed to boot (check TOS_TT path)"
  # AVI is force-quit (no index); -update keeps the LAST decoded frame = held UI.
  ffmpeg -y -i "$BUILD/game.avi" -update 1 "$out" >/dev/null 2>&1 \
    || die "ffmpeg could not extract a frame from $BUILD/game.avi"
  [ -s "$out" ] || die "no frame written to $out"
  rm -f "$BUILD/game.avi"
  echo ">> wrote $out"
}

cmd_scene(){
  local gx=315 gy=367 out="$BUILD/scene.png" vbls=5000 walk="" combat="" menu="" intro="" chargen=""
  # optional leading "X Y"
  if [[ "${1:-}" =~ ^[0-9]+$ && "${2:-}" =~ ^[0-9]+$ ]]; then gx="$1"; gy="$2"; shift 2; fi
  while [ $# -gt 0 ]; do
    case "$1" in
      -o|--out)   out="$2"; shift 2;;
      --walk)     walk="$2"; shift 2;;
      --combat)   combat="$2"; shift 2;;
      --cast)      combat="16"; shift 1;;  # MAGIC demo (COMBAT.BIN mode 16: Light/Heal/Fireball)
      --cast-menu) combat="17"; shift 1;;  # MAGIC UI (COMBAT.BIN mode 17: the C)ast spell menu)
      --cast-more) combat="18"; shift 1;;  # MAGIC (COMBAT.BIN mode 18: Create Food + Unlock Magic)
      --cast-field) combat="19"; shift 1;; # MAGIC (COMBAT.BIN mode 19: Fire/Energy/Dispel field layer)
      --cast-status) combat="20"; shift 1;;# MAGIC (COMBAT.BIN mode 20: Poison/Charm/Paralyze/Protection)
      --cast-summon) combat="21"; shift 1;;# MAGIC (COMBAT.BIN mode 21: Conjure/Summon/Clone + allied AI)
      --cast-travel) combat="22"; shift 1;;# MAGIC (COMBAT.BIN mode 22: Help + Gate Travel; stages SAVE.000)
      --cast-util) combat="23"; shift 1;;  # MAGIC (COMBAT.BIN mode 23: Locate/Detect Magic/X-ray/Telekinesis)
      --rest) combat="24"; shift 1;;       # PLAY (COMBAT.BIN mode 24: mana display + regen + R)est)
      --menu) menu=1; shift 1;;            # FRONT-END: show the title + main menu (shapes.md)
      --intro) intro=1; shift 1;;          # FRONT-END: play the intro cutscene directly (Phase 2)
      --chargen)  # FRONT-END: character creation (Phase 3). Optional screen: n=name g=gender
                  # p=portrait q=question s=summary N=apply-to-new-game (default 1 = full flow).
                  if [[ "${2:-}" =~ ^[ngpqsNGW1]$ ]]; then chargen="$2"; shift 2; else chargen=1; shift 1; fi;;
      --vbls)     vbls="$2"; shift 2;;
      *) die "unknown scene option: $1";;
    esac
  done

  echo ">> build game.prg (picks up engine/render/dataformats edits)"
  make game >/dev/null || die "game.prg build failed — run 'make game' to see errors"
  echo ">> build world @ ($gx,$gy) + stage CONVERSE/PORTRAIT onto C:"
  make gamedata GX="$gx" GY="$gy" >/dev/null || die "world/gamedata build failed"

  # sentinels: present = headless scenario; absent = static Jaana-join demo.
  # SAVE.000 triggers the auto-walk-and-save path, which moves the camera off
  # ($gx,$gy) — remove it so the capture stays put.
  rm -f "$BUILD/SAVE.000" "$BUILD/NPCWALK.BIN" "$BUILD/COMBAT.BIN" "$BUILD/MENU.REQ" "$BUILD/INTRO.REQ" "$BUILD/CHARGEN.REQ"
  if [ -n "$menu" ];  then printf 1 > "$BUILD/MENU.REQ";  echo ">> staged MENU.REQ (title/main menu)"; fi
  if [ -n "$intro" ]; then printf 1 > "$BUILD/INTRO.REQ"; echo ">> staged INTRO.REQ (intro cutscene)"; fi
  if [ -n "$chargen" ]; then printf %s "$chargen" > "$BUILD/CHARGEN.REQ"; echo ">> staged CHARGEN.REQ=$chargen (character creation)"; fi
  if [ -n "$walk" ];   then printf "$(printf '\\%03o' "$walk")" > "$BUILD/NPCWALK.BIN"; echo ">> staged NPCWALK.BIN = $walk turns"; fi
  if [ -n "$combat" ]; then printf "$(printf '\\%03o' "$combat")" > "$BUILD/COMBAT.BIN"; echo ">> staged COMBAT.BIN mode $combat"; fi
  # Gate Travel (mode 22) reads the moonstone positions from the save template.
  if [ "$combat" = "22" ]; then
    SV="$HOME/.local/share/scummvm/saves/ultima6.000"
    [ -f "$SV" ] && { cp "$SV" "$BUILD/SAVE.000"; echo ">> staged SAVE.000 (moonstone data for Gate Travel)"; } \
                 || echo ">> WARN: no ScummVM save at $SV — Gate Travel will find no moongate"
  fi

  capture "$out" "$vbls"
}

cmd_check(){ echo ">> headless TEXT self-test (ST + TT) — VT-52 console mirrored to stdout"; make check; }
cmd_logic(){ echo ">> host-side logic tests (no emulator)"; make enginetest convtest cvmtest combattest; }

case "${1:-}" in
  scene) shift; cmd_scene "$@";;
  check) cmd_check;;
  logic) cmd_logic;;
  -h|--help|"") usage;;
  *) die "unknown command: $1 (try -h)";;
esac
