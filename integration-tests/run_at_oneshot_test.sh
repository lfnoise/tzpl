#!/usr/bin/env bash
# at()/after() must fire their Void handler exactly once. The default-slot
# overloads used to schedule without oneShot, so a handler re-fired at the
# same beat forever and the render hung. Bounded: a regression is killed and
# fails rather than hanging the suite.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/tzpl_at_oneshot.wav
OUT=/tmp/tzpl_at_oneshot.out

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
rm -f "$WAV"

"$APP" --nogui --nrt "$WAV" --duration 3 "${MODS[@]}" "$SCRIPTS/at_oneshot_check.x" \
    > "$OUT" 2>&1 &
pid=$!
for i in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
if kill -0 $pid 2>/dev/null; then
    kill -9 $pid 2>/dev/null || true
    rm -f "$WAV" "$OUT"
    echo "FAIL: render hung (an at()/after() handler is re-firing)"
    echo "AT ONESHOT TEST FAIL"
    exit 1
fi
wait $pid 2>/dev/null || true

if ! grep -qF "AT ONESHOT: OK" "$OUT"; then
    grep -F "AT ONESHOT" "$OUT" || echo "(no AT ONESHOT output)"
    rm -f "$WAV" "$OUT"
    echo "AT ONESHOT TEST FAIL"
    exit 1
fi
rm -f "$WAV" "$OUT"
echo "AT ONESHOT TEST PASS"
