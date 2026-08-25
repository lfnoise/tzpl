#!/usr/bin/env bash
# Headless checks for live.plan -- the pure planning layer of the live proxy
# system (graph scans, port mapping, swap-op ordering, quant math). No
# engine, no audio; only the synthdef FFI is needed, so this runs under
# tzpl_app --nogui --no-audio.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/live_plan_check.x" 2>/dev/null \
      | grep -E '^(PASS|FAIL|LIVE PLAN)')"
echo "$OUT"
if echo "$OUT" | grep -q "LIVE PLAN ALL PASS"; then
    echo "PASS: live.plan checks"
    exit 0
fi
echo "FAIL: live.plan checks"
exit 1
