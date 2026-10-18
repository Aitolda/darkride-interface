#!/bin/zsh
# PHYSICS.md round 5's acceptance test: the monorail with the physics server driving, twenty bot riders aboard and
# twenty bots walking below, two real clients measuring, one of them under packet loss.
#   tests-manual/physics-baseline/run-monorail.sh [path/to/Darkride.app] [results dir] [udp port] [riders] [walkers] [loss]
# The sandbox domain is restarted from the app's build (domain-sandbox.sh, which starts the physics server with the other
# services) with the bots as the domain's persistent scripts; then observer.js runs with TIVOLI_PACKET_LOSS=<loss>
# (0.05 by default: five per cent of its datagrams dropped, in and out) and track.js makes the track and the cabin, boards
# and rides a lap. At the end the bots are taken out of the sandbox's settings and it is restarted clean. The two clients'
# summaries (PHYSICS-TRACK, PHYSICS-OBSERVER) are printed and their samples land under <results dir>.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/Darkride.app"}
R=${2:-"${TMPDIR:-/tmp}/physics-monorail"}
PORT=${3:-42102}
RIDERS=${4:-20}
WALKERS=${5:-20}
LOSS=${6:-0.05}
BUILD=$(cd "$APP/../.." && pwd)
SANDBOX=${SANDBOX:-$HOME/tivoli-sandboxes/physics}
DOMAIN_SANDBOX=${DOMAIN_SANDBOX:-$HOME/git/tivolicloud/domain-sandbox.sh}   # darkride-ops' sandbox starter
CONFIG="$SANDBOX/domain-config.json"
BIN="$APP/Contents/MacOS/Darkride"
# an agent refuses a script from the local filesystem, so the bots are served over HTTP from this folder
BOT_PORT=$((PORT + 78))
BOT="http://127.0.0.1:$BOT_PORT/monorail-bot.js"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
[ -x "$BUILD/assignment-client/assignment-client" ] || { echo "no assignment-client in $BUILD" >&2; exit 1; }
mkdir -p "$R/mover" "$R/observer"
export HIFI_METAVERSE_URL=${HIFI_METAVERSE_URL:-https://vr.darkride.ai}

set_bots() {   # set_bots <riders> <walkers>: the domain's persistent scripts, in the sandbox's config
    python3 - "$CONFIG" "$BOT" "$1" "$2" <<'PY'
import json, os, sys
path, bot, riders, walkers = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
config = {}
if os.path.exists(path):
    with open(path) as f:
        config = json.load(f)
scripts = []
if riders > 0:
    scripts.append({"url": bot + "?role=rider", "num_instances": riders, "pool": ""})
if walkers > 0:
    scripts.append({"url": bot + "?role=walker", "num_instances": walkers, "pool": ""})
config.setdefault("scripts", {})["persistent_scripts"] = scripts
with open(path, "w") as f:
    json.dump(config, f, indent=2)
print("persistent scripts:", len(scripts), "entries,", riders, "riders,", walkers, "walkers")
PY
}

count_agents() {   # the agents the domain lists (its nodes page is open to localhost)
    curl -s "http://localhost:$((PORT - 2))/nodes.json" 2>/dev/null | python3 -c '
import json, sys
try:
    nodes = json.load(sys.stdin)["nodes"]
    print(sum(1 for n in nodes if n.get("type") == "agent"))
except Exception:
    print(-1)'
}

echo "client: $BIN"
echo "build: $BUILD"
echo "domain: hifi://localhost:$PORT (restarting the physics sandbox from this build, with the physics server)"
echo "bots: $RIDERS riders, $WALKERS walkers; observer packet loss $LOSS"
echo "results: $R"
set_bots "$RIDERS" "$WALKERS"
(cd "$D/bots" && python3 -m http.server "$BOT_PORT" --bind 127.0.0.1 > "$R/bot-http.log" 2>&1) &
HTTP_PID=$!
# an earlier run's track and cabin (lifetime 600 s) would still be in the world, and a train on them: stop the sandbox,
# take every baseline-* entity out of its world file, then start it (domain-sandbox.sh stops by its pid files first)
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
world["Entities"] = [e for e in world.get("Entities", []) if not str(e.get("name", "")).startswith("baseline-")]
with gzip.open(path, "wt") as f:
    json.dump(world, f)
print("world cleaned of earlier runs' test entities:", before - len(world["Entities"]), "removed,", len(world["Entities"]), "kept")
PY
# the physics server is off by default (describe-settings.json); this sandbox runs it
python3 -c 'import json, os, sys; p = sys.argv[1]; c = json.load(open(p)) if os.path.exists(p) else {}; c.setdefault("physics_server", {})["enabled"] = True; json.dump(c, open(p, "w"), indent=2)' "$SANDBOX/domain-config.json"
"$DOMAIN_SANDBOX" physics "$BUILD" "$PORT"

# the sandbox's own monitor adds a child only every 30 s, so the bots get a monitor of their own with a child each;
# they ask the domain for work and take the scripted assignments. Wait for them, up to two minutes.
WANT=$((RIDERS + WALKERS))
BOTS_PID=""
# the physics server first: the sandbox's monitor starts six children for seven services and adds the seventh at its
# next spare check (30 s); a bot monitor started before that would hand one of its children to the server instead
for i in $(seq 1 90); do
    curl -s "http://localhost:$((PORT - 2))/nodes.json" 2>/dev/null | grep -q '"physics-server"' && break
    sleep 1
done
echo "physics-server in the domain after ${i}s"
if [ "$WANT" -gt 0 ]; then
    sleep 3
    nohup "$BUILD/assignment-client/assignment-client" -n "$WANT" --max "$WANT" -a 127.0.0.1 --server-port "$PORT" \
        --log-directory "$SANDBOX/logs" > "$SANDBOX/logs/bots-monitor.log" 2>&1 &
    BOTS_PID=$!
    echo "bot monitor pid $BOTS_PID"
fi
HAVE=0
for i in $(seq 1 120); do
    HAVE=$(count_agents)
    [ "$HAVE" -ge "$WANT" ] 2>/dev/null && break
    sleep 1
done
echo "agents in the domain: $HAVE of $WANT after ${i}s"
sleep 5

# the observer first, under loss, so it is in the domain before the cabin appears (the mover's boot phase is 8 s)
TIVOLI_PACKET_LOSS="$LOSS" "$BIN" --allowMultipleInstances --url "hifi://localhost:$PORT" --testScript "$D/observer.js" \
    --testResultsLocation "$R/observer" > "$R/observer.log" 2>&1 &
OBSERVER=$!
sleep 12
"$BIN" --allowMultipleInstances --url "hifi://localhost:$PORT" --testScript "$D/track.js" --testResultsLocation "$R/mover" \
    > "$R/mover.log" 2>&1 || true
wait $OBSERVER || true

grep "PHYSICS-TRACK" "$R/mover.log" || echo "no mover summary in $R/mover.log" >&2
grep "PHYSICS-OBSERVER" "$R/observer.log" || echo "no observer summary in $R/observer.log" >&2
echo "physics-server log: $(ls -t $SANDBOX/logs/*physics-server* 2>/dev/null | head -1)"
grep -h "MONORAIL-BOT" $SANDBOX/logs/*.log 2>/dev/null | grep -c "aboard" | sed 's/^/bots that boarded (log lines): /'

# the sandbox's logs of this run go with the results (the restart below starts new ones)
cp "$SANDBOX/logs/assignment-client.log" "$R/sandbox-assignment-client.log" 2>/dev/null || true
cp "$SANDBOX/logs/domain-server.log" "$R/sandbox-domain-server.log" 2>/dev/null || true
grep "\[physics-server\]" "$R/sandbox-assignment-client.log" | grep -v "jitter\|Kbps\|Ping:\|Connection class" | grep -i "physics-server:\|error\|crash\|fatal" | tail -30

# the sandbox goes back to its services alone
if [ -n "$BOTS_PID" ]; then
    kill "$BOTS_PID" 2>/dev/null || true   # its children follow it (--parent-pid)
fi
kill "$HTTP_PID" 2>/dev/null || true
set_bots 0 0
# the physics server is off by default (describe-settings.json); this sandbox runs it
python3 -c 'import json, os, sys; p = sys.argv[1]; c = json.load(open(p)) if os.path.exists(p) else {}; c.setdefault("physics_server", {})["enabled"] = True; json.dump(c, open(p, "w"), indent=2)' "$SANDBOX/domain-config.json"
"$DOMAIN_SANDBOX" physics "$BUILD" "$PORT"
