#!/usr/bin/env bash
# NRT renders: bundles scheduled from a clock handler (or code resumed by
# `await delayBeats`) at its own beat must land on that beat's sample. The
# renderer used to fire them after the block holding their beat, so they
# landed late, on the next block boundary.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/tzpl_nrt_handler_timing.wav

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
command -v python3 >/dev/null || { echo "SKIP: no python3"; exit 0; }
rm -f "$WAV"

"$APP" --nogui --nrt "$WAV" --duration 6 "${MODS[@]}" \
    "$SCRIPTS/nrt_handler_timing.x" >/dev/null 2>&1

# Expected: the first sample at which the 73 bpm clock reaches each beat.
if python3 - "$WAV" <<'PY'
import array, math, struct, sys
b = open(sys.argv[1], 'rb').read()
ch, sr = struct.unpack('<HI', b[22:28])
i = b.find(b'data'); n = struct.unpack('<I', b[i+4:i+8])[0]
a = array.array('f'); a.frombytes(b[i+8:i+8+n])
def steps(c):
    x = a[c::ch]
    return [k for k in range(1, len(x)) if abs(x[k] - x[k-1]) > 1e-4]
def want(beats):
    return [math.ceil(bt * 60.0 / 73.0 * sr - 1e-6) for bt in beats]
ok = True
for c, name, beats in ((0, "sched handler", [0.25 + 0.5 * k for k in range(11)]),
                       (1, "delayBeats", [0.4 + 0.7 * k for k in range(8)])):
    got, exp = steps(c), want(beats)
    bad = [(g, e) for g, e in zip(got, exp) if abs(g - e) > 1]
    if len(got) < len(exp) or bad:
        ok = False
        print(f"FAIL {name}: {len(got)} steps, want {len(exp)}; (got, want) off: {bad[:4]}")
    else:
        print(f"PASS {name}: {len(exp)} steps on their beat samples")
sys.exit(0 if ok else 1)
PY
then rm -f "$WAV"; echo "NRT HANDLER TIMING TEST PASS"
else rm -f "$WAV"; echo "NRT HANDLER TIMING TEST FAIL"; exit 1
fi
