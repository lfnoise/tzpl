#!/usr/bin/env python3
"""Scan a float32 WAV for sample-to-sample discontinuities (clicks)."""
import struct, sys

path = sys.argv[1]
thresh = float(sys.argv[2]) if len(sys.argv) > 2 else 0.02

with open(path, 'rb') as f:
    data = f.read()

assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'not a wav'
pos = 12
fmt = None
samples = None
while pos + 8 <= len(data):
    cid = data[pos:pos+4]
    size = struct.unpack('<I', data[pos+4:pos+8])[0]
    body = data[pos+8:pos+8+size]
    if cid == b'fmt ':
        fmt = struct.unpack('<HHIIHH', body[:16])
    elif cid == b'data':
        audio = body
    pos += 8 + size + (size & 1)

wformat, chans, rate, _, _, bits = fmt
assert wformat == 3 and bits == 32, f'expected float32, got fmt={wformat} bits={bits}'
n = len(audio) // 4
flat = struct.unpack(f'<{n}f', audio)
frames = n // chans

spikes = []
prev = [0.0] * chans
for i in range(frames):
    for c in range(chans):
        v = flat[i * chans + c]
        d = abs(v - prev[c])
        if d > thresh:
            spikes.append((i / rate, c, d))
        prev[c] = v

print(f'{path}: {frames} frames, {chans} ch, {rate} Hz')
print(f'diff spikes > {thresh}: {len(spikes)}')
# group consecutive spikes into events (within 10 ms)
events = []
for t, c, d in spikes:
    if events and t - events[-1][1] < 0.01:
        ev = events[-1]
        events[-1] = (ev[0], t, max(ev[2], d))
    else:
        events.append((t, t, d))
for t0, t1, d in events[:20]:
    print(f'  click at {t0:.4f}s  maxjump={d:.4f}')
print('CLEAN' if not events else f'{len(events)} click event(s)')
