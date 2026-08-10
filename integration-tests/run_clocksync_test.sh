#!/usr/bin/env bash
# NRT <-> engine TempoClock sync test: clock-module callbacks and delayBeats
# follow the engine's TempoClock slots (slot argument forms), including tempo
# set through the audio_engine FFI and engine-side changes mid-wait.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$("$APP" --nogui --tempo-clocks 2 "${MODS[@]}" "$SCRIPTS/clocksync_check.x" 2>&1)
fail=0
for want in "CLOCKSYNC slot0 handler: OK" "CLOCKSYNC slot1 240bpm: OK" \
            "CLOCKSYNC engine tempo change: OK" "CLOCKSYNC getBeats slots: OK"; do
    echo "$out" | grep -qF "$want" || { echo "MISSING: $want"; fail=1; }
done
if [ "$fail" -ne 0 ]; then
    echo "$out"
    echo "CLOCKSYNC TEST FAIL"
    exit 1
fi
echo "CLOCKSYNC TEST PASS"
