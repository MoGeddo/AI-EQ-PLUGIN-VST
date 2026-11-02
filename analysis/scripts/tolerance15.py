#!/usr/bin/env python3
"""
tolerance15.py - how big must a "problem" be before it is a problem?

The plugin compares a region of the spectrum with the corpus target and calls
it a fault when the excess passes a fixed threshold (2.2 - 2.5 dB).  Measured
on the 15 songs that *define* the target, those thresholds fire on the target
itself:

    boom 110-260   6/15     mud 260-520  4/15     box 520-850  6/15

This script answers two questions with numbers instead of taste.

 1. WHY.  A monophonic voice has almost no energy below its fundamental, so a
    fixed band that reaches under the fundamental measures the singer's
    register, not the mix.  Replicating the plugin's own excess formula on the
    measured LTAS of the 15 songs, the boom number separates by F0 (r = -0.76
    in the plugin's own report).  Clamping the window to f >= 1.5 * F0 - the
    fix implemented in ProblemDetector.h - is tested here first.

 2. HOW MUCH TOLERANCE.  After the clamp, whatever spread is left across the
    15 songs is the engineer's own natural variation: it must be *inside* the
    tolerance, not flagged.  So the threshold is set from that measured spread.

Output
  analysis/out/amir15/tolerance.json
"""
import json
import os

import numpy as np

ROOT = "/home/user/AI-EQ-PLUGIN-VST"
OUT = os.path.join(ROOT, "analysis", "out", "amir15")
GRID = 20.0 * 2.0 ** (np.arange(958) / 96.0)          # same grid as aeq.corpus

# the table compiled into plugin/Source/model/TargetCurve.h (86 pts, 1/12 oct)
TABLE = np.array([
    -20.021, -18.699, -17.178, -15.473, -13.585, -11.496, -9.176, -6.634, -3.937,
    -1.307, 1.056, 3.002, 4.576, 5.895, 7.035, 7.978, 8.684, 9.119, 9.268, 9.137,
    8.762, 8.199, 7.522, 6.834, 6.252, 5.868, 5.714, 5.753, 5.905, 6.067, 6.146,
    6.081, 5.830, 5.351, 4.619, 3.681, 2.672, 1.744, 0.984, 0.403, -0.064, -0.508,
    -0.988, -1.474, -1.883, -2.154, -2.323, -2.510, -2.845, -3.398, -4.149, -5.014,
    -5.898, -6.752, -7.591, -8.482, -9.483, -10.553, -11.534, -12.261, -12.709,
    -13.022, -13.413, -13.999, -14.722, -15.410, -15.908, -16.197, -16.382, -16.593,
    -16.907, -17.313, -17.764, -18.221, -18.665, -19.092, -19.507, -19.940, -20.459,
    -21.149, -22.069, -23.242, -24.690, -26.543, -29.198, -33.442])

REGIONS = [("boom", 110, 260, 2.5), ("mud", 260, 520, 2.2),
           ("box", 520, 850, 2.2), ("nasal", 850, 1300, 2.5),
           ("harsh", 2000, 4200, 2.2)]


def at_smoothed(f, octaves=0.5):
    """TargetCurve::atSmoothed - 1/12-oct table, gaussian over +-0.5 oct."""
    sigma = max(0.5, octaves * 12.0)
    r = int(np.ceil(3.0 * sigma))
    x = np.log2(max(f, 1.0) / 100.0) * 12.0
    c = int(np.clip(round(x), 0, len(TABLE) - 1))
    acc = wsum = 0.0
    for k in range(-r, r + 1):
        j = int(np.clip(c + k, 0, len(TABLE) - 1))
        w = np.exp(-0.5 * (k / sigma) ** 2)
        acc += TABLE[j] * w
        wsum += w
    return acc / wsum


