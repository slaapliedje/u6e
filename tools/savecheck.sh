#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# savecheck.sh — prove a save we WROTE (tools/nuviewrite) is ScummVM-loadable.
# Temporarily swaps our written save into ScummVM's slot 0, loads it headless,
# screenshots, and ALWAYS restores the user's original save (trap on exit).
#
# Usage: tools/savecheck.sh <written.000> [out.png]
set -e
WRITTEN="${1:-build/ultima6_moved.000}"
OUT="${2:-build/svm_loaded_moved.png}"
SLOT="$HOME/.local/share/scummvm/saves/ultima6.000"
BAK="$SLOT.savecheck-bak"
DATA="refdata/dos/u6"
DISP=:99

[ -f "$WRITTEN" ] || { echo "no $WRITTEN"; exit 2; }
[ -f "$SLOT" ]    || { echo "no ScummVM slot $SLOT"; exit 2; }

restore(){ [ -f "$BAK" ] && mv -f "$BAK" "$SLOT" && echo "restored original save"; \
           pkill -f "Xvfb $DISP" 2>/dev/null || true; }
trap restore EXIT INT TERM

cp -f "$SLOT" "$BAK"            # back up the user's real save
cp -f "$WRITTEN" "$SLOT"       # swap in the save we wrote
echo "swapped our written save into slot 0"

pkill -f "Xvfb $DISP" 2>/dev/null || true; sleep 1
export SDL_VIDEODRIVER=x11; unset WAYLAND_DISPLAY       # render on the Xvfb, not a Wayland session
Xvfb $DISP -screen 0 1280x960x24 >/tmp/u6_xvfb.log 2>&1 &
sleep 2
export DISPLAY=$DISP
# --save-slot=0 pins Nuvie's "Journey Onward" to slot 0 (it otherwise loads the config's
# latest_save slot, which need not be 0 — that silently voided this check for a while).
scummvm -p "$DATA" --no-fullscreen --no-aspect-ratio --save-slot=0 ultima6 >/tmp/u6_svm.log 2>&1 &
SVM=$!
sleep 9
i=0; while [ $i -lt 6 ]; do xdotool key --clearmodifiers Escape; sleep 1.2; i=$((i+1)); done
sleep 1
xdotool mousemove 645 645 click 1     # "Journey Onward" -> loads slot 0 (our save)
sleep 4
import -window root /tmp/u6_loaded.png
convert /tmp/u6_loaded.png -trim +repage "$OUT" 2>/dev/null || cp /tmp/u6_loaded.png "$OUT"
kill $SVM 2>/dev/null || true
echo "wrote $OUT"
