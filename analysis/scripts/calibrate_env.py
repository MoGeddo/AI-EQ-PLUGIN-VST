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
REGIONS = {"mud": (260, 520), "box": (520, 850),
           "nasal": (850, 1300), "harsh": (2000, 4200)}
ENV_REGIONS = ("mud", "box")      # the only fixed regions the corpus measures better
TILT = "boomtilt"                 # pitch-relative: low 0.9-2.5 F0 minus body 3-8 F0

MARGIN_DB = 0.15      # sit just above the loudest of his own mixes
INJECT_DB, INJECT_Q = 3.0, 2.0


def parse(out):
    excess = {}
    for line in out.splitlines():
        if line.startswith("median F0"):
            excess["_f0"] = float(line.split(":")[1].split()[0])
    for line in out.splitlines():
        if line.startswith("EXCESS "):
            _, rid, _, raw, _, env = line.split()
            excess[rid] = (float(raw), float(env))
    return excess


def run_cli(cli, path):
    r = subprocess.run([cli, "analyze", path], capture_output=True, text=True)
    return parse(r.stdout)


def f0_of(cli, path):
    return run_cli(cli, path).get("_f0", 0.0)


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
    print(f"\n{'region':<9} {'raw max':>8} {'env max':>8} {'raw med':>8} {'env med':>8}  used")
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
        print(f"{rid:<9} {fmt(t['raw_max'])} {fmt(t['env_max'])} {fmt(t['raw_med'])} "
              f"{fmt(t['env_med'])}  {'env' if rid in ENV_REGIONS else 'raw'}"
              f"   ({t['n_env']}/{len(files)} measurable on the envelope)")

    tilts = np.array([e[TILT][1] for e in base["clean"].values()])
    tilts_ok = tilts[tilts > -49]
    print(f"{'boomtilt':<9} {'   n/a ':>8} {np.max(tilts_ok) if tilts_ok.size else float('nan'):+8.2f} "
          f"{'   n/a ':>8} {np.median(tilts_ok) if tilts_ok.size else float('nan'):+8.3f}  tilt"
          f"   ({tilts_ok.size}/{len(files)} measurable - the old fixed band was 0/{len(files)})")

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
        # the tilt's fault has to sit inside ITS window, which moves with the singer:
        # a +3 dB bell at 1.5*F0, not at a fixed frequency
        resp = []
        for f in files[:args.inject_takes]:
            f0 = base["clean"][os.path.basename(f)].get("_f0", 0.0)
            if not (55 < f0 < 700):
                continue
            x, sr = sf.read(f, dtype="float32", always_2d=True)
            x = x.mean(axis=1)
            tmp = os.path.join(OUT, "_cal.wav")
            sf.write(tmp, peak(x, sr, 1.5 * f0, INJECT_Q, INJECT_DB).astype(np.float32), sr)
            bad = run_cli(cli, tmp)
            resp.append(bad[TILT][1] - base["clean"][os.path.basename(f)][TILT][1])
            print(f"  boomtilt @ {1.5 * f0:4.0f} Hz (1.5 x F0 {f0:.0f}) -> "
                  f"tilt moves {resp[-1]:+.2f} dB")
        base["inject"][TILT] = {"env": float(np.mean(resp)) if resp else None}
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

    # ---- the pitch-relative region ---------------------------------------
    for rid in (TILT,):
        if tilts_ok.size == 0:
            raise SystemExit("boomtilt: not measurable on the corpus")
        bar = round(float(tilts_ok.max()) + MARGIN_DB, 1)
        resp = base["inject"].get(TILT, {}).get("env")
        final[rid] = {"thr": bar, "resp": resp, "raw_thr": None, "raw_resp": None}
        print(f"{rid:<9} {bar:6.2f} {0 if resp is None else resp:11.2f} "
              f"{0 if resp is None else resp - bar:+8.2f}   "
              f"{'measurable' if resp is not None else '(no injection data)'}")

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
            resptxt = "n/a" if v["resp"] is None else format(v["resp"], "+.2f")
            if rid in thr:
                src = thr[rid]["env_max"]
                note = (f"clean max {src:+.2f} dB, +{INJECT_DB} dB Q{INJECT_Q} fault moves it "
                        f"{resptxt} dB (raw bar {thr[rid]['raw_max']:+.2f})")
            else:
                src = float(np.max(tilts_ok))
                note = (f"pitch-relative tilt (low 0.9-2.5 F0 minus body 3-8 F0): "
                        f"clean max {src:+.2f} dB, fault at 1.5 F0 moves it {resptxt} dB")
            fh.write(f"    // {rid}: {note}\n")
            name = "boom_tilt" if rid == TILT else rid
            fh.write(f"    static constexpr double {name} = {v['thr']:.1f};\n")
        fh.write("};\n\n} // namespace aeq\n")
    print(f"\nwrote {header}")
    print(f"wrote {CACHE}")


if __name__ == "__main__":
    main()
