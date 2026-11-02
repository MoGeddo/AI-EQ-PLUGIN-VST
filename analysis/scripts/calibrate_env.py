#!/usr/bin/env python3
"""
calibrate_env.py - derive the envelope thresholds from the plugin itself.

The rule in this project is that a threshold is the *outer edge of the engineer's
own work*: the biggest deviation any of his 15 mixes shows against the average of
his 15 mixes.  Whatever the measurement is, the bar moves with it.

That rule has to be applied on the C++ numbers, not on the Python ones.  The two
implementations agree on the algorithm (that is gate G8) but they do not measure
the same audio: the CLI analyses the whole take, the Python reference the first
90 seconds, and their VADs and band grids differ slightly.  On the first take the
envelope excess in 260-520 Hz is +2.02 dB in Python and +2.95 dB in C++ - enough
to turn a "no false alarms" design into 3 of 15 flagged.  So: run the shipped
binary, read its own EXCESS lines, and write the constants from those.

Output: plugin/Source/model/EnvThresholds.h  (+ a table on stdout)

Usage:  .venv/bin/python analysis/scripts/calibrate_env.py [--wav dir] [--cli path]
"""
import argparse
import glob
import json
import os
import subprocess

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "analysis", "out", "amir15")
CACHE = os.path.join(OUT, "calibrate_env.json")

# the regions the detector scores, and whether the envelope is used there
REGIONS = {"boom": (110, 260), "mud": (260, 520), "box": (520, 850),
           "nasal": (850, 1300), "harsh": (2000, 4200)}
ENV_REGIONS = ("mud", "box")      # the only two the corpus can measure better

MARGIN_DB = 0.15      # sit just above the loudest of his own mixes
INJECT_DB, INJECT_Q = 3.0, 2.0


def parse(out):
    excess = {}
    for line in out.splitlines():
        if line.startswith("EXCESS "):
            _, rid, _, raw, _, env = line.split()
            excess[rid] = (float(raw), float(env))
    return excess


def run_cli(cli, path):
    r = subprocess.run([cli, "analyze", path], capture_output=True, text=True)
    return parse(r.stdout)


