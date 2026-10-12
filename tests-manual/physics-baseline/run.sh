#!/bin/sh
# Run the physics baseline (PHYSICS.md round 0) against a built client, serverless.
#   tests-manual/physics-baseline/run.sh [path/to/interface.app] [results dir]
# Prints the PHYSICS-BASELINE summary lines; the samples land in <results dir>/physics-baseline.json.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/interface.app"}
R=${2:-"${TMPDIR:-/tmp}/physics-baseline"}
mkdir -p "$R"
BIN="$APP/Contents/MacOS/interface"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
echo "client: $BIN"
echo "results: $R"
# --allowMultipleInstances: without it a second client hands its --url to the client already running and exits
"$BIN" --allowMultipleInstances --url "file://$D/world.json" --testScript "$D/baseline.js" --testResultsLocation "$R" > "$R/client.log" 2>&1 || true
grep "PHYSICS-BASELINE" "$R/client.log" || { echo "no summary in $R/client.log" >&2; exit 1; }
