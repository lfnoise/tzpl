# Checks player_quant_render.x's output: each stream (its own pitch) must
# sound only between its grid-aligned start and its switch beat.
# Usage: python3 player_quant_check.py <render.wav>   (pure stdlib)
#
# The render runs at 60 bpm with zero latency, so beat b is at b seconds.
import math
import struct
import sys


def read_wav(path):
    data = open(path, 'rb').read()
    pos = 12
    fs = ch = bits = fmt = None
    audio = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack('<I', data[pos + 4:pos + 8])[0]
        if cid == b'fmt ':
            fmt, ch, fs, _, _, bits = struct.unpack('<HHIIHH', data[pos + 8:pos + 24])
        elif cid == b'data':
            audio = data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    if bits == 32:
        x = struct.unpack('<%df' % (len(audio) // 4), audio)
    else:
        x = [s / 32768.0 for s in struct.unpack('<%dh' % (len(audio) // 2), audio)]
    return fs, x[0::ch]


def level(x, fs, t0, t1, f):
    """Goertzel magnitude of f over [t0, t1) seconds, normalized to amplitude."""
    a, b = int(t0 * fs), int(t1 * fs)
    seg = x[a:b]
    w = 2 * math.pi * f / fs
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for v in seg:
        s1, s2 = v + c * s1 - s2, s1
    p = s1 * s1 + s2 * s2 - c * s1 * s2
    return 2 * math.sqrt(max(p, 0.0)) / max(len(seg), 1)


def onset(x, fs, f, t0, t1, hop=0.002, win=0.01, thresh=0.1):
    """First window start in [t0, t1) where f exceeds thresh (~1/3 of full)."""
    t = t0
    while t < t1:
        if level(x, fs, t, t + win, f) > thresh:
            return t + win  # the window's end is where the tone has filled it
        t += hop
    return None


fs, x = read_wav(sys.argv[1])
fails = []


def expect(cond, msg):
    print(('ok   ' if cond else 'FAIL ') + msg)
    if not cond:
        fails.append(msg)


ON, OFF = 0.1, 0.02
# (freq, start beat, end beat) -- end is the switch beat that cuts it
streams = [(440.0, 1.0, 2.0), (880.0, 2.0, 8.0), (1320.0, 8.0, 9.0), (990.0, 9.0, 12.0)]
for f, start, end in streams:
    o = onset(x, fs, f, start - 0.5, start + 0.5)
    expect(o is not None and abs(o - start) < 0.015,
           f'{f:.0f} Hz starts on beat {start:g} (onset {o})')
    expect(level(x, fs, max(start - 0.95, 0.0), start - 0.05, f) < OFF,
           f'{f:.0f} Hz silent before beat {start:g}')
    if end < 11.5:
        expect(level(x, fs, end + 0.03, min(end + 2.0, 11.9), f) < OFF,
               f'{f:.0f} Hz silent after beat {end:g}')
# A's note from beat 1 sounds right up to the switch (cut there, not early)
expect(level(x, fs, 1.85, 1.98, 440.0) > ON, '440 Hz held until beat 2')
# B's half-beat notes keep coming up to the switch
expect(level(x, fs, 7.52, 7.72, 880.0) > ON, '880 Hz still playing at beat 7.5')
# D was superseded by E (due earlier) before it ever started
expect(level(x, fs, 0.0, 11.9, 660.0) < OFF, '660 Hz never sounds')

if fails:
    print('PLAYER QUANT FAIL')
    sys.exit(1)
print('PLAYER QUANT PASS')
