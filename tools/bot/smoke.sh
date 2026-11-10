#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# smoke.sh [baseline]: the bot's quick check after a change (docs/design/bot_harness.md §5b).
#   U6: castle + bridge on a fresh drive; the run is deterministic, so run.log (times cut) and
#       transcripts.json must match the baseline taken at a known-good commit (smoke.sh baseline).
#   SE: se.py smoke (walk, look, talk, save/restore).
# Needs: make game-falcon convkw build/mksave world-se. Output in build/bot-smoke/.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); B="$ROOT/build"; O="$B/bot-smoke"; BASE="$B/bot-baseline"
HD="$O/u6hd"
rm -rf "${O:?}"; mkdir -p "$HD"
cp "$B/gamef.prg" "$HD/GAMEF.PRG"; cp "$ROOT"/refdata/dos/u6/CONVERSE.? "$ROOT"/refdata/dos/u6/PORTRAIT.? "$HD/"
cp "$B/U6.PAK" "$B/U6FE.PAK" "$HD/"; "$B/mksave" "$ROOT/refdata/dos/u6" "$HD/SAVE.000" >/dev/null; echo 3 > "$HD/MIDI.CFG"
python3 "$ROOT/tools/bot/run.py" "$HD" "$O/u6" castle bridge > "$O/u6.out" 2>&1; u6rc=$?
cut -d' ' -f2- "$O/u6/run.log" | grep -v '^run [0-9]*s' | sed 's/ done in [0-9]*s:/ done:/' > "$O/u6/run.cut"
if [ "${1:-}" = baseline ]; then
    rm -rf "${BASE:?}"; mkdir -p "$BASE"; cp "$O/u6/run.cut" "$O/u6/transcripts.json" "$BASE/"
    (cd "$ROOT" && git rev-parse --short HEAD) > "$BASE/commit"; echo "baseline taken at $(cat "$BASE/commit")"; exit 0
fi
fail=0
if [ ! -d "$BASE" ]; then echo "U6: no baseline (run smoke.sh baseline at a good commit)"; fail=1
elif [ $u6rc -ne 0 ]; then echo "U6: run failed (rc $u6rc), see $O/u6.out"; fail=1
elif cmp -s "$BASE/run.cut" "$O/u6/run.cut" && cmp -s "$BASE/transcripts.json" "$O/u6/transcripts.json"; then
    echo "U6: same as the baseline ($(cat "$BASE/commit"))"
else echo "U6: DIFFERS from the baseline ($(cat "$BASE/commit")):"; diff "$BASE/run.cut" "$O/u6/run.cut" | head -20; fail=1; fi
BOT_SE_HD="$O/sehd" python3 "$ROOT/tools/bot/se.py" "$O/se" smoke > "$O/se.out" 2>&1 && echo "SE: smoke clean" || {
    echo "SE: smoke issues:"; grep ISSUE "$O/se.out"; fail=1; }
exit $fail
