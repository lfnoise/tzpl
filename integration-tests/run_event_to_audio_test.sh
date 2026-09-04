#!/usr/bin/env bash
# eventToAudio + event-rate trigger semantics (event_to_audio.x):
#   L: eventToAudio(c) tr  -- one-sample impulses on exact 0->positive samples
#   R: c tr                -- held event-rate trigger, cleared by the NEXT
#                             control event (guards the delay-read activation
#                             fix; before it the trigger never cleared)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/event_to_audio.wav

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

"$APP" --nogui --nrt "$WAV" --duration 0.15 "${MODS[@]}" \
    "$SCRIPTS/event_to_audio.x" >/dev/null 2>&1

python3 - "$WAV" <<'EOF'
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
assert ch == 2, 'expected stereo render'
L = frames[0::2]
R = frames[1::2]
t1, t2, t3, t4 = (round(t * fs) for t in (0.04, 0.07, 0.10, 0.12))

failures = 0
def check(label, cond):
    global failures
    print(('PASS ' if cond else 'FAIL ') + label)
    if not cond:
        failures += 1

eps = 1e-6
# L: one-sample impulses at exactly t1 and t4, zero everywhere else.
imp_samples = [i for i, v in enumerate(L) if abs(v) > eps]
check('L impulses exactly at [%d, %d] (got %s)' % (t1, t4, imp_samples[:6]),
      imp_samples == [t1, t4])
check('L impulse height 1.0', abs(L[t1] - 1.0) < eps and abs(L[t4] - 1.0) < eps)

# R: held trigger -- 1 on [t1, t2), 0 on [t2, t4), 1 from t4 to the end.
def region(sig, a, b, want):
    return all(abs(v - want) < eps for v in sig[a:b])
check('R zero before first trigger', region(R, 0, t1, 0.0))
check('R held 1 on [%d, %d)' % (t1, t2), region(R, t1, t2, 1.0))
check('R cleared by next event on [%d, %d)' % (t2, t4), region(R, t2, t4, 0.0))
check('R held 1 from %d to end' % t4, region(R, t4, len(R), 1.0))

print('EVENT TO AUDIO TEST ' + ('PASS' if failures == 0 else 'FAIL'))
sys.exit(1 if failures else 0)
EOF
