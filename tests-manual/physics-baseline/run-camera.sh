#!/bin/zsh
# Camera collision for the third-person camera (camera-collision.js), serverless on world.json's ground:
#   tests-manual/physics-baseline/run-camera.sh [path/to/Darkride.app] [results dir]
# One walled-off client (harness.sh's client: its own settings and app data in <results>/camera-home, no sign-in, the
# machine lock, the small corner window, the TEST title). Prints the CAMERA-COLLISION lines; the samples are in
# <results>/camera-collision.json, the pictures (the wall case with the option on and off) in <results>/camera-home/snaps.
#
# Copyright 2026 Darkride.
#
# Distributed under the MIT License.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${${1:-"$D/../../build/interface/Darkride.app"}:A}   # absolute: open -a takes no relative path
R=${2:-"${TMPDIR:-/tmp}/camera-collision"}
BIN="$APP/Contents/MacOS/Darkride"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
mkdir -p "$R"
echo "client: $BIN"
echo "results: $R"
. "${HARNESS:-$D/harness.sh}"   # HARNESS: another harness.sh, say a branch's not yet in main
client camera --url "file://$D/world.json" --testScript "$D/camera-collision.js" quitWhenFinished --testResultsLocation "$R" || true
grep -a "CAMERA-COLLISION" "$R/camera.log" | sed 's/.*CAMERA-COLLISION/CAMERA-COLLISION/' || { echo "no summary in $R/camera.log" >&2; exit 1; }
ls "$R"/camera-home/snaps 2>/dev/null
