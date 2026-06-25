#!/usr/bin/env bash
# Cross-process actor messaging over NATS: an actor served by serveActors()
# receives messages delivered to a bridged NATS subject. Verifies both messages
# arrive and are processed.
#
# Requires: nats-server on PATH, tzpl_app built with -DTZPL_BUILD_NATS=ON.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPT="$ROOT/integration-tests/scripts/actor_nats_loopback.x"
PORT=14222
OUT="$(mktemp)"

if ! command -v nats-server >/dev/null 2>&1; then echo "SKIP: nats-server not found"; exit 0; fi
if [[ ! -x "$APP" ]]; then echo "SKIP: $APP not built (-DTZPL_BUILD_NATS=ON)"; exit 0; fi

nats-server -p "$PORT" >/tmp/nats_actor_test.log 2>&1 &
NS=$!
trap 'kill "$NS" 2>/dev/null; pkill -9 -f "tzpl_app.*actor_nats_loopback" 2>/dev/null' EXIT
sleep 0.5

"$APP" --nogui --nats-url "nats://127.0.0.1:$PORT" \
    -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" "$SCRIPT" >"$OUT" 2>&1 &
APP_PID=$!
sleep 2
kill -INT "$APP_PID" 2>/dev/null; sleep 0.5; kill -9 "$APP_PID" 2>/dev/null

echo "=== actor output ==="
grep -E "serving|ACTOR GOT" "$OUT"
echo "=================="

got=$(grep -c "ACTOR GOT" "$OUT")
if [[ "$got" -eq 2 ]]; then
    echo "ACTOR-NATS: PASS (both messages delivered + processed)"; rm -f "$OUT"; exit 0
else
    echo "ACTOR-NATS: FAIL (got $got/2)"; rm -f "$OUT"; exit 1
fi
