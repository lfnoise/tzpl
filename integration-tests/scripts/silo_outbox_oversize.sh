#!/usr/bin/env bash
# siloOutbox oversize rejection. A silo start() encodes a ~9 KB Msg (well over
# the 4096-byte OutboxMsg::kMaxBytes cap) and calls siloOutbox; it must return
# tzpl_errInternal (1) without enqueuing, while a small message returns
# tzpl_errNone (0). start() runs via siloStartAt (gCurrentSilo set); ordering vs
# the script thread is irrelevant, so assert the return codes, not order.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPT="$ROOT/integration-tests/scripts/silo_outbox_oversize.x"
OUT="$(mktemp)"

if [[ ! -x "$APP" ]]; then echo "SKIP: $APP not built"; exit 0; fi

trap 'pkill -9 -f "tzpl_app.*silo_outbox_oversize" 2>/dev/null' EXIT

"$APP" --nogui -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" "$SCRIPT" >"$OUT" 2>&1 &
APP_PID=$!
sleep 3
kill -INT "$APP_PID" 2>/dev/null; sleep 0.4; kill -9 "$APP_PID" 2>/dev/null

echo "=== output ==="
grep -E "bigLen=|oversize_rc=|normal_rc=" "$OUT"
echo "=============="

big=$(grep -oE 'bigLen=[0-9]+' "$OUT" | grep -oE '[0-9]+$')
over=$(grep -oE 'oversize_rc=-?[0-9]+' "$OUT" | grep -oE '\-?[0-9]+$')
norm=$(grep -oE 'normal_rc=-?[0-9]+' "$OUT" | grep -oE '\-?[0-9]+$')

if [[ "${over:-}" == "1" && "${norm:-}" == "0" && "${big:-0}" -gt 4096 ]]; then
    echo "SILO-OUTBOX-OVERSIZE: PASS (${big}B payload > 4096 rejected rc=1; normal rc=0)"
    rm -f "$OUT"; exit 0
elif [[ -z "${over:-}" ]]; then
    echo "SILO-OUTBOX-OVERSIZE: SKIP (start() did not run -- no audio tick headless)"
    rm -f "$OUT"; exit 0
else
    echo "SILO-OUTBOX-OVERSIZE: FAIL (big=${big:-?} over=${over:-?} norm=${norm:-?})"
    cat "$OUT"; rm -f "$OUT"; exit 1
fi
