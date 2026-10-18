#!/bin/zsh
# PHYSICS.md round 6: the Land Rover on the terrain, driven over a scripted course (rover.js, landrover.js).
#   tests-manual/physics-baseline/run-rover.sh [path/to/interface.app] [results dir] [udp port|serverless] [loss] [delay_ms]
# make-terrain.py writes the terrain file into the results dir, which is served over HTTP on <port>+78 (the physics server
# and the clients fetch it from there); rover.js and landrover.js are copied beside it with the two one-line wrapper
# scripts that name the role and the terrain URL. Serverless: one client, the driver, in world.json (the fallback path).
# On a domain (a sandbox started from this build with domain-sandbox.sh, the physics server among its services): the
# passenger first, then the driver after 12 s, the driver under TIVOLI_PACKET_LOSS=<loss> and TIVOLI_PACKET_DELAY_MS=<delay>
# when given (0 and 0 by default). The summaries (PHYSICS-ROVER lines) are printed; the samples land in
# <results dir>/driver/physics-rover.json and <results dir>/passenger/physics-rover.json, and the sandbox's assignment-client
# log of the run (the physics server's lines are in it under [physics-server]) is copied beside them.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/interface.app"}
R=${2:-"${TMPDIR:-/tmp}/physics-rover"}
PORT=${3:-42102}
LOSS=${4:-0}
DELAY=${5:-0}
SANDBOX=${SANDBOX:-$HOME/tivoli-sandboxes/physics}
BIN="$APP/Contents/MacOS/interface"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
mkdir -p "$R/driver" "$R/passenger"
export HIFI_METAVERSE_URL=${HIFI_METAVERSE_URL:-https://vr.darkride.ai}
SIZE=192; HEIGHT=32; VOXEL=0.5
HTTP_PORT=42180
[ "$PORT" != "serverless" ] && HTTP_PORT=$((PORT + 78))
TERRAIN_URL="http://127.0.0.1:$HTTP_PORT/rover.drkt"

[ -f "$R/rover.drkt" ] || python3 "$D/make-terrain.py" "$R/rover.drkt" "$SIZE" "$HEIGHT" "$VOXEL"
# one self-contained script per role: the config, the Land Rover rig, the test
for ROLE in driver passenger; do
    { echo "var ROVER_CONFIG = { role: \"$ROLE\", terrainURL: \"$TERRAIN_URL\" };"; cat "$D/landrover.js" "$D/rover.js"; } > "$R/rover-$ROLE.js"
done
(cd "$R" && python3 -m http.server "$HTTP_PORT" --bind 127.0.0.1 > "$R/http.log" 2>&1) &
HTTP_PID=$!
trap 'kill $HTTP_PID 2>/dev/null || true' EXIT
sleep 1

# each client walled off in its own folder under the results (its settings, app data and caches): no client touches the
# user's own settings, and two clients never share an identity (two on one folder were taken for one, 10-18)
wall() {
    mkdir -p "$R/$1-home/config/Tivoli Cloud VR - dev" "$R/$1-home/data" "$R/$1-home/snaps"
    [ -f "$R/$1-home/config/Tivoli Cloud VR - dev/Interface.json" ] ||
        echo "{\"snapshotsLocation\": \"$R/$1-home/snaps\"}" > "$R/$1-home/config/Tivoli Cloud VR - dev/Interface.json"
}
wall driver
wall passenger
echo "client: $BIN"
echo "terrain: $TERRAIN_URL ($SIZE m, $HEIGHT m tall, $VOXEL m voxels)"
echo "results: $R"
if [ "$PORT" = "serverless" ]; then
    echo "serverless: the driver alone in world.json"
    XDG_CONFIG_HOME="$R/driver-home/config" TIVOLI_APP_DATA_DIR="$R/driver-home/data" \
        "$BIN" --allowMultipleInstances --url "file://$D/world.json" --testScript "$R/rover-driver.js" --testResultsLocation "$R/driver" \
        > "$R/driver.log" 2>&1 || true
    grep "PHYSICS-ROVER" "$R/driver.log" || echo "no driver summary in $R/driver.log" >&2
    exit 0
fi

URL="hifi://localhost:$PORT"
echo "domain: $URL; the driver under packet loss $LOSS and a delay of $DELAY ms"
XDG_CONFIG_HOME="$R/passenger-home/config" TIVOLI_APP_DATA_DIR="$R/passenger-home/data" \
    "$BIN" --allowMultipleInstances --url "$URL" --testScript "$R/rover-passenger.js" --testResultsLocation "$R/passenger" \
    > "$R/passenger.log" 2>&1 &
PASSENGER=$!
sleep 12
XDG_CONFIG_HOME="$R/driver-home/config" TIVOLI_APP_DATA_DIR="$R/driver-home/data" \
TIVOLI_PACKET_LOSS="$LOSS" TIVOLI_PACKET_DELAY_MS="$DELAY" "$BIN" --allowMultipleInstances --url "$URL" --testScript "$R/rover-driver.js" \
    --testResultsLocation "$R/driver" > "$R/driver.log" 2>&1 || true
wait $PASSENGER || true
echo "== driver"
grep "PHYSICS-ROVER" "$R/driver.log" || echo "no driver summary in $R/driver.log" >&2
echo "== passenger"
grep "PHYSICS-ROVER" "$R/passenger.log" || echo "no passenger summary in $R/passenger.log" >&2
cp "$SANDBOX/logs/assignment-client.log" "$R/sandbox-assignment-client.log" 2>/dev/null || true
echo "== physics server"
grep "\[physics-server\]" "$R/sandbox-assignment-client.log" 2>/dev/null | grep -i "vehicle\|terrain\|rover\|error\|crash\|fatal" | tail -30
