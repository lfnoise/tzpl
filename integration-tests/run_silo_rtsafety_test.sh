#!/usr/bin/env bash
# A silo module can import NRT-wrapper-heavy modules (audio_engine.*, std.fs.*)
# and load, as long as its own code calls only RT-safe functions; a silo that
# actually calls a non-RT-safe function is still rejected, with the full
# compile error returned through the siloLoad Future.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

OUT="$("$APP" --nogui "${MODS[@]}" "$ROOT/integration-tests/scripts/silo_rtsafety_check.x" 2>/dev/null | grep -E '^(PASS|FAIL|[0-9]+ passed)')"
echo "$OUT"
if echo "$OUT" | grep -q "FAIL" || ! echo "$OUT" | grep -q "0 failed"; then
    echo "SILO RTSAFETY TEST FAIL"
    exit 1
fi
echo "SILO RTSAFETY TEST PASS"
