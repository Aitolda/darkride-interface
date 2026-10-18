#!/bin/zsh
# One arrival in a test world, in either loading mode, on this session's own sandbox (the client skips the sign-in
# screen with --tokens "{}", so the arrival is timed from the connection):
#   tests-manual/world-loading/run.sh <build dir> <results dir> <whole|progressive> [world dir] [skip after s] [udp port]
# LOADING_RADIUS=<metres> sets the domain's loading.radius (default 100; 0 waits for every model in the world), and
# LOADING_TIME_LIMIT=<seconds> its loading.time_limit (default 60).
# The world dir has the snapshot layout of instances/tivoli-instance.sh (models.json.gz, config.json with the spawn
# point, assets/map.json and assets/files/<sha256>); the default is the Tivoli Town copy in ~/tivoli-test-worlds.
# The sandbox ("loading", ~/tivoli-sandboxes/loading) gets a fresh copy of the world and the domain's loading
# settings each run, starts, takes one walled-off client running arrival.js, and is stopped by its pids afterwards.
# The client is walled off (its own settings and app data), and its resource cache is fresh each run (a cold arrival):
# the caches live in TIVOLI_APP_DATA_DIR (data/cache, data/ktx_cache; ~/Library/Caches is not touched, checked 10-18),
# which points into the run and is deleted after it.
set -e
D=$(cd "$(dirname "$0")" && pwd)
BUILD=${1:?usage: run.sh <build dir> <results dir> <whole|progressive> [world dir] [skip after s] [udp port]}
R=${2:?results dir}
MODE=${3:?whole or progressive}
WORLD=${4:-$HOME/tivoli-test-worlds/tivolitown-v2}
SKIP=${5:--1}
PORT=${6:-42102}
SANDBOX=$HOME/tivoli-sandboxes/loading
APP="$BUILD/interface/interface.app"
BIN="$APP/Contents/MacOS/interface"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
[ -f "$WORLD/models.json.gz" ] || { echo "no world at $WORLD" >&2; exit 1; }
case "$MODE" in whole|progressive) ;; *) echo "mode is whole or progressive" >&2; exit 1;; esac

# the world, fresh: the entity server persists into its copy
mkdir -p "$SANDBOX/entities" "$SANDBOX/assets"
for pidfile in "$SANDBOX"/*.pid(N); do
    pid=$(cat "$pidfile"); case "$(ps -o command= -p $pid 2>/dev/null)" in *"$BUILD"*) kill $pid;; esac; rm -f "$pidfile"
done
cp "$WORLD/models.json.gz" "$SANDBOX/entities/models.json.gz"
rm -rf "$SANDBOX/assets"; mkdir -p "$SANDBOX/assets"
[ -d "$WORLD/assets" ] && cp -R "$WORLD/assets/." "$SANDBOX/assets/"
# the domain's settings: the loading mode and the world's spawn point
python3 - "$SANDBOX/domain-config.json" "$WORLD/config.json" "$MODE" <<'PY'
import json, os, sys
path, world_config, mode = sys.argv[1:4]
config = json.load(open(path)) if os.path.exists(path) else {}
world = json.load(open(world_config)) if os.path.exists(world_config) else {}
config["loading"] = {"whole_world": mode == "whole", "time_limit": int(os.environ.get("LOADING_TIME_LIMIT") or 60)}
if os.environ.get("LOADING_RADIUS"):
    config["loading"]["radius"] = int(os.environ["LOADING_RADIUS"])  # metres; 0 waits for the whole world
if world.get("paths"):
    config["paths"] = world["paths"]
json.dump(config, open(path, "w"), indent=2)
PY
/Users/thistlequell/git/tivolicloud/domain-sandbox.sh loading "$BUILD" "$PORT"
for i in {1..60}; do curl -s "localhost:$((PORT - 2))/nodes.json" | grep -q entity-server && break; sleep 1; done

# one walled-off client, with a fresh cache
RUN="$R/$MODE-$(date +%H%M%S)"
mkdir -p "$RUN/config/Tivoli Cloud VR - dev" "$RUN/data" "$RUN/snaps" "$RUN/cache"
echo "{\"snapshotsLocation\": \"$RUN/snaps\"}" > "$RUN/config/Tivoli Cloud VR - dev/Interface.json"
{ echo "var ARRIVAL_SKIP_AFTER = $SKIP;"; cat "$D/arrival.js"; } > "$RUN/arrival.js"
echo "client: $BIN  mode: $MODE  world: $WORLD  results: $RUN"
XDG_CONFIG_HOME="$RUN/config" XDG_CACHE_HOME="$RUN/cache" TIVOLI_APP_DATA_DIR="$RUN/data" HIFI_METAVERSE_URL=https://vr.darkride.ai \
    "$BIN" --allowMultipleInstances --url "hifi://localhost:$PORT" --tokens "{}" --testScript "$RUN/arrival.js" \
    --testResultsLocation "$RUN" > "$RUN/client.log" 2>&1 || true

# the sandbox off again, by its pids
for pidfile in "$SANDBOX"/*.pid(N); do
    pid=$(cat "$pidfile"); case "$(ps -o command= -p $pid 2>/dev/null)" in *"$BUILD"*) kill $pid;; esac; rm -f "$pidfile"
done
# the client's caches (2 GB a run for Tivoli Town, the textures in data/ktx_cache) go; the log and snapshots stay
rm -rf "$RUN/data" "$RUN/cache"
grep "WORLD-LOADING\|World loading" "$RUN/client.log" || { echo "no summary in $RUN/client.log" >&2; exit 1; }
