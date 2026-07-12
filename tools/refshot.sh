#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# refshot.sh — capture Ultima VI REFERENCE screenshots from ScummVM (its Ultima
# engine is Nuvie-based) for visually debugging our remake. Clean-room-OK: we are
# OBSERVING the reference running on the user's own data, not copying it.
#
# Runs fully headless under Xvfb on display :99 — never touches the :0 desktop.
# Requires: scummvm (with U6 detected), Xvfb, xdotool, ImageMagick (import/convert).
# Output is 640x400 = exactly 2x U6's native 320x200, so downscale 2x to overlay
# on our captures (or upscale ours 2x).
#
# Usage: tools/refshot.sh [u6-data-dir] [out-dir]
set -e
DATA="${1:-refdata/dos/u6}"
OUT="${2:-refshots}"
DISP=:99
mkdir -p "$OUT"

pkill -f "Xvfb $DISP" 2>/dev/null || true
sleep 1
Xvfb $DISP -screen 0 1280x960x24 >/tmp/u6_xvfb.log 2>&1 &
XVFB=$!
sleep 2

export DISPLAY=$DISP
scummvm -p "$DATA" --no-fullscreen --no-aspect-ratio ultima6 >/tmp/u6_svm.log 2>&1 &
SVM=$!
sleep 9

# skip the intro animation to the main menu
i=0; while [ $i -lt 6 ]; do xdotool key --clearmodifiers Escape; sleep 1.2; i=$((i+1)); done
sleep 1
import -window root /tmp/u6_menu.png
convert /tmp/u6_menu.png -trim +repage "$OUT/u6_mainmenu.png" 2>/dev/null || true

# "Journey Onward" (loads ultima6.000) -> gameplay. Menu item is bottom of the
# card; in the 1280x960 root it sits at ~(645,645).
xdotool mousemove 645 645 click 1
sleep 4                                         # (Escape here would open ScummVM's
                                                #  global menu — U6's own prompt is fine)
import -window root /tmp/u6_play.png
convert /tmp/u6_play.png -trim +repage "$OUT/u6_gameplay.png" 2>/dev/null
convert "$OUT/u6_gameplay.png" -resize 320x200 "$OUT/u6_gameplay_1x.png" 2>/dev/null || true

echo "wrote $OUT/u6_gameplay.png (640x400), u6_gameplay_1x.png (320x200), u6_mainmenu.png"

kill $SVM 2>/dev/null || true
kill $XVFB 2>/dev/null || true
