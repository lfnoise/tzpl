#!/usr/bin/env bash
# Gated linen envelope (common_ugens.x): linear slew on the gate. The test
# synth (linen_envelope.x) renders two envelopes as DC, one per channel:
# a full release (rise completes, holds at 1, decays over `dec`) and an
# early release (gate drops mid-rise, so the decay starts immediately from
# the current amplitude with the same slope). Guards the linear-segment
# semantics: rise slope 1/rise, decay slope 1/dec, exact hold at 1.0,
# clamped zero tails.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/linen_envelope.wav

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

"$APP" --nogui --nrt "$WAV" --duration 1.6 "${MODS[@]}" \
    "$SCRIPTS/linen_envelope.x" >/dev/null 2>&1

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
full = frames[0::ch]
early = frames[1::ch]

failures = 0
def check(label, cond):
    global failures
    print(('PASS ' if cond else 'FAIL ') + label)
    if not cond:
        failures += 1

def at(sig, t):
    return sig[round(t * fs)]

def slope(sig, t0, t1):
    return (at(sig, t1) - at(sig, t0)) / (t1 - t0)

RISE, DEC = 0.2, 0.4

# Full release, channel 0.
check('rise slope is 1/rise', abs(slope(full, 0.02, 0.18) - 1 / RISE) < 1e-3)
check('rise midpoint is 0.5', abs(at(full, 0.1) - 0.5) < 1e-3)
check('holds at exactly 1.0', at(full, 0.5) == 1.0 and at(full, 0.95) == 1.0)
check('decay slope is -1/dec', abs(slope(full, 1.05, 1.35) + 1 / DEC) < 1e-3)
check('full tail clamps to 0', at(full, 1.45) == 0.0 and full[-1] == 0.0)

# Early release, channel 1: gate drops at 0.1 s, halfway up the rise.
check('early tracks the rise before release', abs(at(early, 0.05) - at(full, 0.05)) < 1e-6)
check('early peak is the release amplitude', abs(max(early) - 0.5) < 1e-3)
check('early decay starts at once, same slope', abs(slope(early, 0.12, 0.25) + 1 / DEC) < 1e-3)
check('early reaches 0 in dec/2, stays', at(early, 0.35) == 0.0 and early[-1] == 0.0)

print('LINEN ENVELOPE TEST ' + ('PASS' if failures == 0 else 'FAIL'))
sys.exit(1 if failures else 0)
EOF
