#!/usr/bin/env bash
# End-to-end test for the fillBuffer FFI + buffer wrap-at-length semantics.
#
# 1. Builds one period of a sine with the ifft builtin, pushes it into an
#    engine buffer with fillBuffer, and plays it through a vread wavetable
#    oscillator; the render must match a sinosc reference to interpolation
#    error.
# 2. Renders the same counter-driven buffer read from a non-power-of-two
#    6-frame ramp buffer and from a 12-frame buffer holding the ramp twice;
#    the renders are bit-identical exactly when buffer indices wrap at the
#    buffer's true length (the old pow2-mask wrap read zero padding instead).
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

render /tmp/fb_sine_ref.wav "$SCRIPTS/fill_buffer_sine_ref.x"
render /tmp/fb_sine_tab.wav "$SCRIPTS/fill_buffer_sine_tab.x"
render /tmp/fb_wrap6.wav    "$SCRIPTS/fill_buffer_wrap6.x"
render /tmp/fb_wrap12.wav   "$SCRIPTS/fill_buffer_wrap12.x"

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/fill_buffer_compare.x" 2>/dev/null)"
echo "$OUT"
if echo "$OUT" | grep -q "FILL BUFFER PASS"; then
    echo "PASS: fillBuffer + wrap-at-length end-to-end"
    exit 0
fi
echo "FAIL: fillBuffer end-to-end"
exit 1
