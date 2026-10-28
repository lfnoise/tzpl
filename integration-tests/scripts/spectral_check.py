# Numeric checks for the spectralChain renders made by spectral_check.x
# (needs numpy). Usage: python3 spectral_check.py <dir>
#
# Each render is 2 s at 48 kHz, float32, limiter off. [dry, wet] renders
# are checked by finding the lag and least-squares gain of wet against dry
# and the residual left after removing them.
import struct
import sys

import numpy as np


def read_wav(path):
    data = open(path, 'rb').read()
    pos = 12
    fs = ch = None
    audio = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack('<I', data[pos + 4:pos + 8])[0]
        if cid == b'fmt ':
            _fmt, ch, fs, _, _, _bits = struct.unpack('<HHIIHH', data[pos + 8:pos + 24])
        elif cid == b'data':
            audio = data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    x = np.frombuffer(audio, dtype=np.float32).astype(np.float64).reshape(-1, ch)
    return fs, x


DIR = sys.argv[1]
fails = 0


def check(ok, msg):
    global fails
    print(('PASS ' if ok else 'FAIL ') + msg)
    if not ok:
        fails += 1


def load(name):
    fs, x = read_wav(f'{DIR}/{name}.wav')
    assert fs == 48000, fs
    check(np.all(np.isfinite(x)), f'{name}: finite')
    return x


def db(v):
    return 20 * np.log10(v + 1e-30)


def rms(v):
    return np.sqrt(np.mean(v ** 2))


def fit(dry, wet, lag):
    # wet[t + lag] ~ g * dry[t], after the start-up frames
    a = dry[8192:-4096]
    b = wet[8192 + lag:len(wet) - 4096 + lag]
    g = np.dot(a, b) / np.dot(a, a)
    return g, db(rms(b - g * a) / rms(b))


def dry_wet(name, lag, gain, tol=1e-3, resid=-100.0):
    x = load(name)
    g, r = fit(x[:, 0], x[:, 1], lag)
    check(abs(g - gain) < tol and r < resid,
          f'{name}: latency {lag}, gain {g:.5f} (want {gain}), residual {r:.1f} dB (< {resid})')


def spectrum(x, start, n=16384):
    seg = x[start:start + n] * np.hanning(n)
    return np.fft.rfftfreq(n, 1 / 48000), np.abs(np.fft.rfft(seg)) ** 2


dry_wet('id256', 256, 1.0)
dry_wet('id1024', 1024, 1.0)
dry_wet('id2048', 2048, 1.0)
dry_wet('half', 512, 0.5)
dry_wet('series', 1024, 0.25)
dry_wet('control', 512, 0.25)
dry_wet('stereoL', 512, 1.0)
dry_wet('stereoR', 512, 0.5)

# band-pass 1000..2000 Hz on white noise: the stop band is far down
x = load('band')[:, 0]
f, p = spectrum(x, 24000)
inband = p[(f > 1100) & (f < 1900)].mean()
stop = max(p[(f > 100) & (f < 800)].mean(), p[(f > 2300) & (f < 20000)].mean())
check(db(np.sqrt(stop / inband)) < -50, f'band: stop band {db(np.sqrt(stop / inband)):.1f} dB (< -50)')

# shift up 4 bins: 1031.25 Hz -> 1218.75 Hz, nothing left at the original
x = load('shift')[:, 0]
f, p = spectrum(x, 24000)
peak = f[np.argmax(p)]
left = p[np.abs(f - 1031.25) < 20].max() / p.max()
check(abs(peak - 1218.75) < 5 and db(np.sqrt(left)) < -60,
      f'shift: peak {peak:.1f} Hz (want 1218.75), original {db(np.sqrt(left)):.1f} dB (< -60)')

# gate at -40 dB: the hiss away from the tone drops a long way, the tone stays
x = load('gate')
_, pu = spectrum(x[:, 0], 24000)
_, pg = spectrum(x[:, 1], 24000)
noise = (f > 3000) & (f < 20000)
tone = np.abs(f - 1031.25) < 20
drop = db(np.sqrt(pg[noise].mean() / pu[noise].mean()))
tone_loss = db(np.sqrt(pg[tone].max() / pu[tone].max()))
check(drop < -30 and abs(tone_loss) < 0.5, f'gate: hiss {drop:.1f} dB (< -30), tone {tone_loss:+.2f} dB')

# fixed phases: every hop the same pulse shape -> strongly periodic at 256;
# zero phase (a scalar phase, so specComplex stacks a scalar) and centered
for ch, what in ((0, 'zero phase'), (1, 'centered')):
    x = load('robot')[:, ch][24000:72000]
    x = x - x.mean()
    ac = np.correlate(x[:8192], x[:8192 + 512], 'valid')[:513] / np.dot(x[:8192], x[:8192])
    check(ac[256] > 0.8 and rms(x) > 0.01, f'robot ({what}): autocorrelation at the hop {ac[256]:.3f} (> 0.8)')

# smoothed magnitudes converge to the dry level
x = load('smooth')
lvl = db(rms(x[48000:, 1]) / rms(x[48000:, 0]))
early = db(rms(x[1024:1024 + 480, 1]) / rms(x[48000:, 0]))
check(abs(lvl) < 0.5 and early < -3, f'smooth: settled {lvl:+.2f} dB, first 10 ms {early:.1f} dB (< -3)')

# freeze: after the input stops at 1 s, the frozen side still sounds and the
# unfrozen side is silent
x = load('freeze')
tail = slice(int(1.2 * 48000), int(1.9 * 48000))
check(db(rms(x[tail, 1])) > -30 and db(rms(x[tail, 0])) < -100,
      f'freeze: frozen tail {db(rms(x[tail, 1])):.1f} dB (> -30), unfrozen {db(rms(x[tail, 0])):.1f} dB (< -100)')

print('SPECTRAL CHECKS ' + ('PASS' if fails == 0 else f'FAILED: {fails}'))
sys.exit(1 if fails else 0)
