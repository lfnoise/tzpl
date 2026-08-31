#!/usr/bin/env bash
# NoteParam event-rate sample accuracy: noteSetParams mid-note must take
# effect on exactly its scheduled sample. The test synth emits its noteParam
# as DC while gated (noteparam_event_rate.x); the WAV must be a step function
# 0.25 -> 0.75 (at 0.04 s) -> 0.5 (at 0.07 s) with edges on the exact
# scheduled samples. Guards the np_active flag path: generated
# noteSetParams/noteSetParamRange set per-serial flags, the engine flags the
# node, and processEvents recomputes the per-voice value the same sample.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/noteparam_event_rate.wav

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

"$APP" --nogui --nrt "$WAV" --duration 0.15 "${MODS[@]}" \
    "$SCRIPTS/noteparam_event_rate.x" >/dev/null 2>&1

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
mono = frames[::ch]

failures = 0
def check(label, cond):
    global failures
    print(('PASS ' if cond else 'FAIL ') + label)
    if not cond:
        failures += 1

# Step edges must land on the exact scheduled samples (60 BPM: beat == sec).
s1, s2 = round(0.04 * fs), round(0.07 * fs)
eps = 1e-6
check('level before first change is 0.25', abs(mono[s1 - 1] - 0.25) < eps)
check('0.75 lands exactly at sample %d' % s1, abs(mono[s1] - 0.75) < eps)
check('level before second change is 0.75', abs(mono[s2 - 1] - 0.75) < eps)
check('0.5 lands exactly at sample %d' % s2, abs(mono[s2] - 0.5) < eps)
check('steady tail at 0.5', abs(mono[-1] - 0.5) < eps)

print('NOTEPARAM EVENT TEST ' + ('PASS' if failures == 0 else 'FAIL'))
sys.exit(1 if failures else 0)
EOF
