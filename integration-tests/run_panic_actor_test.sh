#!/usr/bin/env bash
# The "clear schedulers" panic must stop a delay-driven silo actor, not only
# the SiloTaskScheduler's coroutine tasks. Spawns an arpeggiating actor, then
# panics and asserts the master output goes quiet.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
OUT="$("$APP" --nogui --sample-rate 48000 --buffer-frames 512 "${MODS[@]}" \
        "$ROOT/integration-tests/scripts/panic_actor_check.x" 2>/dev/null \
        | grep -E '^(PASS|FAIL|[0-9]+ passed)')"
echo "$OUT"
if echo "$OUT" | grep -q "FAIL" || ! echo "$OUT" | grep -q "0 failed"; then
    echo "PANIC ACTOR TEST FAIL"; exit 1
fi
echo "PANIC ACTOR TEST PASS"
