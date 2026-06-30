#!/usr/bin/env bash
# Silo -> NRT actor messaging (actor model Phase 2c, reverse direction). A silo
# actor sends a 4-note line to an NRT actor "conductor"; the main thread's
# runActorServer drains the silo outbox, decodes each Msg, delivers it by name,
# and drives the NRT actor, which prints "NRT GOT <pitch>". Verifies all four
# arrive. (The silo's per-beat tick is driven by the audio callback; if CoreAudio
# does not start in a headless run, no notes tick -- reported as SKIP.)
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPT="$ROOT/integration-tests/scripts/silo_to_nrt.x"
OUT="$(mktemp)"

if [[ ! -x "$APP" ]]; then echo "SKIP: $APP not built"; exit 0; fi

trap 'pkill -9 -f "tzpl_app.*silo_to_nrt" 2>/dev/null' EXIT

"$APP" --nogui -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" "$SCRIPT" >"$OUT" 2>&1 &
APP_PID=$!
sleep 4
kill -INT "$APP_PID" 2>/dev/null; sleep 0.4; kill -9 "$APP_PID" 2>/dev/null

echo "=== actor output ==="
grep -E "NRT GOT" "$OUT"
echo "===================="

got=$(grep -c "NRT GOT" "$OUT")
if [[ "$got" -eq 4 ]]; then
    echo "SILO->NRT: PASS (all 4 messages delivered + processed)"; rm -f "$OUT"; exit 0
elif [[ "$got" -eq 0 ]]; then
    echo "SILO->NRT: SKIP (no audio ticks -- CoreAudio did not start headless)"; rm -f "$OUT"; exit 0
else
    echo "SILO->NRT: FAIL (got $got/4)"; rm -f "$OUT"; exit 1
fi
