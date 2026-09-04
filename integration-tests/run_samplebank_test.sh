#!/usr/bin/env bash
# End-to-end test for sample banks (loadSampleBank + the per-note bank
# lookup in generated synths).
#
# 1. Renders two sine fixtures (440 Hz and 660 Hz) to wav.
# 2. Loads them as the low/high pitch zones of a sample bank on a voicer
#    sampler synth (compiled by synthc via defSynthX), whose per-note lookup
#    resolves (pitch, velocity) -> sample and plays it back at the rate
#    derived from the resolved rootKey and source sample rate.
# 3. Renders a note in each zone: at the zone's root key the playback rate
#    is exactly 1, so each render must reproduce its fixture sample-for-
#    sample (compared onset-aligned: bundle submission lands on different
#    block boundaries per render, so absolute offsets differ), and the two
#    zone renders must differ from each other (zone selection happened).
# 4. Repeats with a velocity-LAYERED bank: both zones cover the full pitch
#    range and tile by velocity band instead (tiles may share a pitch range
#    as long as their velocity ranges are disjoint). The same pitch played
#    at velocity 40 vs 100 must select the low vs high layer.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
DUR=0.1

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

render() {  # <out.wav> <script>
    "$APP" --nogui --nrt "$1" --duration "$DUR" "${MODS[@]}" "$2" >/dev/null 2>&1
}

render /tmp/sb_fix_lo.wav "$SCRIPTS/samplebank_fixture_lo.x"
render /tmp/sb_fix_hi.wav "$SCRIPTS/samplebank_fixture_hi.x"
SB_PITCH=60 render /tmp/sb_lo.wav "$SCRIPTS/samplebank_sampler.x"
SB_PITCH=72 render /tmp/sb_hi.wav "$SCRIPTS/samplebank_sampler.x"
SB_LAYOUT=vel SB_PITCH=60 SB_VEL=40  render /tmp/sb_vlo.wav "$SCRIPTS/samplebank_sampler.x"
SB_LAYOUT=vel SB_PITCH=60 SB_VEL=100 render /tmp/sb_vhi.wav "$SCRIPTS/samplebank_sampler.x"

python3 - <<'EOF'
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
def check(label, ok, detail):
    global fail
    print(("  PASS " if ok else "  FAIL ") + label + " (" + detail + ")")
    if not ok: fail += 1

fix_lo = read_ch0('/tmp/sb_fix_lo.wav'); smp_lo = read_ch0('/tmp/sb_lo.wav')
fix_hi = read_ch0('/tmp/sb_fix_hi.wav'); smp_hi = read_ch0('/tmp/sb_hi.wav')

check("lo non-silent", onset(smp_lo) is not None, "onset=%s" % onset(smp_lo))
check("hi non-silent", onset(smp_hi) is not None, "onset=%s" % onset(smp_hi))

d = aligned_maxdiff(fix_lo, smp_lo)
check("lo: pitch 60 reproduces the low fixture", d is not None and d < 1e-4, "maxdiff=%s" % d)
d = aligned_maxdiff(fix_hi, smp_hi)
check("hi: pitch 72 reproduces the high fixture", d is not None and d < 1e-4, "maxdiff=%s" % d)
d = aligned_maxdiff(smp_lo, smp_hi)
check("zones render different content", d is not None and d > 0.01, "maxdiff=%s" % d)

# Velocity-layered bank: same pitch range in both zones, tiled by velocity.
smp_vlo = read_ch0('/tmp/sb_vlo.wav'); smp_vhi = read_ch0('/tmp/sb_vhi.wav')
d = aligned_maxdiff(fix_lo, smp_vlo)
check("vel 40: selects the low velocity layer", d is not None and d < 1e-4, "maxdiff=%s" % d)
d = aligned_maxdiff(fix_hi, smp_vhi)
check("vel 100: selects the high velocity layer", d is not None and d < 1e-4, "maxdiff=%s" % d)

print("SAMPLEBANK " + ("PASS" if fail == 0 else "FAIL"))
sys.exit(0 if fail == 0 else 1)
EOF
rc=$?
if [ $rc -eq 0 ]; then
    echo "PASS: sample bank end-to-end"
else
    echo "FAIL: sample bank end-to-end"
fi
exit $rc
