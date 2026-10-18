#!/bin/zsh
# PHYSICS.md round 7: the airship over the terrain, flown through a scripted course (airship.js, airship-rig.js).
#   tests-manual/physics-baseline/run-airship.sh [path/to/Darkride.app] [results dir] [udp port|serverless] [loss] [delay_ms] [car]
# With "car" as the sixth argument the ship carries a track along its deck with a trolley on it (round 8, nesting): the
# crew rides the trolley, on and off at its stations, instead of walking the deck.
# make-terrain.py writes the terrain file into the results dir, which is served over HTTP on <port>+78 (the physics server
# and the clients fetch it from there); landrover.js (the terrain), airship-rig.js and airship.js are copied beside it
# with a one-line wrapper per role that names the role and the terrain URL. Serverless: one client, the pilot, in
# world.json (the fallback path). On a domain (the physics sandbox, restarted here from the client's build with the
# earlier runs' test entities taken out of its world; the physics server among its services): the crew first, then the
# pilot after 12 s, the pilot under TIVOLI_PACKET_LOSS=<loss> and TIVOLI_PACKET_DELAY_MS=<delay> when given (0 and 0 by
# default). The summaries (PHYSICS-AIRSHIP lines) are printed; the
# samples land in <results dir>/pilot/physics-airship.json and <results dir>/crew/physics-airship.json, and the sandbox's
# assignment-client log of the run (the physics server's lines are in it under [physics-server]) is copied beside them.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/Darkride.app"}
R=${2:-"${TMPDIR:-/tmp}/physics-airship"}
PORT=${3:-42102}
LOSS=${4:-0}
DELAY=${5:-0}
CAR=${6:-}
DECK_CAR=false
[ "$CAR" = "car" ] && DECK_CAR=true
SANDBOX=${SANDBOX:-$HOME/tivoli-sandboxes/physics}
DOMAIN_SANDBOX=${DOMAIN_SANDBOX:-$HOME/git/tivolicloud/domain-sandbox.sh}   # darkride-ops' sandbox starter
BIN="$APP/Contents/MacOS/Darkride"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
mkdir -p "$R/pilot" "$R/crew"
export HIFI_METAVERSE_URL=${HIFI_METAVERSE_URL:-https://vr.darkride.ai}
SIZE=192; HEIGHT=32; VOXEL=0.5
HTTP_PORT=42180
[ "$PORT" != "serverless" ] && HTTP_PORT=$((PORT + 78))
TERRAIN_URL="http://127.0.0.1:$HTTP_PORT/rover.drkt"

[ -f "$R/rover.drkt" ] || python3 "$D/make-terrain.py" "$R/rover.drkt" "$SIZE" "$HEIGHT" "$VOXEL"
for ROLE in pilot crew; do
    { echo "var AIRSHIP_CONFIG = { role: \"$ROLE\", terrainURL: \"$TERRAIN_URL\", deckCar: $DECK_CAR };"; cat "$D/landrover.js" "$D/airship-rig.js" "$D/airship.js"; } > "$R/airship-$ROLE.js"
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
    "$BIN" --allowMultipleInstances --url "file://$D/world.json" --testScript "$R/airship-pilot.js" --testResultsLocation "$R/pilot" \
        > "$R/pilot.log" 2>&1 || true
    grep "PHYSICS-AIRSHIP" "$R/pilot.log" || echo "no pilot summary in $R/pilot.log" >&2
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
                     if not (str(e.get("name", "")).startswith("airship-") or e.get("name") == "rover-terrain")]
with gzip.open(path, "wt") as f:
    json.dump(world, f)
print("world cleaned of earlier runs' test entities:", before - len(world["Entities"]), "removed,", len(world["Entities"]), "kept")
PY
# the physics server is off by default (describe-settings.json); this sandbox runs it
python3 -c 'import json, os, sys; p = sys.argv[1]; c = json.load(open(p)) if os.path.exists(p) else {}; c.setdefault("physics_server", {})["enabled"] = True; json.dump(c, open(p, "w"), indent=2)' "$SANDBOX/domain-config.json"
"$DOMAIN_SANDBOX" physics "$BUILD" "$PORT"
# the sandbox's monitor starts six children for seven services and adds the seventh, the physics server, at its next
# spare check (30 s)
for i in $(seq 1 90); do
    curl -s "http://localhost:$((PORT - 2))/nodes.json" 2>/dev/null | grep -q '"physics-server"' && break
    sleep 1
done
echo "physics-server in the domain after ${i}s"

URL="hifi://localhost:$PORT"
echo "domain: $URL; the pilot under packet loss $LOSS and a delay of $DELAY ms"
"$BIN" --allowMultipleInstances --url "$URL" --testScript "$R/airship-crew.js" --testResultsLocation "$R/crew" \
    > "$R/crew.log" 2>&1 &
CREW=$!
sleep 12
TIVOLI_PACKET_LOSS="$LOSS" TIVOLI_PACKET_DELAY_MS="$DELAY" "$BIN" --allowMultipleInstances --url "$URL" --testScript "$R/airship-pilot.js" \
    --testResultsLocation "$R/pilot" > "$R/pilot.log" 2>&1 || true
wait $CREW || true
echo "== pilot"
grep "PHYSICS-AIRSHIP" "$R/pilot.log" || echo "no pilot summary in $R/pilot.log" >&2
echo "== crew"
grep "PHYSICS-AIRSHIP" "$R/crew.log" || echo "no crew summary in $R/crew.log" >&2
cp "$SANDBOX/logs/assignment-client.log" "$R/sandbox-assignment-client.log" 2>/dev/null || true
echo "== physics server"
grep "\[physics-server\]" "$R/sandbox-assignment-client.log" 2>/dev/null | grep -i "airship\|vehicle\|teleport\|terrain\|error\|crash\|fatal" | tail -30
