#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# animobserve.sh — capture several frames of ScummVM U6 gameplay over time and diff
# consecutive frames to find which tiles ANIMATE (clean-room ground truth for our
# tile-animation work). Loads the user's current save (throne room has fire braziers).
# Output: build/anim_f0..f5.png + build/anim_diff_*.png (changed pixels highlighted).
set -e
DATA="refdata/dos/u6"
OUT="build"
DISP=:99
mkdir -p "$OUT"
pkill -f "Xvfb $DISP" 2>/dev/null || true; sleep 1
Xvfb $DISP -screen 0 1280x960x24 >/tmp/u6_xvfb.log 2>&1 &
sleep 2
export DISPLAY=$DISP
scummvm -p "$DATA" --no-fullscreen --no-aspect-ratio ultima6 >/tmp/u6_svm.log 2>&1 &
SVM=$!
sleep 9
i=0; while [ $i -lt 6 ]; do xdotool key --clearmodifiers Escape; sleep 1.2; i=$((i+1)); done
sleep 1
xdotool mousemove 645 645 click 1     # Journey Onward -> load slot 0
sleep 4
# grab 6 frames ~0.6s apart
i=0; while [ $i -lt 6 ]; do
  import -window root /tmp/af.png
  convert /tmp/af.png -trim +repage "$OUT/anim_f$i.png" 2>/dev/null || cp /tmp/af.png "$OUT/anim_f$i.png"
  i=$((i+1)); sleep 0.6
done
kill $SVM 2>/dev/null || true
pkill -f "Xvfb $DISP" 2>/dev/null || true
# diff consecutive frames: white where pixels changed
i=1; while [ $i -lt 6 ]; do j=$((i-1));
  compare -metric AE "$OUT/anim_f$j.png" "$OUT/anim_f$i.png" -highlight-color white -lowlight-color black "$OUT/anim_diff_$j$i.png" 2>>/tmp/anim_ae.txt || true
  i=$((i+1)); done
echo "AE (changed pixel counts) per consecutive pair:"; cat /tmp/anim_ae.txt 2>/dev/null; echo
echo "wrote $OUT/anim_f0..5.png + $OUT/anim_diff_*.png"
