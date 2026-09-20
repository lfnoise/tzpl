#!/usr/bin/env bash
# A constant-only graph must be patchable into the audio graph.
#
# `140 scalar outlet` used to declare its out port tzpl_constRate. Silo::connect
# requires the two ends' rates to match exactly -- it does no conversion -- so
# every connect of such a def was rejected with errRateMismatch (engine error
# 20), via defSynth + play and via live's `y <- fn() S { 140 scalar }` alike.
# Both compilers now declare a sub-event-rate outlet audio rate.
#
# Rendered once per compiler (the output node takes one multi-channel port, so
# two sources would sum and a failure could not be attributed). The check pins
# the DC level, so a port that is connected but never actually filled --
# silence, or a stale value -- fails here too.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

FAILED=0

check_mode() {  # <mode> <label>
    local mode="$1" label="$2" wav="/tmp/const_outlet_$1.wav"
    TZPL_CONST_MODE="$mode" "$APP" --nogui --nrt "$wav" --duration 0.05 \
        "${MODS[@]}" "$SCRIPTS/const_outlet.x" >/dev/null 2>&1
    if ! python3 - "$wav" "$label" <<'EOF'
import struct, sys

def read_wav(path):
    with open(path, 'rb') as f:
        d = f.read()
    i = 12
    fs, ch = 48000, 1
    while i < len(d):
        cid = d[i:i+4]
        sz = struct.unpack('<I', d[i+4:i+8])[0]
        if cid == b'fmt ':
            ch = struct.unpack('<H', d[i+10:i+12])[0]
            fs = struct.unpack('<I', d[i+12:i+16])[0]
        if cid == b'data':
            n = sz // 4
            return list(struct.unpack('<%df' % n, d[i+8:i+8+sz])), fs, ch
        i += 8 + sz + (sz & 1)
    raise SystemExit('no data chunk')

frames, fs, ch = read_wav(sys.argv[1])
label = sys.argv[2]
# Channel 0 is the one the mono source feeds.
sig = frames[0::ch]
# Skip the head: node creation lands on a sample boundary and the engine's
# output stage may ramp in. Everything after must be flat DC.
lo = min(64, len(sig) // 4)
eps = 1e-6
tail = sig[lo:]

failures = 0
def check(text, cond):
    global failures
    print(('PASS ' if cond else 'FAIL ') + '%s: %s' % (label, text))
    if not cond:
        failures += 1

check('render is long enough (%d frames)' % len(sig), len(sig) > 256)
# The real failure mode: a constRate port was rejected at connect, so the
# channel stayed silent.
check('not silent (peak %.6f)' % max(abs(v) for v in tail),
      max(abs(v) for v in tail) > eps)
check('flat DC +0.25 (min %.6f max %.6f)' % (min(tail), max(tail)),
      all(abs(v - 0.25) < eps for v in tail))
sys.exit(1 if failures else 0)
EOF
    then
        FAILED=1
    fi
}

check_mode cpp "C++ compiler (defSynth)"
check_mode x   "synthc (defSynthX)"

if [ "$FAILED" -ne 0 ]; then
    echo "FAIL: constant-only graphs do not patch into the audio graph"
    exit 1
fi

echo "PASS: constant-only graphs patch into the audio graph"
