#!/usr/bin/env bash
# Live NATS loopback test for the Msg binary message format.
# Starts a local nats-server, runs nats_msg_loopback.x (which subscribes to a
# subject as Bytes and publishes an encoded Msg to it), and checks the message
# round-trips and decodes correctly.
#
# Requires: nats-server on PATH, and tzpl_app built with -DTZPL_BUILD_NATS=ON.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPT="$ROOT/integration-tests/scripts/nats_msg_loopback.x"
PORT=14222
OUT="$(mktemp)"

if ! command -v nats-server >/dev/null 2>&1; then
    echo "SKIP: nats-server not found on PATH"; exit 0
fi
if [[ ! -x "$APP" ]]; then
    echo "SKIP: $APP not built (configure with -DTZPL_BUILD_NATS=ON)"; exit 0
fi

nats-server -p "$PORT" >/tmp/nats_server_test.log 2>&1 &
NS=$!
trap 'kill "$NS" 2>/dev/null' EXIT
sleep 0.5

"$APP" --nogui --nats-url "nats://127.0.0.1:$PORT" \
    -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" "$SCRIPT" >"$OUT" 2>&1 &
APP_PID=$!
sleep 1.5
kill -INT "$APP_PID" 2>/dev/null
wait "$APP_PID" 2>/dev/null

echo "=== app output ==="
cat "$OUT"
echo "=================="

if grep -q 'DECODED \[note, 60, 0.8, "hello over nats"\]' "$OUT"; then
    echo "LOOPBACK: PASS"
    rm -f "$OUT"
    exit 0
else
    echo "LOOPBACK: FAIL"
    rm -f "$OUT"
    exit 1
fi
