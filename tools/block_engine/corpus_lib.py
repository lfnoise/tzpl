"""Shared helpers for the block-engine reference corpus tools.

The corpus (corpus.tsv) lists NRT renders made from `main` before the block
engine work, and re-rendered on the block-engine branch for comparison
(devplans/BLOCK_ENGINE_PLAN.md, M0 and M5).
"""

import array
import os
import struct
from dataclasses import dataclass

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(TOOLS_DIR))
CORPUS_TSV = os.path.join(TOOLS_DIR, "corpus.tsv")


@dataclass
class Entry:
    name: str
    duration: float
    env: dict
    includes: list
    entry: str      # path to a .x file, or "driver:<module>:<code>"
    expect: str     # "same" or "change"
    note: str


def load_corpus(path=CORPUS_TSV):
    """Parses corpus.tsv. Blank lines and lines starting with # are skipped.
    Columns (tab separated): name duration env includes entry expect note.
    env is space-separated KEY=VALUE pairs or "-"; includes is
    comma-separated directories relative to the repo root or "-"."""
    entries, seen = [], set()
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) < 6:
                raise ValueError(f"{path}:{lineno}: expected at least 6 columns")
            name, dur, env, incs, entry, expect = cols[:6]
            note = cols[6] if len(cols) > 6 else ""
            if name in seen:
                raise ValueError(f"{path}:{lineno}: duplicate name {name}")
            if expect not in ("same", "change"):
                raise ValueError(f"{path}:{lineno}: expect must be same or change")
            seen.add(name)
            entries.append(Entry(
                name=name,
                duration=float(dur),
                env={} if env == "-" else dict(kv.split("=", 1) for kv in env.split()),
                includes=[] if incs == "-" else incs.split(","),
                entry=entry,
                expect=expect,
                note=note,
            ))
    return entries


@dataclass
class Wav:
    rate: int
    chans: int
    samples: array.array   # interleaved float32

    @property
    def frames(self):
        return len(self.samples) // self.chans if self.chans else 0

    def channel(self, c):
        return self.samples[c::self.chans]


def read_wav(path):
    """Reads a 32-bit float WAV (format 3), as tzpl_app --nrt writes."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    i, fmt, data = 12, None, None
    while i + 8 <= len(d):
        cid, sz = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24])
        elif cid == b"data":
            data = d[i + 8:i + 8 + sz]
        i += 8 + sz + (sz & 1)
    if fmt is None or data is None:
        raise ValueError(f"{path}: missing fmt or data chunk")
    tag, chans, rate, _, _, bits = fmt
    if tag != 3 or bits != 32:
        raise ValueError(f"{path}: expected 32-bit float samples")
    a = array.array("f")
    a.frombytes(data[:len(data) - len(data) % 4])
    return Wav(rate=rate, chans=chans, samples=a)
