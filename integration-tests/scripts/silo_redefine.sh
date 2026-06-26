#!/usr/bin/env bash
# Silo function redefinition. Loads pitch()/report() onto silo 0, then siloLoads a
# new pitch() body. The already-compiled report() calls pitch() through its global
# slot; with the persistent incremental compile context the redefinition reuses
# that slot, so report() must print the NEW value. Expects: pitch=60.0 (first
# load) then pitch=72.0 (after redefinition). Deterministic -- both loads are
# awaited, so no dependency on the audio thread ticking.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPT="$ROOT/integration-tests/scripts/silo_redefine.x"
OUT="$(mktemp)"

if [[ ! -x "$APP" ]]; then echo "SKIP: $APP not built"; exit 0; fi

trap 'pkill -9 -f "tzpl_app.*silo_redefine" 2>/dev/null' EXIT

"$APP" --nogui -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" "$SCRIPT" >"$OUT" 2>&1 &
APP_PID=$!
sleep 3
kill -INT "$APP_PID" 2>/dev/null; sleep 0.4; kill -9 "$APP_PID" 2>/dev/null

echo "=== output ==="
grep -E "pitch=|load[01]=|^done" "$OUT"
echo "=============="

if grep -q "pitch=60.0" "$OUT" && grep -q "pitch=72.0" "$OUT"; then
    echo "SILO-REDEFINE: PASS (report() picked up the redefined pitch)"; rm -f "$OUT"; exit 0
else
    echo "SILO-REDEFINE: FAIL"; cat "$OUT"; rm -f "$OUT"; exit 1
fi
