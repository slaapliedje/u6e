#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the player's release zip: the programs and CONVERT.TOS, no game data. The player copies their
# DOS files next to them and the game converts them at first start (docs/design/first_boot_convert.md).
# usage: scripts/make-release.sh [version]     -> build/u6e-<version>.zip
set -eu
BUILD=${BUILD:-build}
VER=${1:-$(git rev-parse --short HEAD)}
OUT=$BUILD/release/u6e-$VER
STRIP=${STRIP:-m68k-atari-mint-strip}

rm -rf "${OUT:?}"; mkdir -p "$OUT"
for f in game.prg:GAME.PRG gamest.prg:GAMEST.PRG gamevc.prg:GAMEVC.PRG CONVERT.TOS:CONVERT.TOS; do
    "$STRIP" -o "$OUT/${f#*:}" "$BUILD/${f%:*}"
done
sed 's/$/\r/' LICENSE > "$OUT/LICENSE.TXT"
sed -e "s/@VER@/$VER/" -e 's/$/\r/' scripts/release-readme.txt > "$OUT/README.TXT"

# only what is listed here may go out: the zip is public, the player's data is not
for f in "$OUT"/*; do
    case "${f##*/}" in GAME.PRG|GAMEST.PRG|GAMEVC.PRG|CONVERT.TOS|LICENSE.TXT|README.TXT) ;;
    *) echo "unexpected file in release: $f" >&2; exit 1 ;; esac
done
rm -f "$BUILD/u6e-$VER.zip"
(cd "$OUT" && zip -q -X -9 "../../u6e-$VER.zip" ./*)
ls -l "$BUILD/u6e-$VER.zip"
