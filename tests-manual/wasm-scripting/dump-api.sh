#!/bin/sh
# Dump the API a script sees, serverless (WASM-SCRIPTING.md round 0): tests-manual/wasm-scripting/dump-api.sh [path/to/interface.app]
# Writes results/api-client.json and results/api-entity_client.json beside this script; the client's log goes to
# results/dump-api.log. The client gets its own settings and data directories under results/dump-api-home.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/interface.app"}
BIN="$APP/Contents/MacOS/interface"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
H="$D/results/dump-api-home"
mkdir -p "$H/config" "$H/data"
echo "client: $BIN"
XDG_CONFIG_HOME="$H/config" TIVOLI_APP_DATA_DIR="$H/data" "$BIN" --allowMultipleInstances --url "file://$D/world.json" \
    --testScript "$D/dump-api.js" --testResultsLocation "$H" > "$D/results/dump-api.log" 2>&1 || true
grep "WASM-API" "$D/results/dump-api.log" || { echo "no WASM-API lines in results/dump-api.log" >&2; exit 1; }
ls -la "$D"/results/api-*.json
