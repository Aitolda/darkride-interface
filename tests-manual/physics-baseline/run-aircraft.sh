#!/bin/zsh
# PHYSICS.md round 8: the tilt-thrust aircraft over the terrain, flown through a scripted course by airship.js in its
# aircraft form (aircraft-rig.js): the pilot alone (there is no deck to walk).
#   tests-manual/physics-baseline/run-aircraft.sh [path/to/interface.app] [results dir] [udp port|serverless] [loss] [delay_ms]
# Serverless: one client in world.json. On a domain (the physics sandbox, restarted here from the client's build with
# the earlier runs' test entities taken out of its world): the pilot under TIVOLI_PACKET_LOSS and TIVOLI_PACKET_DELAY_MS
# when given. The summary (PHYSICS-AIRCRAFT lines) is printed; the samples land in <results dir>/pilot/physics-airship.json.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/interface.app"}
R=${2:-"${TMPDIR:-/tmp}/physics-aircraft"}
PORT=${3:-42102}
LOSS=${4:-0}
DELAY=${5:-0}
SANDBOX=/Users/thistlequell/tivoli-sandboxes/physics
BIN="$APP/Contents/MacOS/interface"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
mkdir -p "$R/pilot"
export HIFI_METAVERSE_URL=${HIFI_METAVERSE_URL:-https://vr.darkride.ai}
SIZE=192; HEIGHT=32; VOXEL=0.5
HTTP_PORT=42180
[ "$PORT" != "serverless" ] && HTTP_PORT=$((PORT + 78))
TERRAIN_URL="http://127.0.0.1:$HTTP_PORT/rover.drkt"

[ -f "$R/rover.drkt" ] || python3 "$D/make-terrain.py" "$R/rover.drkt" "$SIZE" "$HEIGHT" "$VOXEL"
for ROLE in pilot; do
    { echo "var AIRSHIP_CONFIG = { role: \"$ROLE\", vehicle: \"aircraft\", terrainURL: \"$TERRAIN_URL\" };"; cat "$D/landrover.js" "$D/airship-rig.js" "$D/aircraft-rig.js" "$D/airship.js"; } > "$R/aircraft-$ROLE.js"
done
if ! lsof -nP -iTCP:$HTTP_PORT -sTCP:LISTEN > /dev/null 2>&1; then
    (cd "$R" && python3 -m http.server "$HTTP_PORT" --bind 127.0.0.1 > "$R/http.log" 2>&1) &
    HTTP_PID=$!
    trap 'kill $HTTP_PID 2>/dev/null || true' EXIT
    sleep 1
fi

echo "client: $BIN"
echo "terrain: $TERRAIN_URL"
echo "results: $R"
if [ "$PORT" = "serverless" ]; then
    echo "serverless: the pilot alone in world.json"
    "$BIN" --allowMultipleInstances --url "file://$D/world.json" --testScript "$R/aircraft-pilot.js" --testResultsLocation "$R/pilot" \
        > "$R/pilot.log" 2>&1 || true
    grep "PHYSICS-AIRCRAFT" "$R/pilot.log" || echo "no pilot summary in $R/pilot.log" >&2
    exit 0
fi

# an earlier run's terrain (lifetime 600 s) would still be in the world beside this run's: stop the sandbox, take the
# earlier runs' rover-terrain and airship-* entities out of its world file, then start it from the client's build
BUILD=$(cd "$APP/../.." && pwd)
for pidfile in "$SANDBOX"/*.pid(N); do
    pid=$(cat "$pidfile"); kill "$pid" 2>/dev/null || true; rm -f "$pidfile"
done
sleep 3
python3 - "$SANDBOX/entities/models.json.gz" <<'PY'
import gzip, json, sys
path = sys.argv[1]
try:
    world = json.load(gzip.open(path))
except Exception as e:
    print("no world file to clean:", e); sys.exit(0)
before = len(world.get("Entities", []))
world["Entities"] = [e for e in world.get("Entities", [])
                     if not (str(e.get("name", "")).startswith("airship-") or str(e.get("name", "")).startswith("aircraft-") or e.get("name") == "rover-terrain")]
with gzip.open(path, "wt") as f:
    json.dump(world, f)
print("world cleaned of earlier runs' test entities:", before - len(world["Entities"]), "removed,", len(world["Entities"]), "kept")
PY
# the physics server is off by default (describe-settings.json); this sandbox runs it
python3 -c 'import json, os, sys; p = sys.argv[1]; c = json.load(open(p)) if os.path.exists(p) else {}; c.setdefault("physics_server", {})["enabled"] = True; json.dump(c, open(p, "w"), indent=2)' "/Users/thistlequell/tivoli-sandboxes/physics/domain-config.json"
/Users/thistlequell/git/tivolicloud/domain-sandbox.sh physics "$BUILD" "$PORT"
# the sandbox's monitor starts six children for seven services and adds the seventh, the physics server, at its next
# spare check (30 s)
for i in $(seq 1 90); do
    curl -s "http://localhost:$((PORT - 2))/nodes.json" 2>/dev/null | grep -q '"physics-server"' && break
    sleep 1
done
echo "physics-server in the domain after ${i}s"

URL="hifi://localhost:$PORT"
echo "domain: $URL; the pilot under packet loss $LOSS and a delay of $DELAY ms"
TIVOLI_PACKET_LOSS="$LOSS" TIVOLI_PACKET_DELAY_MS="$DELAY" "$BIN" --allowMultipleInstances --url "$URL" --testScript "$R/aircraft-pilot.js" \
    --testResultsLocation "$R/pilot" > "$R/pilot.log" 2>&1 || true
echo "== pilot"
grep "PHYSICS-AIRCRAFT" "$R/pilot.log" || echo "no pilot summary in $R/pilot.log" >&2
cp "$SANDBOX/logs/assignment-client.log" "$R/sandbox-assignment-client.log" 2>/dev/null || true
echo "== physics server"
grep "\[physics-server\]" "$R/sandbox-assignment-client.log" 2>/dev/null | grep -i "aircraft\|vehicle\|teleport\|terrain\|error\|crash\|fatal" | tail -30