def peak(x, sr, f, q, db):
    w0 = 2 * np.pi * f / sr
    a = np.sin(w0) / (2 * q)
    A = 10 ** (db / 40.0)
    return lfilter([1 + a * A, -2 * np.cos(w0), 1 - a * A],
                   [1 + a / A, -2 * np.cos(w0), 1 - a / A], x)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default="/tmp/wav15")
    ap.add_argument("--cli", default=os.path.join(ROOT, "build", "aeq_cli"))
    ap.add_argument("--inject-takes", type=int, default=3)
    ap.add_argument("--refresh", action="store_true")
    args = ap.parse_args()

    cli = args.cli
    if not os.path.exists(cli):
        raise SystemExit(f"build it first: g++ -std=c++17 -O2 -Iplugin/Source "
                         f"plugin/cli/main.cpp -o {cli}")
    files = sorted(glob.glob(os.path.join(args.wav, "*.wav")))
    if not files:
        raise SystemExit(f"no wav files in {args.wav}")

    if os.path.exists(CACHE) and not args.refresh:
        base = json.load(open(CACHE, encoding="utf-8"))
        print(f"clean pass from cache ({len(base['clean'])} takes); --refresh to redo")
    else:
        base = {"clean": {}, "inject": {}}
        print(f"clean pass: {len(files)} takes through {os.path.basename(cli)}")
        for f in files:
            base["clean"][os.path.basename(f)] = run_cli(cli, f)

    # ---- the bar: the outer edge of his own work, per region --------------
    print(f"\n{'region':<7} {'raw max':>8} {'env max':>8} {'raw med':>8} {'env med':>8}  used")
    thr = {}
    for rid, (lo, hi) in REGIONS.items():
        raw = np.array([e[rid][0] for e in base["clean"].values()])
        env = np.array([e[rid][1] for e in base["clean"].values()])
        raw_ok, env_ok = raw[raw > -49], env[env > -49]     # -50 = not measurable
        thr[rid] = {
            "raw_max": float(raw_ok.max()) if raw_ok.size else None,
            "env_max": float(env_ok.max()) if env_ok.size else None,
            "raw_med": float(np.median(raw_ok)) if raw_ok.size else None,
            "env_med": float(np.median(env_ok)) if env_ok.size else None,
            "n_raw": int(raw_ok.size), "n_env": int(env_ok.size),
        }
        t = thr[rid]
        fmt = lambda v: "   n/a " if v is None else f"{v:+8.2f}"
        print(f"{rid:<7} {fmt(t['raw_max'])} {fmt(t['env_max'])} {fmt(t['raw_med'])} "
              f"{fmt(t['env_med'])}  {'env' if rid in ENV_REGIONS else 'raw'}"
              f"   ({t['n_env']}/{len(files)} measurable on the envelope)")

    # ---- does the tighter bar still catch a +3 dB fault? -----------------
    if not base["inject"]:
        print(f"\nfault injection: +{INJECT_DB} dB Q{INJECT_Q} at the region centre")
        for rid in ENV_REGIONS:
            lo, hi = REGIONS[rid]
            centre = float(np.sqrt(lo * hi))
            resp = {"env": [], "raw": []}
            for f in files[:args.inject_takes]:
                x, sr = sf.read(f, dtype="float32", always_2d=True)
                x = x.mean(axis=1)
                tmp = os.path.join(OUT, "_cal.wav")
                sf.write(tmp, peak(x, sr, centre, INJECT_Q, INJECT_DB).astype(np.float32), sr)
                bad = run_cli(cli, tmp)
                clean = base["clean"][os.path.basename(f)]
                resp["env"].append(bad[rid][1] - clean[rid][1])
                resp["raw"].append(bad[rid][0] - clean[rid][0])
            base["inject"][rid] = {k: float(np.mean(v)) for k, v in resp.items()}
            print(f"  {rid:<5} @ {centre:4.0f} Hz -> env sees {base['inject'][rid]['env']:+.2f} dB, "
                  f"raw sees {base['inject'][rid]['raw']:+.2f} dB")
        try:
            os.remove(os.path.join(OUT, "_cal.wav"))
        except OSError:
            pass
        json.dump(base, open(CACHE, "w", encoding="utf-8"), indent=1)

    # ---- thresholds and detectability -----------------------------------
    print(f"\n{'region':<7} {'bar':>6} {'fault resp':>11} {'margin':>8}   verdict")
    final = {}
    gains = {}
    for rid in ENV_REGIONS:
        t = thr[rid]
        if t["env_max"] is None:
            raise SystemExit(f"{rid}: the corpus cannot measure the envelope here")
        bar = round(t["env_max"] + MARGIN_DB, 1)
        resp = base["inject"].get(rid, {}).get("env")
        final[rid] = {"thr": bar, "resp": resp,
                      "raw_thr": t["raw_max"], "raw_resp": base["inject"].get(rid, {}).get("raw")}
        if resp is None:
            verdict = "(no injection data)"
        else:
            verdict = ("clear" if resp > bar else "does NOT clear the bar") + \
                      f" (resp/bar = {resp / bar:.2f})"
        print(f"{rid:<7} {bar:6.2f} {0 if resp is None else resp:11.2f} "
              f"{0 if resp is None else resp - bar:+8.2f}   {verdict}")
        # the honest comparison: a fault is "detected" when its response clears the
        # bar, so what matters is resp/bar in each domain - not the raw response.
        raw_thr = thr[rid]["raw_max"]
        raw_resp = base["inject"].get(rid, {}).get("raw")
        if resp and raw_resp and raw_thr:
            gains[rid] = (resp / bar) / (raw_resp / raw_thr)
    if gains:
        txt = ", ".join(f"{k} {v:.1f}x" for k, v in gains.items())
        print(f"\nSUMMARY envelope-domain gain: {txt}")
        print("  (gain = how much closer a known +3 dB fault comes to its own bar in the")
        print("   envelope domain than in the raw domain.  >1 means the envelope decision")
        print("   is the more sensitive one.  The fault still does not clear either bar at")
        print("   region level - that is the argument for scoring single bands, not for")
        print("   abandoning the envelope.)")
        weak = {k: v for k, v in gains.items() if v < 1.5}
        if weak:
            print(f"GATE FAIL: the envelope is not the more sensitive decision here: {weak}")
            raise SystemExit(1)
        print("GATE PASS: every envelope-scored region is at least 1.5x closer to its bar")

    # ---- write the header -------------------------------------------------
    header = os.path.join(ROOT, "plugin", "Source", "model", "EnvThresholds.h")
    with open(header, "w", encoding="utf-8") as fh:
        fh.write("// Generated by analysis/scripts/calibrate_env.py - do not edit by hand.\n"
                 "//\n"
                 "// The bar for an envelope-domain region is the largest deviation any of the 15\n"
                 "// reference mixes shows against the average of the same 15, measured by THIS\n"
                 "// binary, plus 0.15 dB.  Derived here rather than typed by hand because the\n"
                 "// C++ and the Python reference do not measure identical audio (whole take vs\n"
                 "// first 90 s, different VADs): the same band reads +2.02 dB in Python and\n"
                 "// +2.95 dB in C++, which is the difference between zero false alarms and\n"
                 "// three of fifteen.\n"
                 "//\n"
                 "// Together with the threshold, the file records what a known fault does to\n"
                 "// the same measurement, so a future change cannot quietly trade one for the\n"
                 "// other.  Re-run the script after touching the analyser.\n"
                 "#pragma once\n\nnamespace aeq {\n\nstruct EnvThresholds {\n")
        for rid, v in final.items():
            fh.write(f"    // {rid}: clean max {thr[rid]['env_max']:+.2f} dB, "
                     f"+{INJECT_DB} dB Q{INJECT_Q} fault moves it "
                     f"{'n/a' if v['resp'] is None else format(v['resp'], '+.2f')} dB "
                     f"(raw bar {thr[rid]['raw_max']:+.2f})\n")
            fh.write(f"    static constexpr double {rid} = {v['thr']:.1f};\n")
        fh.write("};\n\n} // namespace aeq\n")
    print(f"\nwrote {header}")
    print(f"wrote {CACHE}")


if __name__ == "__main__":
    main()
