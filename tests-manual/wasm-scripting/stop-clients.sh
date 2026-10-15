#!/bin/sh
# Stops every client of this checkout (a client process whose command line names interface-wasm: the build
# path, or a file URL under it), and nothing else: the other sessions' clients and Caity's are left alone.
# SIGTERM first, SIGKILL after 5 s. Run it before and after every test run.
#   tests-manual/wasm-scripting/stop-clients.sh
my_clients() {
    ps -ax -o pid=,command= | grep "MacOS/interface" | grep "interface-wasm" | grep -v grep | awk '{print $1}'
}
PIDS=$(my_clients)
if [ -z "$PIDS" ]; then
    echo "no clients of interface-wasm are running"
    exit 0
fi
for P in $PIDS; do
    echo "stopping $P: $(ps -o etime= -p $P | tr -d ' ') old, $(ps -o command= -p $P | cut -c1-80)"
    kill $P 2>/dev/null
done
sleep 5
for P in $PIDS; do
    if kill -0 $P 2>/dev/null; then
        echo "$P did not stop, killing it"
        kill -9 $P 2>/dev/null
    fi
done
sleep 1
echo "remaining: $(my_clients | wc -l | tr -d ' ')"
