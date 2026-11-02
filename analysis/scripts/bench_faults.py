#!/usr/bin/env python3
"""
bench_faults.py - inject a known EQ fault into real takes and check that the
shipped detector calls it.

A detector that never fires is indistinguishable from a detector that works, so
the acceptance criterion is always "this fault, on this many takes".  The fault is
injected with a biquad peaking filter, the same shape an engineer would use, and
the takes are the reference vocals - so nothing here depends on synthetic audio.

Usage:
  .venv/bin/python analysis/scripts/bench_faults.py --freq 665 --q 1.5 --db 4 --expect 2
  .venv/bin/python analysis/scripts/bench_faults.py --sweep          (the table in docs)

Exit code 0 only if the detector flagged at least --expect takes (and never on
the untouched material, which is checked in audit_decisions.py).
"""
import argparse
import glob
import os
import subprocess

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REGION_IDS = {"boom", "mud", "box", "nasal", "harsh"}


def peak(x, sr, f, q, db):
    w0 = 2 * np.pi * f / sr
    a = np.sin(w0) / (2 * q)
    A = 10 ** (db / 40.0)
    return lfilter([1 + a * A, -2 * np.cos(w0), 1 - a * A],
                   [1 + a / A, -2 * np.cos(w0), 1 - a / A], x)


def flagged(cli, path):
    out = subprocess.run([cli, "analyze", path], capture_output=True, text=True).stdout
    return {l.split()[1] for l in out.splitlines()
            if l.startswith("PROBLEM ") and l.split()[1] in REGION_IDS}


def bench(cli, files, f, q, db, verbose=True):
    hits = []
    tmp = os.path.join(ROOT, "analysis", "out", "amir15", "_bench.wav")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    for path in files:
        x, sr = sf.read(path, dtype="float32", always_2d=True)
        x = x.mean(axis=1)
        sf.write(tmp, peak(x, sr, f, q, db).astype(np.float32), sr)
        ids = flagged(cli, tmp)
        hits.append(ids)
        if verbose:
            print(f"  @{f:>5} +{db} dB Q{q}  {os.path.basename(path)[:26]:<28} "
                  f"{sorted(ids) if ids else '-'}")
    try:
        os.remove(tmp)
    except OSError:
        pass
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default="/tmp/wav15")
    ap.add_argument("--cli", default=os.path.join(ROOT, "build", "aeq_cli"))
    ap.add_argument("--freq", type=float, default=665)
    ap.add_argument("--q", type=float, default=1.5)
    ap.add_argument("--db", type=float, default=4.0)
    ap.add_argument("--expect", type=int, default=2)
    ap.add_argument("--takes", type=int, default=3)
    ap.add_argument("--sweep", action="store_true")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.wav, "*.wav")))[:args.takes]
    if not files:
        raise SystemExit(f"no wav files in {args.wav}")

    if args.sweep:
        print("fault              caught")
        for f, q, db in [(368, 0.8, 3.0), (665, 0.8, 3.0), (368, 2.0, 3.0),
                         (665, 2.0, 3.0), (665, 1.5, 4.0), (300, 1.5, 5.0)]:
            hits = bench(args.cli, files, f, q, db, verbose=False)
            n = sum(1 for h in hits if h)
            print(f"@{f:>4} +{db} dB Q{q:<4}  {n}/{len(files)}")
        return

    hits = bench(args.cli, files, args.freq, args.q, args.db)
    n = sum(1 for h in hits if h)
    print(f"\ncaught {n}/{len(files)} (needed {args.expect})")
    if n < args.expect:
        print("GATE FAIL: the detector misses this fault")
        raise SystemExit(1)
    print("GATE PASS: fault caught on the required number of takes")


if __name__ == "__main__":
    main()
