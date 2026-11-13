#!/usr/bin/env python3
"""calibrate_warn.py - the bars behind the detectors that are not scored by a region.

Four detectors in `ProblemDetector` compare one number against one literal:

    rumble   level(30-70 Hz) - level(300-700 Hz)        > -34 dB
    sib      the sibilance p90 of 5-10k / 1-4k          > -6 dB
    dull     target(9k) - (level(7-12k) - level(1-2k))  > 4 dB
    noise    the silence floor                          > -72 dBFS

Those four numbers were typed.  On the 15 dry stems that *define* the target, two of them
fire on his own material - `noise` on 15 of 15, `sib` on 10 - which by the project's own
rule (analysis/scripts/audit_decisions.py) is a false alarm: the reference mixes are the
definition of "right", not examples of what is wrong.

The naive fix - "bar = worst of his 15 + a margin" - is what this script first did, and it
is wrong in a way worth keeping the code for: it silences the detector on *every* take that
starts lower than the noisy one.  A bar is only honest if it satisfies both halves at once:

    no take of his fires          max(clean)   <   bar        (for "high is bad")
    every faulted take fires      min(faulted) >   bar

so the quantity that decides whether a statistic is usable is the GAP between those two
edges, not where the bar lands.  When the gap is negative no bar exists: the statistic is
the wrong one, and the fix is to measure something take-relative instead of something
absolute.  That is what happened to `dull` (absolute distance to a target curve, spread
6.9 dB across his own mixes) and to `sib` (absolute p90, spread 27 dB).

    .venv/bin/python analysis/scripts/calibrate_warn.py --wav /tmp/wav15          # report
    .venv/bin/python analysis/scripts/calibrate_warn.py --wav /tmp/wav15 --write  # header
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")
OUT = os.path.join(ROOT, "analysis", "out", "amir15")
CACHE = os.path.join(OUT, "warn_stats.json")
HEADER = os.path.join(ROOT, "plugin", "Source", "model", "WarnThresholds.h")
MIN_GAP_DB = 1.0        # a bar needs this much daylight between the two edges
MARGIN_DB = 1.0         # how far past the edge an evidence-only bar sits


def stats(cli: str, path: str) -> dict:
    out = subprocess.run([cli, "analyze", path], capture_output=True, text=True).stdout
    d: dict[str, float] = {}
    for line in out.splitlines():
        if line.startswith("STAT "):
            _, k, v = line.split()
            d[k] = float(v)
        elif line.startswith("EXCESS "):
            parts = line.split()          # EXCESS <id> raw <v> env <v>
            if len(parts) == 6:
                d[f"excess_{parts[1]}_raw"] = float(parts[3])
                d[f"excess_{parts[1]}_env"] = float(parts[5])
    return d


# ---- the four faults, each the shape the detector claims to catch ------------------
def add_hiss(x, sr, db):
    rng = np.random.default_rng(7)
    level = 10.0 ** (db / 20.0) * np.sqrt(np.mean(x ** 2))
    return x + level * rng.standard_normal(len(x)) * 0.3


def boost_high(x, sr, fc, db):
    sos = butter(2, fc / (sr / 2), btype="highpass", output="sos")
    return x + (10 ** (db / 20.0) - 1.0) * sosfilt(sos, x)


def lowpass(x, sr, fc, order=4):
    return sosfilt(butter(order, fc / (sr / 2), btype="lowpass", output="sos"), x)


def peak(x, sr, f, q, db):
    """A peaking bell of `db` at `f`: the shape of the fault each region claims to catch."""
    from scipy.signal import lfilter
    w0 = 2 * np.pi * f / sr
    a = np.sin(w0) / (2 * q)
    A = 10 ** (db / 40.0)
    return lfilter([1 + a * A, -2 * np.cos(w0), 1 - a * A],
                   [1 + a / A, -2 * np.cos(w0), 1 - a / A], x)


def sibilant_bursts(x, sr, n=8, ms=60.0, db=12.0):
    """Short 5-10 kHz bursts: what a de-esser is for.

    A static shelf was the wrong fault for this detector and the numbers said so - a
    shelf moves the whole distribution, so the *excess* of the peaks over the body does
    not move at all (+1.0 dB), and the absolute level of 5-10 kHz spans 27 dB across his
    own mixes anyway.  Sibilance is a dynamic problem: peaks, not level.
    """
    rng = np.random.default_rng(11)
    y = x.copy()
    sos = butter(2, [5000.0 / (sr / 2), 10000.0 / (sr / 2)], btype="bandpass", output="sos")
    body = np.sqrt(np.mean(sosfilt(sos, x) ** 2))
    span = len(x) // (n + 1)
    win = np.hanning(int(ms / 1000.0 * sr))
    for i in range(n):
        start = span * (i + 1)
        burst = rng.standard_normal(len(win)) * body * 10 ** (db / 20.0) * win
        y[start:start + len(win)] += sosfilt(sos, burst)
    return y


def add_hum(x, sr, hz, db):
    t = np.arange(len(x)) / sr
    level = 10.0 ** (db / 20.0) * np.sqrt(np.mean(x ** 2))
    return x + level * np.sin(2 * np.pi * hz * t)


FAULTS = {
    "noise":  ("hiss at -20 dB of the take RMS", lambda x, sr: add_hiss(x, sr, -20.0)),
    "sib":    ("eight 60 ms 5-10k bursts at +12 dB", lambda x, sr: sibilant_bursts(x, sr)),
    "dull":   ("12 dB/octave low-pass at 7 kHz", lambda x, sr: lowpass(x, sr, 7000.0)),
    "rumble": ("40 Hz tone at -20 dB",           lambda x, sr: add_hum(x, sr, 40.0, -20.0)),
    # the two raw-domain regions: the same +4 dB Q1.5 bell the region is supposed to catch
    # A region detector claims a BROAD lift, so the falsifier is broad: Q1.5 inside a
    # one-octave region moves the region average by only +1.2 dB, which is why the old
    # 2.2 dB bar could not have caught one.
    "harsh":  ("+4 dB Q0.7 bell at 3.0 kHz",     lambda x, sr: peak(x, sr, 3000.0, 0.7, 4.0)),
    "nasal":  ("+4 dB Q0.7 bell at 1.2 kHz",     lambda x, sr: peak(x, sr, 1200.0, 0.7, 4.0)),
}

# A detector whose bar sits beyond his own range only speaks for a bigger fault, so the
# number worth publishing is HOW BIG that fault has to be.  These are the same shapes at
# twice the size (and 20 dB bursts); the script reports the size that trips each bar.
STRONG = {
    "sib":    ("200 bursts of 100 ms at +20 dB", lambda x, sr: sibilant_bursts(x, sr, 200, 100.0, 20.0)),
    "harsh":  ("+8 dB Q0.7 bell at 3.0 kHz",     lambda x, sr: peak(x, sr, 3000.0, 0.7, 8.0)),
    "nasal":  ("+8 dB Q0.7 bell at 1.2 kHz",     lambda x, sr: peak(x, sr, 1200.0, 0.7, 8.0)),
    "dull":   ("18 dB/octave low-pass at 6 kHz", lambda x, sr: lowpass(x, sr, 6000.0, order=6)),
}

# (detector, statistic, which way is bad, human name)
CANDIDATES = [
    ("rumble", "rumble",     +1, "30-70 Hz over 300-700 Hz"),
    ("sib",    "sibp90",     +1, "sibilance p90, absolute"),
    ("sib",    "sibexc",     +1, "sibilance p90 minus the take's median"),
    ("dull",   "airmiss",    +1, "distance from the 9 kHz target"),
    ("dull",   "airslope",   -1, "8-12k over 2-4k, inside the take"),
    ("noise",  "noisefloor", +1, "silence floor, dBFS"),
    ("harsh",  "excess_harsh_raw", +1, "2-4.2k excess over the target curve"),
    ("harsh",  "harshslope", +1, "2-4.2k over 6-9k, inside the take"),
    ("nasal",  "excess_nasal_raw", +1, "850-1300 excess over the target curve"),
    ("nasal",  "nasalslope", +1, "850-1300 over 2-4k, inside the take"),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default="/tmp/wav15")
    ap.add_argument("--cli", default=CLI)
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--fault-takes", type=int, default=4)
    args = ap.parse_args()

    if not os.path.exists(args.cli):
        raise SystemExit(f"build it first: g++ -std=c++17 -O2 -Iplugin/Source "
                         f"plugin/cli/main.cpp -o {args.cli}")
    files = sorted(glob.glob(os.path.join(args.wav, "*.wav")))
    if not files:
        raise SystemExit(f"no wav files in {args.wav}")
    os.makedirs(OUT, exist_ok=True)

    # ---- clean pass ---------------------------------------------------------------
    if os.path.exists(CACHE) and not args.refresh:
        data = json.load(open(CACHE, encoding="utf-8"))
        if len(data.get("clean", {})) == len(files):
            print(f"clean pass from cache ({len(data['clean'])} takes); --refresh to redo")
        else:
            args.refresh = True
    if args.refresh or not os.path.exists(CACHE):
        data = {"clean": {}, "fault": {}}
    if not data.get("clean") or len(data["clean"]) != len(files):
        print(f"clean pass: {len(files)} takes through {os.path.basename(args.cli)}")
        data["clean"] = {os.path.basename(f): stats(args.cli, f) for f in files}
        json.dump(data, open(CACHE, "w", encoding="utf-8"), indent=1)

    clean = data["clean"]
    keys = ["rumble", "sibp90", "sibmed", "sibexc", "airmiss", "airslope", "airtop",
            "noisefloor", "maxrms", "hfcut"]
    print(f"\n{'statistic':<11} {'min':>8} {'median':>8} {'max':>8}")
    for k in keys:
        col = np.array([e[k] for e in clean.values() if k in e])
        if col.size:
            print(f"{k:<11} {col.min():+8.2f} {np.median(col):+8.2f} {col.max():+8.2f}")

    # ---- fault pass, per take ------------------------------------------------------
    # stored per (detector, take) so the gap can be computed take by take, not from an
    # average: an average hides exactly the case where one take moves less than the rest
    faulted = data.setdefault("faulted", {})
    for rid, (what, fn) in FAULTS.items():
        want = f"{rid}_faulted"
        if want in faulted and not args.refresh:
            got = faulted[want]
            print(f"\n{rid}: {what} (cached, {len(got)} takes)")
        else:
            got = {}
            print(f"\n{rid}: {what}")
            for f in files[:args.fault_takes]:
                x, sr = sf.read(f, dtype="float32", always_2d=True)
                x = x.mean(axis=1)
                tmp = os.path.join(OUT, "_warn.wav")
                sf.write(tmp, fn(x, sr).astype(np.float32), sr)
                got[os.path.basename(f)] = stats(args.cli, tmp)
            faulted[want] = got
            json.dump(data, open(CACHE, "w", encoding="utf-8"), indent=1)
        for name, s in got.items():
            before = clean[name]
            print(f"  {name[:26]:<28} " + "  ".join(
                f"{k} {s[k] - before[k]:+6.2f}" for k in ("sibp90", "sibexc", "airmiss",
                                                          "airslope", "noisefloor", "rumble")))
    try:
        os.remove(os.path.join(OUT, "_warn.wav"))
    except OSError:
        pass

    # ---- the strong pass, for a bar that has to sit beyond his own range -----------
    strong = data.setdefault("faulted_strong", {})
    for rid, (what, fn) in STRONG.items():
        want = f"{rid}_strong"
        if want in strong and not args.refresh:
            continue
        got = {}
        for f in files[:args.fault_takes]:
            x, sr = sf.read(f, dtype="float32", always_2d=True)
            x = x.mean(axis=1)
            tmp = os.path.join(OUT, "_warn.wav")
            sf.write(tmp, fn(x, sr).astype(np.float32), sr)
            got[os.path.basename(f)] = stats(args.cli, tmp)
        strong[want] = got
        json.dump(data, open(CACHE, "w", encoding="utf-8"), indent=1)
        print(f"  strong fault for {rid}: {what}")
    try:
        os.remove(os.path.join(OUT, "_warn.wav"))
    except OSError:
        pass

    # ---- which statistic survives both halves? -------------------------------------
    print(f"\n{'detector':<8} {'statistic':<24} {'worst clean':>11} {'best fault':>11} "
          f"{'gap':>7}  verdict")
    chosen: dict[str, tuple[str, float, int]] = {}
    readouts: dict[str, dict] = {}
    # The detectors that may be a NUMBER instead of a VERDICT.  A verdict is a promise
    # that something is wrong; it is only allowed when his own 15 mixes and a known fault
    # are separated by daylight.  Where they are not, the plugin measures, prints and
    # keeps quiet - which is the difference between a reading and a false alarm.
    MAY_BE_A_READOUT = {"sib", "harsh", "nasal"}
    for det in ("rumble", "sib", "dull", "noise", "harsh", "nasal"):
        cands = [c for c in CANDIDATES if c[0] == det]
        best = None
        for _, key, direction, name in cands:
            cs = np.array([e[key] for e in clean.values()])
            fs = np.array([e[key] for e in faulted.get(f"{det}_faulted", {}).values()])
            if fs.size == 0:
                continue
            if direction > 0:
                edge_clean, edge_fault = cs.max(), fs.min()
                gap = edge_fault - edge_clean
            else:
                edge_clean, edge_fault = cs.min(), fs.max()
                gap = edge_clean - edge_fault
            usable = gap >= MIN_GAP_DB
            print(f"{det:<8} {name:<24} {edge_clean:+11.2f} {edge_fault:+11.2f} "
                  f"{gap:+7.2f}  "
                  f"{'USABLE as a verdict' if usable else 'no bar exists for this quantity'}")
            if usable and best is None:
                best = (key, (edge_clean + edge_fault) / 2.0, direction)
            if best is None:
                last_edge, last_gap, last_key = edge_clean, gap, key
                last_dir = direction
        if best is not None:
            chosen[det] = best
            continue
        # the strongest fault of that kind, against his own worst
        sg = data.get("faulted_strong", {}).get(f"{det}_strong", {})
        reach = max((sg[n][last_key] for n in sg if n in clean), default=None)
        readouts[det] = {"key": last_key, "spread": float(np.ptp(
            [e[last_key] for e in clean.values()])), "gap": last_gap, "reach": reach}
        print(f"{'':<8} {'-> readout':<24} {'':>11} {'':>11} {'':>7}  his own mixes span "
              f"{readouts[det]['spread']:.1f} dB of it, and the weakest take under the "
              f"biggest fault tried only reaches "
              f"{'n/a' if reach is None else f'{reach:+.2f}'} - no verdict is claimed")

    bad = [d for d, r in readouts.items() if d not in MAY_BE_A_READOUT]
    if bad:
        print(f"\n  FAIL a detector that must be a verdict has no bar: {', '.join(bad)}")
        return 1
    missing = [d for d in ("rumble", "dull", "noise") if d not in chosen]
    if missing:
        print(f"\n  FAIL no usable statistic for: {', '.join(missing)}")
        return 1

    print("\nverdicts (midpoint of the daylight between his own mixes and a fault):")
    for det, (key, bar, direction) in chosen.items():
        print(f"  {det:<7} {key:<10} {bar:+8.2f}   ({'above' if direction > 0 else 'below'} "
              f"the bar is a fault)")
    print("readouts (measured and printed; his own range overlaps any fault, so no verdict):")
    for det, r in readouts.items():
        print(f"  {det:<7} {r['key']:<10} his range spans {r['spread']:5.1f} dB, "
              f"gap {r['gap']:+.1f} dB")
    loud = float(np.median([e["maxrms"] for e in clean.values()])) - 6.0
    print(f"  loud    (a silence floor means nothing below this) {loud:+8.2f} dBFS RMS")

    # ---- the de-esser's own automatic engagement ---------------------------------
    exc = np.array([e["sibexc"] for e in clean.values()])
    deess_auto = round(float(np.percentile(exc, 75)) + 4.0, 1)
    print(f"  de-ess  engages automatically above {deess_auto:+.1f} dB of sibilant excess "
          f"(his p75 {np.percentile(exc, 75):+.1f}, median {np.median(exc):+.1f}, "
          f"worst {exc.max():+.1f})")
    print(f"          that engages on {int((exc > deess_auto).sum())} of his {len(exc)} mixes, "
          f"capped at 1.5 dB of gain reduction")

    if args.write:
        with open(HEADER, "w", encoding="utf-8") as fh:
            fh.write(
                "// Generated by analysis/scripts/calibrate_warn.py - do not edit by hand.\n"
                "//\n"
                "// A detector may only raise its voice when the 15 dry stems that define\n"
                "// \"right\" and a known fault are separated by daylight.  Measured on those\n"
                "// stems with the shipped binary, three detectors pass and three do not:\n"
                "//\n"
                "//   verdict   rumble, dull (8-12k vs 2-4k), noise (silence floor)\n"
                "//   readout   sibilance, 2-4k harshness, 1 kHz nasality - his own mixes\n"
                "//             span more of each than any fault injected into them, so a\n"
                "//             threshold would be a guess that fires on his records\n"
                "//\n"
                "// The readouts are still measured and printed (STAT lines, the panel's\n"
                "// numbers); what they are not allowed to do is call a released mix wrong.\n"
                "#pragma once\n\nnamespace aeq {\n\nstruct WarnThresholds {\n")
            fh.write(f"    // rumble: 30-70 Hz over 300-700 Hz; above the bar is a fault\n"
                     f"    static constexpr double rumble = {chosen['rumble'][1]:.1f};\n")
            fh.write(f"    // dull: 8-12k over 2-4k inside the take; below the bar is a fault\n"
                     f"    static constexpr double airSlope = {chosen['dull'][1]:.1f};\n")
            fh.write(f"    // noise: silence floor in dBFS; above the bar is a fault\n"
                     f"    static constexpr double noiseFloor = {chosen['noise'][1]:.1f};\n")
            fh.write(f"    // a take quieter than this has no meaningful silence floor\n"
                     f"    static constexpr double loudEnough = {loud:.1f};\n")
            fh.write(f"    // the de-esser's own automatic engagement: his p75 + 4 dB, capped\n"
                     f"    // at 1.5 dB of gain reduction in RestoreCore\n"
                     f"    static constexpr double deEssAuto = {deess_auto:.1f};\n")
            fh.write("};\n\n} // namespace aeq\n")
        print(f"\nwrote {HEADER}")
    else:
        print("\n(no --write: header untouched)")
    print("\nWARN CALIBRATION OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
