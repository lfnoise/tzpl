#!/usr/bin/env python3
"""Compares two renders of the block-engine reference corpus.

    compare_corpus.py REFDIR NEWDIR [--tol-db DB] [--exact] [-f REGEX]

For each corpus entry rendered in both directories, it reports the largest
sample difference in dBFS and the time of the first sample that exceeds the
tolerance.

An entry passes when:
- both renders are non-silent and finite;
- they have the same rate and channel count;
- the difference stays under --tol-db (default -100 dBFS) over the shorter
  render, and the lengths differ by at most one block (64 frames).

--exact requires byte-identical files instead. That is how the reference is
checked for determinism: render it twice from the same commit.

Entries marked "change" in corpus.tsv are reported but never fail the run.
"""

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from corpus_lib import load_corpus, read_wav  # noqa: E402

SILENCE_PEAK = 1e-4
BLOCK = 64


def db(x):
    return -math.inf if x <= 0 else 20.0 * math.log10(x)


def compare(ref_path, new_path, tol):
    r, n = read_wav(ref_path), read_wav(new_path)
    if (r.rate, r.chans) != (n.rate, n.chans):
        return False, f"format differs: {r.rate}Hz/{r.chans}ch vs {n.rate}Hz/{n.chans}ch"
    problems = []
    for label, w in (("ref", r), ("new", n)):
        finite = [abs(x) for x in w.samples if math.isfinite(x)]
        if len(finite) != len(w.samples):
            problems.append(f"{label} has {len(w.samples) - len(finite)} non-finite samples")
        if max(finite, default=0.0) < SILENCE_PEAK:
            problems.append(f"{label} is silent")
    m = min(len(r.samples), len(n.samples))
    worst, first = 0.0, None
    rs, ns = r.samples, n.samples
    for i in range(m):
        d = abs(rs[i] - ns[i])
        if d > worst:
            worst = d
        if first is None and d > tol:
            first = i
    if abs(r.frames - n.frames) > BLOCK:
        problems.append(f"length {r.frames} vs {n.frames} frames")
    msg = f"max diff {db(worst):7.1f} dBFS"
    if first is not None:
        msg += f", first over tolerance at {first // r.chans / r.rate:.6f}s (ch {first % r.chans})"
    if problems:
        msg += "; " + "; ".join(problems)
    return first is None and not problems, msg


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("refdir")
    ap.add_argument("newdir")
    ap.add_argument("--tol-db", type=float, default=-100.0)
    ap.add_argument("--exact", action="store_true")
    ap.add_argument("-f", "--filter")
    args = ap.parse_args()
    tol = 10.0 ** (args.tol_db / 20.0)

    failed, checked, missing = [], 0, []
    for e in load_corpus():
        if args.filter and not re.search(args.filter, e.name):
            continue
        rp = os.path.join(args.refdir, f"{e.name}.wav")
        np_ = os.path.join(args.newdir, f"{e.name}.wav")
        have_ref, have_new = os.path.exists(rp), os.path.exists(np_)
        if not (have_ref and have_new):
            if have_ref or have_new:
                missing.append(e.name)
            continue
        checked += 1
        if args.exact:
            with open(rp, "rb") as a, open(np_, "rb") as b:
                ok = a.read() == b.read()
            msg = "identical" if ok else "bytes differ"
            if not ok:
                msg += " (" + compare(rp, np_, 0.0)[1] + ")"
        else:
            ok, msg = compare(rp, np_, tol)
        tag = "PASS" if ok else ("CHANGED" if e.expect == "change" else "FAIL")
        print(f"{tag:<8} {e.name:<36} {msg}")
        if not ok and e.expect == "same":
            failed.append(e.name)

    if missing:
        print(f"rendered in only one dir ({len(missing)}): {' '.join(missing)}")
    print(f"{checked - len(failed)}/{checked} passed")
    sys.exit(1 if failed or missing else 0)


if __name__ == "__main__":
    main()
