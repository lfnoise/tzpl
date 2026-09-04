#!/usr/bin/env bash
# Non-flat (AoS) voicer tests: byte-parity between both compilers across the
# control-flow voice-body corpus, plus live behavioral checks (seeded distinct
# per-voice init draws, init-rate state surviving noteOn, per-note seq tables
# starting on pattern[0]). Runs LIVE audio briefly (quiet DC test signals).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/synthc_nonflat_diff.x" 2>&1)
if ! echo "$out" | grep -qF "NONFLAT DIFF PASS"; then
    echo "$out" | grep -E "PASS|FAIL" || true
    echo "NONFLAT VOICER TEST FAIL (diff)"
    exit 1
fi

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/nonflat_voicer_behavior.x" 2>&1)
if ! echo "$out" | grep -qF "NONFLAT BEHAVIOR PASS"; then
    echo "$out" | grep -E "PASS|FAIL" || true
    echo "NONFLAT VOICER TEST FAIL (behavior)"
    exit 1
fi

# Sample bank in an AoS voicer: at each zone's root key the playback rate is
# exactly 1, so the render must reproduce the fixture sample-for-sample
# (onset-aligned; same fixtures and property as run_samplebank_test.sh).
render() { "$APP" --nogui --nrt "$1" --duration 0.1 "${MODS[@]}" "$2" >/dev/null 2>&1; }
render /tmp/sb_fix_lo.wav "$SCRIPTS/samplebank_fixture_lo.x"
render /tmp/sb_fix_hi.wav "$SCRIPTS/samplebank_fixture_hi.x"
SB_PITCH=60 render /tmp/sb_aos_lo.wav "$SCRIPTS/samplebank_sampler_aos.x"
SB_PITCH=72 render /tmp/sb_aos_hi.wav "$SCRIPTS/samplebank_sampler_aos.x"

if ! python3 - <<'EOF'
import struct, sys

def read_ch0(path):
    b = open(path, 'rb').read()
    i = b.find(b'data')
    n = struct.unpack('<I', b[i+4:i+8])[0]
    vals = struct.unpack('<%df' % (n // 4), b[i+8:i+8+n])
    return [vals[k*2] for k in range(len(vals)//2)]   # ch0 of stereo f32

def onset(x, eps=1e-6):
    for i, v in enumerate(x):
        if abs(v) > eps: return i
    return None

def aligned_maxdiff(a, b):
    oa, ob = onset(a), onset(b)
    if oa is None or ob is None: return None
    n = min(len(a) - oa, len(b) - ob)
    return max(abs(a[oa+i] - b[ob+i]) for i in range(n))

fail = 0
for fix, out, label in [("/tmp/sb_fix_lo.wav", "/tmp/sb_aos_lo.wav", "low zone"),
                        ("/tmp/sb_fix_hi.wav", "/tmp/sb_aos_hi.wav", "high zone")]:
    d = aligned_maxdiff(read_ch0(fix), read_ch0(out))
    if d is None or d > 2e-3:
        print("FAIL AoS bank %s (maxdiff %s)" % (label, d)); fail = 1
    else:
        print("PASS AoS bank %s (maxdiff %g)" % (label, d))
sys.exit(fail)
EOF
then
    echo "NONFLAT VOICER TEST FAIL (bank)"
    exit 1
fi
echo "NONFLAT VOICER TEST PASS"
