#!/bin/sh
# Runs parity.js on the QtScript engine and on the WebAssembly engine (WASM-SCRIPTING.md round 3) and diffs
# the PARITY lines:  tests-manual/wasm-scripting/parity.sh [path/to/interface.app] [timeout seconds]
# Logs in results/parity/{qtscript,wasm}.log; the PARITY lines in results/parity/{qtscript,wasm}.txt.
set -e
D=$(cd "$(dirname "$0")" && pwd)
APP=${1:-"$D/../../build/interface/interface.app"}
SECONDS_MAX=${2:-120}
BIN="$APP/Contents/MacOS/interface"
[ -x "$BIN" ] || { echo "no client at $BIN" >&2; exit 1; }
R="$D/results/parity"
mkdir -p "$R"
run_one() {
    NAME=$1
    ON=$2
    mkdir -p "$R/$NAME/config" "$R/$NAME/data"
    DARKRIDE_RUN_JS_ON_WASM=$ON XDG_CONFIG_HOME="$R/$NAME/config" TIVOLI_APP_DATA_DIR="$R/$NAME/data" "$BIN" --allowMultipleInstances \
        --url "file://$D/world.json" --testScript "$D/parity.js" --testResultsLocation "$R/$NAME" > "$R/$NAME.log" 2>&1 &
    PID=$!
    WAITED=0
    while kill -0 $PID 2>/dev/null; do
        if [ $WAITED -ge "$SECONDS_MAX" ]; then
            echo "$NAME: timeout after $SECONDS_MAX s, stopping the client"
            kill $PID 2>/dev/null || true
            break
        fi
        sleep 1
        WAITED=$((WAITED + 1))
    done
    grep -o "PARITY .*" "$R/$NAME.log" > "$R/$NAME.txt" || true
    echo "$NAME: $(wc -l < "$R/$NAME.txt") lines"
}
run_one qtscript 0
run_one wasm 1
echo "--- differences (< qtscript, > wasm):"
diff "$R/qtscript.txt" "$R/wasm.txt" || true
