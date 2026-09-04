#!/usr/bin/env bash
# End-to-end test for sample bank sustain loops: smpl-chunk metadata
# extraction (root key + fractional loop points) and the sub-sample-accurate
# loopPhasor wrap.
#
# Fixtures are written by Python (an implementation independent of the
# engine's chunk walker) as float32 WAVs whose content is a sine of period
# P = 40.05 frames, silent from frame 4500 on, with an smpl chunk:
#
#   sbl_fix_file.wav  unityNote 60, loop [1000, 1400.5)  -- 10 P exactly, the
#                     .5 coming from the loop's dwFraction sub-sample field
#   sbl_fix_expl.wav  unityNote 60, loop [1000, 1350)    -- NOT a multiple of P
#
# Because the aligned loop length is an exact multiple of the sine period,
# a sub-sample-accurate looper at rate 1 must reproduce the analytic sine
# indefinitely -- every wrap subtracts exactly 10 periods. Any reset-to-start
# wrap, integer quantization of the loop length, or missed dwFraction shows
# up as a growing phase error. The misaligned fixture proves the comparison
# has teeth (its file loop CANNOT stay clean) and that explicit sampleZone
# loop points override the file's.
#
# Renders (0.2 s, note held throughout):
#   1. file fixture, pitch 60          -> clean sine across ~15 wraps
#   2. file fixture, pitch 60.5        -> clean resampled sine (fractional
#                                         positions + fractional wrap)
#   3. file fixture, sus=0             -> playhead runs past the loop into
#                                         the silent tail (no wrap)
#   4. expl fixture, file loop         -> NOT clean (discrimination check)
#   5. expl fixture, explicit [1000, 1400.5) -> clean again (override wins)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
DUR=0.2

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

render() {  # <out.wav> [ENV=VAL ...]
    local out="$1"; shift
    env "$@" "$APP" --nogui --nrt "$out" --duration "$DUR" "${MODS[@]}" \
        "$SCRIPTS/samplebank_loop.x" >/dev/null 2>&1
}

# Probe the engine's render sample rate so the fixtures can declare it (the
# sampler's rate term is fileSR/engineSR, which must be exactly 1 here).
render /tmp/sbl_probe.wav SBL_WAV=/dev/null 2>/dev/null || true

python3 - <<'EOF'
import math, struct

def probe_sr(path):
    b = open(path, 'rb').read()
    i = b.find(b'fmt ')
    return struct.unpack('<I', b[i+12:i+16])[0]

SR = probe_sr('/tmp/sbl_probe.wav')
P = 40.05                  # sine period in frames
N = 6000                   # total frames; silent from SILENT_FROM on
SILENT_FROM = 4500
AMP = 0.5

def smpl_chunk(unity_note, loop_start, loop_end):
    # loop_end is EXCLUSIVE and may be fractional; the smpl loop stores the
    # last played frame (inclusive) plus a 32-bit fraction of the next frame.
    end_incl = loop_end - 1.0
    dw_end = int(math.floor(end_incl))
    dw_frac = int(round((end_incl - dw_end) * 2**32)) & 0xFFFFFFFF
    body = struct.pack('<9I', 0, 0, int(1e9 / SR), unity_note, 0, 0, 0, 1, 0)
    body += struct.pack('<6I', 0, 0, loop_start, dw_end, dw_frac, 0)
    return b'smpl' + struct.pack('<I', len(body)) + body

def write_fixture(path, loop_start, loop_end):
    frames = [AMP * math.sin(2 * math.pi * i / P) if i < SILENT_FROM else 0.0
              for i in range(N)]
    data = struct.pack('<%df' % N, *frames)
    fmt = b'fmt ' + struct.pack('<IHHIIHH', 16, 3, 1, SR, SR * 4, 4, 32)
    chunks = fmt + smpl_chunk(60, loop_start, loop_end) \
        + b'data' + struct.pack('<I', len(data)) + data
    riff = b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks
    open(path, 'wb').write(riff)

write_fixture('/tmp/sbl_fix_file.wav', 1000, 1400.5)   # 10 P exactly
write_fixture('/tmp/sbl_fix_expl.wav', 1000, 1350.0)   # misaligned

# AIFF fixture exercising the big-endian INST + MARK walker branch: PCM16,
# period P2 = 40.1 frames, loop [1000, 1401) = 10 P2 exactly (AIFF loop
# points are integer marker positions; sub-sample accuracy still matters
# because the wrap subtracts the loop length from fractional playheads).
P2 = 40.1
def ext80(x):  # IEEE 754 80-bit extended, for the AIFF sample rate field
    m = int(x)
    e = 16383 + 63
    while m < 2**63: m <<= 1; e -= 1
    return struct.pack('>HQ', e, m)

frames2 = [AMP * math.sin(2 * math.pi * i / P2) if i < SILENT_FROM else 0.0
           for i in range(N)]
