#!/bin/sh
# Run the physics baseline with two clients on a domain (PHYSICS.md round 3): one moves the cabin as a frame
# (baseline-frame.js), the other rides it and watches (observer.js). The domain is a sandbox started with
# domain-sandbox.sh from the same build; it must be up first.
#   tests-manual/physics-baseline/run-pair.sh [path/to/Darkride.app] [results dir] [udp port] [frame|track]
# "track" (round 4) runs track.js on the mover: the cabin rides a Track entity and the observer boards at its station.
# Prints the PHYSICS-BASELINE lines of the mover and the PHYSICS-OBSERVER lines of the observer; the samples
# land in <results dir>/mover/physics-baseline.json and <results dir>/observer/physics-observer.json.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/Darkride.app"}
R=${2:-"${TMPDIR:-/tmp}/physics-pair"}
PORT=${3:-42102}
MODE=${4:-frame}
MOVER="$D/baseline-frame.js"
[ "$MODE" = "track" ] && MOVER="$D/track.js"
URL="hifi://localhost:$PORT"
BIN="$APP/Contents/MacOS/Darkride"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
mkdir -p "$R/mover" "$R/observer"
export HIFI_METAVERSE_URL=${HIFI_METAVERSE_URL:-https://vr.darkride.ai}
echo "client: $BIN"
echo "domain: $URL"
echo "results: $R"
# the observer first, so that it is in the domain before the cabin appears (the mover's boot phase is 8 s)
"$BIN" --allowMultipleInstances --url "$URL" --testScript "$D/observer.js" --testResultsLocation "$R/observer" > "$R/observer.log" 2>&1 &
OBSERVER=$!
sleep 12
"$BIN" --allowMultipleInstances --url "$URL" --testScript "$MOVER" --testResultsLocation "$R/mover" > "$R/mover.log" 2>&1 || true
wait $OBSERVER || true
grep "PHYSICS-BASELINE\|PHYSICS-TRACK" "$R/mover.log" || echo "no mover summary in $R/mover.log" >&2
grep "PHYSICS-OBSERVER" "$R/observer.log" || echo "no observer summary in $R/observer.log" >&2
