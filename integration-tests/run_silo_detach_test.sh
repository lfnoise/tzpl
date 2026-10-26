#!/usr/bin/env bash
# detachVM must stop the silo's tasks and actors along with its VM: a
# melody actor is started, the VM detached and allNotesOff sent, and the
# master output must go quiet (five rounds; the old race was intermittent).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
OUT="$("$APP" --nogui --sample-rate 48000 --buffer-frames 512 "${MODS[@]}" \
        "$ROOT/integration-tests/scripts/silo_detach_check.x" 2>/dev/null \
        | grep -E '^(PASS|FAIL|[0-9]+ passed)')"
echo "$OUT"
if echo "$OUT" | grep -q "FAIL" || ! echo "$OUT" | grep -q "0 failed"; then
    echo "SILO DETACH TEST FAIL"; exit 1
fi
echo "SILO DETACH TEST PASS"