data2 = struct.pack('>%dh' % N, *[int(round(v * 32767)) for v in frames2])
comm = b'COMM' + struct.pack('>IhIh', 18, 1, N, 16) + ext80(SR)
mark = struct.pack('>H', 2) \
    + struct.pack('>HI', 1, 1000) + b'\x02b0\x00' \
    + struct.pack('>HI', 2, 1401) + b'\x02e0\x00'
mark = b'MARK' + struct.pack('>I', len(mark)) + mark
inst = b'INST' + struct.pack('>I', 20) \
    + struct.pack('>bbbbbbh', 60, 0, 0, 127, 0, 127, 0) \
    + struct.pack('>HHH', 1, 1, 2) + struct.pack('>HHH', 0, 0, 0)
ssnd = b'SSND' + struct.pack('>III', 8 + len(data2), 0, 0) + data2
form = comm + mark + inst + ssnd
open('/tmp/sbl_fix_aiff.aiff', 'wb').write(
    b'FORM' + struct.pack('>I', 4 + len(form)) + b'AIFF' + form)
print('fixtures written at SR', SR)
EOF

render /tmp/sbl_file.wav  SBL_WAV=/tmp/sbl_fix_file.wav
render /tmp/sbl_frac.wav  SBL_WAV=/tmp/sbl_fix_file.wav SBL_PITCH=60.5
render /tmp/sbl_nosus.wav SBL_WAV=/tmp/sbl_fix_file.wav SBL_SUS=0.0
render /tmp/sbl_badfile.wav SBL_WAV=/tmp/sbl_fix_expl.wav
render /tmp/sbl_expl.wav  SBL_WAV=/tmp/sbl_fix_expl.wav SBL_LS=1000.0 SBL_LE=1400.5
render /tmp/sbl_aiff.wav  SBL_WAV=/tmp/sbl_fix_aiff.aiff

status=0
python3 - <<'EOF' || status=$?
import math, struct, sys

P = 40.05
AMP = 0.5
SILENT_FROM = 4500

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

def sine_maxdiff(x, rate, span):
    # The playhead advances `rate` frames per output frame; wraps subtract an
    # exact multiple of P, so the reference is AMP*sin(2*pi*j*rate/P) at
    # note frame j. Frame 0 is sin(0) = 0, invisible to onset detection, so
    # the detected onset is note frame 1 -- hence k+1.
    o = onset(x)
    if o is None: return None
    n = min(span, len(x) - o)
    return max(abs(x[o+k] - AMP * math.sin(2 * math.pi * (k+1) * rate / P))
               for k in range(n))

fail = 0
def check(name, ok, detail):
    global fail
    print(('  PASS ' if ok else '  FAIL ') + name + ' (' + detail + ')')
    if not ok: fail += 1

x = read_ch0('/tmp/sbl_file.wav')
d = sine_maxdiff(x, 1.0, 6000)
check('file loop, rate 1: phase-locked across ~15 fractional wraps',
      d is not None and d < 2e-3, 'maxdiff=%s' % d)

x = read_ch0('/tmp/sbl_frac.wav')
r = 2.0 ** (0.5 / 12.0)
d = sine_maxdiff(x, r, 6000)
check('file loop, pitch 60.5: fractional positions + fractional wrap',
      d is not None and d < 5e-3, 'maxdiff=%s' % d)

x = read_ch0('/tmp/sbl_nosus.wav')
o = onset(x)
head = sine_maxdiff(x, 1.0, 4400)
tail = max(abs(v) for v in x[o+4600:o+5900]) if o is not None else None
check('sus=0: playhead runs past the loop into the silent tail',
      o is not None and head < 2e-3 and tail is not None and tail < 1e-6,
      'head maxdiff=%s tail max=%s' % (head, tail))

x = read_ch0('/tmp/sbl_file.wav')
o = onset(x)
sus_tail = max(abs(v) for v in x[o+4600:o+5900])
check('sus=1: the loop sustains where the run-out went silent',
      sus_tail > 0.3, 'tail max=%s' % sus_tail)

x = read_ch0('/tmp/sbl_badfile.wav')
d = sine_maxdiff(x, 1.0, 6000)
check('misaligned file loop breaks phase (comparison has teeth)',
      d is not None and d > 0.1, 'maxdiff=%s' % d)

x = read_ch0('/tmp/sbl_expl.wav')
d = sine_maxdiff(x, 1.0, 6000)
check('explicit zone loop overrides the file loop',
      d is not None and d < 2e-3, 'maxdiff=%s' % d)

P = 40.1   # AIFF fixture period (see fixture writer)
x = read_ch0('/tmp/sbl_aiff.wav')
d = sine_maxdiff(x, 1.0, 6000)
check('AIFF INST/MARK loop + baseNote (16-bit fixture)',
      d is not None and d < 2e-3, 'maxdiff=%s' % d)

print('SAMPLEBANK LOOP ' + ('FAIL' if fail else 'PASS'))
sys.exit(1 if fail else 0)
EOF
if [ $status -eq 0 ]; then echo "PASS: sample bank loops end-to-end"; else echo "FAIL: sample bank loops"; fi
exit $status
