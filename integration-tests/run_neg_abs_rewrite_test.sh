#!/usr/bin/env bash
# `k * abs(x)` must keep its sign when k < 0 (issue #13).
#
# Both rewriters turned `k * abs(x)` into `abs(k * x)` for any low-rate k, so a
# negative constant or control silently produced +|k||x| instead of -|k||x|.
# The rule now fires only on a positive constant.
#
# Rendered once per compiler and case (the output node takes one port, so a
# failure stays attributable). Each case must be one-signed, with the right
# sign, and peak at |k| * 0.1 -- which also proves the positive-constant case
# still compiles to the right function.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

FAILED=0

check_case() {  # <mode> <case> <sign> <peak> <label>
    local mode="$1" kase="$2" wav="/tmp/neg_abs_$2_$1.wav"
    TZPL_ABS_MODE="$mode" TZPL_ABS_CASE="$kase" "$APP" --nogui --nrt "$wav" \
        --duration 0.1 "${MODS[@]}" "$SCRIPTS/neg_abs_rewrite.x" >/dev/null 2>&1
    if ! python3 - "$wav" "$3" "$4" "$5" <<'EOF'
import struct, sys

def read_wav(path):
    with open(path, 'rb') as f:
        d = f.read()
    i = 12
    ch = 1
    while i < len(d):
        cid = d[i:i+4]
        sz = struct.unpack('<I', d[i+4:i+8])[0]
        if cid == b'fmt ':
            ch = struct.unpack('<H', d[i+10:i+12])[0]
        if cid == b'data':
            n = sz // 4
            return list(struct.unpack('<%df' % n, d[i+8:i+8+sz])), ch
        i += 8 + sz + (sz & 1)
    raise SystemExit('no data chunk')

frames, ch = read_wav(sys.argv[1])
sign, peak, label = int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
sig = frames[0::ch]
eps = 1e-6

failures = 0
def check(text, cond):
    global failures
    print(('PASS ' if cond else 'FAIL ') + '%s: %s' % (label, text))
    if not cond:
        failures += 1

check('render is long enough (%d frames)' % len(sig), len(sig) > 1024)
wrong = sum(1 for v in sig if v * sign < -eps)
check('one-signed, sign %+d (%d wrong-signed samples)' % (sign, wrong), wrong == 0)
got = max(abs(v) for v in sig)
check('peak %.4f ~= %.4f' % (got, peak), abs(got - peak) < 0.01 * peak)
sys.exit(1 if failures else 0)
EOF
    then
        FAILED=1
    fi
}

for mode in cpp x; do
    [ "$mode" = cpp ] && who="C++ compiler (defSynth)" || who="synthc (defSynthX)"
    check_case "$mode" negconst -1 0.2 "$who, -2 * abs(x)"
    check_case "$mode" negctl   -1 0.2 "$who, control(-2) * abs(x)"
    check_case "$mode" posconst  1 0.3 "$who, 3 * abs(x)"
done

if [ "$FAILED" -ne 0 ]; then
    echo "FAIL: k * abs(x) loses its sign for negative k"
    exit 1
fi

echo "PASS: k * abs(x) keeps its sign for negative k"
