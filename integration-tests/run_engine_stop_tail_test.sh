#!/usr/bin/env bash
# engineStop clears captured audio state: a feedback echo rings, the engine
# is stopped and restarted, and the restart must begin from silence instead
# of resuming the tail frozen in the delay line. Exercises the stopAudio
# node-reset sweep plus the generated plugins' _reset (uninit + init with
# buffer/bank pointers preserved). Runs LIVE audio briefly (quiet 440 Hz).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/engine_stop_clears_tail.x" 2>&1)
if echo "$out" | grep -qF "TAIL CLEAR PASS"; then
    echo "ENGINE STOP TAIL TEST PASS"
else
    echo "$out" | grep -E "peak|PASS|FAIL|err:" || true
    echo "ENGINE STOP TAIL TEST FAIL"
    exit 1
fi
