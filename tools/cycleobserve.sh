#!/bin/sh
# cycleobserve.sh — swap a given save into ScummVM slot 0, capture several gameplay
# frames over time, restore the original. Used to OBSERVE colour cycling in a chosen
# scene (e.g. open water) by diffing frames. Always restores the user's save (trap).
# Usage: cycleobserve.sh <save.000> <out-prefix>   (writes <prefix>0..5.png)
set -e
SAVE="${1:-build/ocean.000}"
OUT="${2:-build/cw_f}"
SLOT="$HOME/.local/share/scummvm/saves/ultima6.000"
BAK="$SLOT.cycobs-bak"
DATA="refdata/dos/u6"
DISP=:99
[ -f "$SAVE" ] || { echo "no $SAVE"; exit 2; }
[ -f "$SLOT" ] || { echo "no slot"; exit 2; }
restore(){ [ -f "$BAK" ] && mv -f "$BAK" "$SLOT"; pkill -f "Xvfb $DISP" 2>/dev/null || true; }
trap restore EXIT INT TERM
cp -f "$SLOT" "$BAK"; cp -f "$SAVE" "$SLOT"
pkill -f "Xvfb $DISP" 2>/dev/null || true; sleep 1
Xvfb $DISP -screen 0 1280x960x24 >/tmp/u6_xvfb.log 2>&1 &
sleep 2
export DISPLAY=$DISP
scummvm -p "$DATA" --no-fullscreen --no-aspect-ratio ultima6 >/tmp/u6_svm.log 2>&1 &
SVM=$!
sleep 9
i=0; while [ $i -lt 6 ]; do xdotool key --clearmodifiers Escape; sleep 1.2; i=$((i+1)); done
sleep 1
xdotool mousemove 645 645 click 1     # Journey Onward -> load slot 0 (our swapped save)
sleep 4
i=0; while [ $i -lt 6 ]; do
  import -window root /tmp/cw.png
  convert /tmp/cw.png -trim +repage "${OUT}$i.png" 2>/dev/null || cp /tmp/cw.png "${OUT}$i.png"
  i=$((i+1)); sleep 0.5
done
kill $SVM 2>/dev/null || true
echo "wrote ${OUT}0..5.png"
