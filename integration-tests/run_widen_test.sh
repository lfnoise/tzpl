#!/usr/bin/env bash
# Mixing f32 and f64 signals widens to f64 (issue #16).
#
# Type inference used to unify an operation with its operands, so an f64
# signal meeting an f32 one had no solution ("intersected to empty") in both
# compilers. Now, when unification fails, the operands that must widen get an
# explicit cast and inference runs again (f32 + f64 -> f64, floats win over
# ints), as the SynthDef guide says.
#
# Each case is rendered four ways -- {C++, synthc} x {implicit widening, the
# same graph with the casts written by hand} -- and all four must be
# bit-identical and non-silent: widening must compute exactly what the
# explicit casts compute, in both compilers. synthc_widen_diff.x (in the FFI
# suite) checks the generated C++ byte-matches as well.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
CASES=11

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

FAILED=0
render() {  # mode form case -> wav path
    local wav="/tmp/widen_$1_$2_$3.wav"
    rm -f "$wav"
    TZPL_WIDEN_MODE="$1" TZPL_WIDEN_FORM="$2" TZPL_WIDEN_CASE="$3" \
        "$APP" --nogui --nrt "$wav" --duration 0.25 "${MODS[@]}" \
        "$SCRIPTS/widen_render.x" >/dev/null 2>&1 || true
    echo "$wav"
}

for i in $(seq 0 $((CASES - 1))); do
    ref=$(render cpp cast "$i")
    if ! python3 - "$ref" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read() if __import__('os').path.exists(sys.argv[1]) else b''
i = 12; peak = 0.0; n = 0
while i + 8 <= len(d):
    cid, sz = d[i:i+4], struct.unpack('<I', d[i+4:i+8])[0]
    if cid == b'data':
        v = struct.unpack('<%df' % (sz // 4), d[i+8:i+8+sz]); n = len(v)
        peak = max((abs(x) for x in v if x == x), default=0.0)
    i += 8 + sz + (sz & 1)
sys.exit(0 if n > 1024 and peak > 1e-3 else 1)
PY
    then
        echo "FAIL case $i: hand-cast C++ reference missing or silent"; FAILED=1; continue
    fi
    for mf in "cpp widened" "x widened" "x cast"; do
        out=$(render $mf "$i")
        if cmp -s "$ref" "$out"; then
            echo "PASS case $i: $mf == cpp cast"
        else
            echo "FAIL case $i: $mf differs from (or failed vs) cpp cast"; FAILED=1
        fi
    done
done

if [ "$FAILED" -ne 0 ]; then
    echo "FAIL: f32/f64 widening does not match explicit casts"
    exit 1
fi
echo "PASS: f32/f64 widening matches explicit casts in both compilers"
