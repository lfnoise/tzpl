# Spectral regression checks for the pink noise family (needs numpy).
# Usage: python3 pink_spectrum_check.py <pink.wav> <pinkf.wav> <pinkfe.wav>
#
# Reads float32 WAVs rendered by pink_check.x (30 s, fs 48k, limiter off),
# fits the power slope against 1/f, and bounds the deviation at spot
# frequencies. Bounds are generous vs. the measured spectra (480 s
# characterization, Aug 2026) but tight enough to catch the historical
# regressions: the unweighted ladder's -1.2 dB dip near fs/5 in pink, and
# the stale-state Kellett filter's HF collapse (-3.4 dB @10k) in pinkf.
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
    x = np.frombuffer(audio, dtype=np.float32).astype(np.float64).reshape(-1, ch)[:, 0]
    return fs, x


def deviation_bins(path):
    """1/6-octave deviations (dB) from ideal 1/f, anchored to 100 Hz-1 kHz."""
    fs, x = read_wav(path)
    if np.isnan(x).any():
        return None, None, 'NaN in render'
    rms = float(np.sqrt((x ** 2).mean()))
    if not (0.02 < rms < 0.3):
        return None, None, f'rms {rms:.4f} out of range'
    seg = 1 << 15
    hop = seg // 2
    win = np.hanning(seg)
    n = (len(x) - seg) // hop + 1
    psd = np.zeros(seg // 2 + 1)
    for i in range(n):
        s = x[i * hop:i * hop + seg] * win
        psd += np.abs(np.fft.rfft(s)) ** 2
    psd /= n
    f = np.fft.rfftfreq(seg, 1 / fs)
    v = f > 0
    f, psd = f[v], psd[v]
    edges = 2.0 ** np.arange(np.log2(20), np.log2(fs / 2) + 1 / 6, 1 / 6)
    fc, dev = [], []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (f >= lo) & (f < hi)
        if m.sum() == 0:
            continue
        c = np.sqrt(lo * hi)
        fc.append(c)
        dev.append(10 * np.log10(psd[m].mean()) + 10 * np.log10(c))
    fc, dev = np.array(fc), np.array(dev)
    dev -= dev[(fc >= 100) & (fc <= 1000)].mean()
    return fc, dev, None


def at(fc, dev, freq):
    return dev[np.argmin(np.abs(fc - freq))]


def check(name, path, spots):
    """spots: list of (freq, lo_dB, hi_dB) deviation bounds."""
    fc, dev, err = deviation_bins(path)
    if err is not None:
        print(f'FAIL {name}: {err}')
        return False
    # dev is already relative to 1/f, so its residual slope must be ~0
    sel = (fc >= 30) & (fc <= 18000)
    slope = np.polyfit(np.log2(fc[sel]), dev[sel], 1)[0]
    ok = True
    if abs(slope) > 0.35:
        print(f'FAIL {name}: residual slope {slope:+.2f} dB/octave vs 1/f')
        ok = False
    for freq, lo, hi in spots:
        d = at(fc, dev, freq)
        if not (lo <= d <= hi):
            print(f'FAIL {name}: deviation {d:+.2f} dB at {freq} Hz (bounds {lo:+.1f}..{hi:+.1f})')
            ok = False
    if ok:
        print(f'PASS {name} spectrum (residual slope {slope:+.2f} dB/oct)')
    return ok


ok = True
# pink: flat everywhere; +-0.6 catches the old -1.2 dB dip at fs/5
ok &= check('pink', sys.argv[1], [
    (100, -0.6, 0.6), (1000, -0.6, 0.6), (5000, -0.6, 0.6),
    (8000, -0.6, 0.6), (12000, -0.6, 0.6), (16000, -0.6, 0.6),
])
# pinkf: flat to near Nyquist; the stale-state bug gave -3.4 @10k, -6.5 @15k
ok &= check('pinkf', sys.argv[2], [
    (100, -0.6, 0.6), (1000, -0.6, 0.6), (5000, -0.8, 0.8),
    (10000, -1.0, 1.0), (15000, -1.2, 1.2),
])
# pinkfe: economy tolerance
ok &= check('pinkfe', sys.argv[3], [
    (100, -1.2, 1.2), (1000, -1.2, 1.2), (10000, -1.5, 1.5),
])

sys.exit(0 if ok else 1)