def excess(ltas_db, f0, lo, hi, clamp):
    """excessVsTarget, with or without the f >= 1.5*F0 clamp."""
    if clamp and f0 > 0:
        lo = max(lo, 1.5 * f0)
    ref = ltas_db[(GRID >= 900) & (GRID < 1150)]
    ref = 10 * np.log10(np.mean(10 ** (ref / 10)))
    m = (GRID >= lo) & (GRID < hi) & (GRID >= 100) & (GRID <= 13561.1)
    if m.sum() < 4:
        return None
    tgt = np.array([at_smoothed(f) for f in GRID[m]])
    return float(np.mean((ltas_db[m] - ref) - tgt))


def main():
    C = json.load(open(os.path.join(OUT, "corpus.json")))
    res = {"scripts": "analysis/scripts/tolerance15.py",
           "why": "a fixed band that reaches under the fundamental measures register, not mix",
           "songs": {}, "regions": {}}
    before = {r[0]: [] for r in REGIONS}
    after = {r[0]: [] for r in REGIONS}
    skipped = {r[0]: 0 for r in REGIONS}
    for s in C["songs"]:
        ltas = np.array(s["ltas_bins_db"])
        f0 = s["med_f0_hz"]
        row = {"f0": round(f0, 1)}
        for name, lo, hi, thr in REGIONS:
            b = excess(ltas, f0, lo, hi, False)
            a = excess(ltas, f0, lo, hi, True)
            row[name] = {"before": None if b is None else round(b, 2),
                         "after": None if a is None else round(a, 2),
                         "clamped_lo": round(max(lo, 1.5 * f0), 0)}
            if b is not None:
                before[name].append(b)
            if a is None:
                skipped[name] += 1
            else:
                after[name].append(a)
        res["songs"][s["title"]] = row

    f0 = np.array([s["med_f0_hz"] for s in C["songs"]])
    print(f"{'region':<8}{'thr':>5} | {'before':>22} | {'after the 1.5*F0 clamp':>30}")
    print(f"{'':8}{'':5} | {'fires':>6}{'sd':>8}{'r(F0)':>8} | {'fires':>6}{'sd':>8}{'r(F0)':>8}{'skipped':>9}")
    for name, lo, hi, thr in REGIONS:
        b = np.array(before[name])
        a = np.array(after[name])
        i_b = [i for i, s in enumerate(C["songs"])
               if res["songs"][s["title"]][name]["before"] is not None]
        i_a = [i for i, s in enumerate(C["songs"])
               if res["songs"][s["title"]][name]["after"] is not None]
        rb = np.corrcoef(f0[i_b], b)[0, 1] if len(b) > 2 else float("nan")
        ra = np.corrcoef(f0[i_a], a)[0, 1] if len(a) > 2 else float("nan")
        res["regions"][name] = {
            "band_hz": [lo, hi], "current_threshold_db": thr,
            "before": {"fires_on_target": int((b > thr).sum()), "n": int(len(b)),
                       "sd_db": round(float(b.std()), 2), "r_with_f0": round(float(rb), 2)},
            "after_clamp": {"fires_on_target": int((a > thr).sum()), "n": int(len(a)),
                            "sd_db": round(float(a.std()), 2), "r_with_f0": round(float(ra), 2),
                            "skipped_as_unmeasurable": skipped[name],
                            "recommended_threshold_db": round(float(max(thr, a.std() if len(a) > 2 else thr)), 2)},
        }
        print(f"{name:<8}{thr:>5.1f} | {(b > thr).sum():>6d}{b.std():>8.2f}{rb:>8.2f} | "
              f"{(a > thr).sum():>6d}{a.std():>8.2f}{ra:>8.2f}{skipped[name]:>9d}")

    tot_b = sum(v["before"]["fires_on_target"] for v in res["regions"].values())
    tot_a = sum(v["after_clamp"]["fires_on_target"] for v in res["regions"].values())
    res["headline"] = (f"false 'problems' on the 15 songs that DEFINE the target: "
                       f"{tot_b} before, {tot_a} after the clamp "
                       f"(out of 5 regions x 15 songs)")
    print("\n" + res["headline"])
    os.makedirs(OUT, exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "tolerance.json"), "w"), ensure_ascii=False, indent=1)
    print("wrote", os.path.join(OUT, "tolerance.json"))


if __name__ == "__main__":
    main()
