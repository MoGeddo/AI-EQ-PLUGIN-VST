#!/usr/bin/env python3
"""
audit_decisions.py - run the shipped binary over a folder of takes and count what
each detector does.  This is the "zero intervention" promise, checked instead of
claimed: on the 15 mixes that define the standard, a region detector that fires
is a false alarm by definition, and a report that says nothing must say so with
numbers rather than by staying quiet.

Usage:  .venv/bin/python analysis/scripts/audit_decisions.py [--wav dir] [--cli path]
"""
import argparse
import collections
import glob
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ACTIONABLE = {"boom", "mud", "box", "nasal", "harsh", "reso", "rumble", "dull", "clip",
              "sib", "noise", "codec", "dc"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default="/tmp/wav15")
    ap.add_argument("--cli", default=os.path.join(ROOT, "build", "aeq_cli"))
    args = ap.parse_args()
    files = sorted(glob.glob(os.path.join(args.wav, "*.wav")))
    if not files:
        raise SystemExit(f"no wav files in {args.wav}")

    fires = collections.Counter()
    near = collections.Counter()
    tilt_measured = 0
    zmax = 0.0
    zwhere = 0.0
    per_take = []
    for f in files:
        out = subprocess.run([args.cli, "analyze", f], capture_output=True, text=True).stdout
        ids = [l.split()[1] for l in out.splitlines() if l.startswith("PROBLEM ")]
        ids = [i for i in ids if i in ACTIONABLE or i.endswith("_near")]
        for line in out.splitlines():
            if line.startswith("EXCESS boomtilt"):
                if float(line.split()[5]) > -49:
                    tilt_measured += 1
            elif line.startswith("ZPEAK "):
                # `f` is the loop variable over the takes; rebinding it here is how one
                # row of the table printed "339.0" instead of the song that produced the
                # peak.  The peak's frequency gets its own name.
                _, zf, z = line.split()
                if float(z) > zmax:
                    zmax, zwhere = float(z), float(zf)
        takes = {i for i in ids if not i.endswith("_near")}
        per_take.append((os.path.basename(f)[:26], sorted(takes)))
        fires.update(takes)
        near.update(i for i in ids if i.endswith("_near"))

    n = len(files)
    print(f"{n} takes, {os.path.basename(args.cli)}")
    for name, ids in per_take:
        print(f"  {name:<28} {', '.join(ids) if ids else '-'}")
    print("\nregion detectors on the reference material (a fire is a false alarm):")
    for rid in ("boom", "mud", "box", "nasal", "harsh"):
        print(f"  {rid:<6} fires on {fires[rid]}/{n}")
    print(f"\npitch-relative bottom weight (was a fixed 110-260 Hz band, 0/{n} measurable):")
    print(f"  measurable on {tilt_measured}/{n} takes")
    print(f"\nlargest smoothed z on the reference material: {zmax:.2f} sigma at {zwhere:.0f} Hz")
    print("  (reported as evidence; a 3 dB broad lift is inside the engineer's own range,")
    print("   so it is not an action - see docs/14 section 9)")
    print("\nnear-miss notes (evidence, no action):")
    for rid in ("mud_near", "box_near"):
        print(f"  {rid:<10} {near[rid]}/{n}")
    # The reference mixes ARE the standard, so *any* problem firing on them means the
    # calibration is not wired to the measurement it claims.  This used to check three
    # region ids; on the 15 dry stems `noise` fired on all fifteen and `sib` on ten,
    # which is what a typed threshold looks like from the outside.  Every detector is
    # now calibrated or demoted to a readout, so the bar is the whole list.
    bad = {rid: n for rid, n in fires.items() if n}
    if bad:
        print(f"GATE FAIL: envelope-scored regions fire on the reference material: {bad}")
        raise SystemExit(1)
    if tilt_measured < n:
        print(f"GATE FAIL: pitch-relative bottom weight measurable on only "
              f"{tilt_measured}/{n} takes")
        raise SystemExit(1)
    if zmax > 3.2:
        print(f"GATE FAIL: the z readout reaches {zmax:.2f} sigma on the reference "
              f"material - it would read as an alarm")
        raise SystemExit(1)
    print("GATE PASS: no envelope-scored region fires on the reference material")
    print("\nother detectors, for context:")
    for rid in sorted(k for k in fires if k not in ("boom", "mud", "box", "nasal", "harsh")):
        print(f"  {rid:<6} fires on {fires[rid]}/{n}")


if __name__ == "__main__":
    main()
