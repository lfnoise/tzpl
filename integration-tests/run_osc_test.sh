#!/usr/bin/env bash
# End-to-end test for the band-limited wavetable oscillators (osc family).
#
# 1. A bank generated from a single sine partial makes every table a pure
#    sine; osc at a fixed frequency must match sinosc to interpolation error.
# 2. A saw-bank FM sweep exercises per-sample table re-selection and the
#    adjacent-table crossfade; the render must be non-silent and bounded.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
DUR=0.1

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

render() {  # <out.wav> <script>
    "$APP" --nogui --nrt "$1" --duration "$DUR" "${MODS[@]}" "$2" >/dev/null 2>&1
}

render /tmp/osc_sine_ref.wav  "$SCRIPTS/osc_sine_ref.x"
render /tmp/osc_sine_bank.wav "$SCRIPTS/osc_sine_bank.x"
render /tmp/osc_sweep.wav     "$SCRIPTS/osc_sweep.x"

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/osc_compare.x" 2>/dev/null)"
echo "$OUT"
if echo "$OUT" | grep -q "OSC PASS"; then
    echo "PASS: wavetable osc end-to-end"
    exit 0
fi
echo "FAIL: wavetable osc end-to-end"
exit 1
