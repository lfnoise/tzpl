#!/usr/bin/env bash
# delayReal test: `await delayReal(secs)` parks for real wall-clock time on
# the live tempo scheduler (top level and inside async fns), zero/negative
# resolve immediately, and the core virtual-beat delay() stays instant.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/delayreal_check.x" 2>&1)
fail=0
for want in "DELAYREAL top-level: OK" "DELAYREAL async fn: OK" \
            "DELAYREAL zero/negative: OK" "DELAYREAL virtual delay: unchanged" \
            "DELAYREAL tempo change: OK" \
            "DELAYBEATS 60bpm: OK" "DELAYBEATS 240bpm: OK"; do
    echo "$out" | grep -qF "$want" || { echo "MISSING: $want"; fail=1; }
done
if [ "$fail" -ne 0 ]; then
    echo "$out"
    echo "DELAYREAL TEST FAIL"
    exit 1
fi
echo "DELAYREAL TEST PASS"
