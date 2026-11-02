#!/usr/bin/env python3
"""
env_target.py - build the reference and the tolerance in the *envelope* domain.

Why this file has to exist before the decision layer can use the envelope:

the plugin compares a measured level with a target curve, and calls a difference
a fault only when it is bigger than the engineer's own spread (the "tolerance").
Both of those numbers were measured on the raw spectrum.  An envelope level is a
different coordinate - 14 dB lower at 200 Hz on the same file, 0.5 dB lower at
1.5 kHz (docs/14 section 4) - so swapping the measurement while keeping the old
target would report a constant fake "deficit" instead of a real problem.

So: measure the same 15 mixes in the envelope domain, and derive

  * target   = the average of his envelope curves (what "balanced" looks like
               when you look at the vocal tract instead of the comb)
  * tolerance = the spread between his 15 mixes in that same coordinate

and prove, by injecting a known +4 dB fault, that the tighter number did not
cost us the ability to see a fault.

Output:
  analysis/out/amir15/env_target.json   every number used by the C++ constants
  analysis/out/amir15/env_target.csv    the two curves, human-readable
  plugin/Source/model/EnvTargetCurve.h  the table the plugin compiles in

Usage:  .venv/bin/python analysis/scripts/env_target.py [--wav dir]
"""
import argparse
import glob
import json
import os

import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "analysis", "out", "amir15")

KFFT, KHOP = 8192, 2048
LIFTER_S = 0.0035          # 3.5 ms
NOTCH_W = 0.00018          # rahmonic notch width, seconds (fixed, not scaled by F0)
SECONDS = 90               # same excerpt length as envelope_demo.py
ACTIVE_DB = -45.0          # frame must be this loud to count (same as the demo)
UNDEFINED = 999.0          # sentinel: "the corpus cannot measure this band"

# 1/12-octave grid, same geometry as TargetCurve.h (100 Hz, 12 points/octave)
FMIN, PPOCT, NPOINTS = 100.0, 12.0, 86
CENTRES = FMIN * 2.0 ** (np.arange(NPOINTS) / PPOCT)
EDGE = 2.0 ** (1.0 / (2.0 * PPOCT))
BLO = CENTRES / EDGE
BHI = CENTRES * EDGE

REGIONS = [(110, 260), (260, 520), (520, 850), (850, 1300), (2000, 4200)]


def frame_curves(path, seconds=SECONDS):
    """per-band envelope and raw levels for one take

    Returns dict of arrays:
      env_db[song band]  level re the 900-1150 Hz reference, envelope domain
      env_ok[song band]  fraction of frames where the band is above 0.9*F0
      raw_db[song band]  same in the raw domain (power average, as the plugin does)
      f0[song]           median F0
    """
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    x = x.mean(axis=1)[: int(seconds * sr)]
    df = sr / KFFT
    freqs = np.fft.rfftfreq(KFFT, 1.0 / sr)
    q = np.arange(KFFT) / sr
    win = np.hanning(KFFT)

    blo = np.clip(np.floor(BLO / df).astype(int), 1, len(freqs) - 1)
    bhi = np.clip(np.ceil(BHI / df).astype(int), 1, len(freqs) - 1)

    env_sum = np.zeros(NPOINTS)
    env_cnt = np.zeros(NPOINTS)
    raw_sum = np.zeros(len(freqs))
    nframe = 0
    f0s = []
    for i in range(0, len(x) - KFFT, KHOP):
        seg = x[i:i + KFFT]
        if np.sqrt(np.mean(seg ** 2)) < 10 ** (ACTIVE_DB / 20.0):
            continue
        p = np.log(np.abs(np.fft.rfft(seg * win)) ** 2 + 1e-20)
        raw_sum += p
        nframe += 1

        c = np.fft.irfft(p, n=KFFT)
        m = (q > 1.0 / 600.0) & (q < 1.0 / 70.0)
        f0 = 1.0 / q[m][np.argmax(c[m])]
        f0s.append(f0)

        c2 = c.copy()
        for k in (1, 2, 3, 4):
            c2 -= (np.exp(-0.5 * ((q - k / f0) / NOTCH_W) ** 2)
                   + np.exp(-0.5 * ((q + k / f0) / NOTCH_W) ** 2)) * c
        c2[q > LIFTER_S] = 0.0
        env = np.fft.rfft(c2, n=KFFT).real     # ln-power per bin, already 1/N normalised

        for b in range(NPOINTS):
            if CENTRES[b] < 0.9 * f0:          # same guard as the C++ analyser
                continue
            # mean of ln, i.e. dB of the geometric mean: the envelope is a smooth
            # log curve, so this is the natural band average.  The C++ does the
            # same operation (Analyzer::envelopeOfFrame) - kept identical on purpose.
            env_sum[b] += env[blo[b]:bhi[b] + 1].mean()
            env_cnt[b] += 1

    raw = raw_sum / max(nframe, 1)
    # raw band level = power average of the bins in the band (what the plugin does)
    raw_db = np.array([10.0 * np.log10(np.mean(np.exp(raw[blo[b]:bhi[b] + 1])) + 1e-30)
                       for b in range(NPOINTS)])
    env_db = np.where(env_cnt > 0, env_sum / np.maximum(env_cnt, 1) * 4.342944819, np.nan)

    ref_m = (CENTRES >= 900.0) & (CENTRES < 1150.0)
    ref_raw = 10.0 * np.log10(np.mean(10.0 ** (raw_db[ref_m] / 10.0)))
    ref_env_vals = env_db[ref_m][~np.isnan(env_db[ref_m])]
    ref_env = float(np.mean(ref_env_vals)) if ref_env_vals.size else np.nan

    return {"env_db": env_db - ref_env, "env_ok": env_cnt / max(nframe, 1),
            "raw_db": raw_db - ref_raw, "f0": float(np.median(f0s)) if f0s else 0.0,
            "frames": nframe}


def excess(curve, target, ok_mask, lo, hi, floor_f0):
    """region excess, the same arithmetic as ProblemDetector::excessVsTarget

    Bands below `floor_f0` are dropped (unmeasurable / register-dominated), and a
    region with fewer than 4 usable bands returns None = "not measurable".
    """
    sel = (CENTRES >= max(lo, floor_f0)) & (CENTRES < hi) & ok_mask
    if sel.sum() < 4:
        return None
    return float(np.mean(curve[sel] - target[sel]))


def inject(x, sr, f_hz, q=0.8, db=4.0):
    from scipy.signal import lfilter
    w0 = 2 * np.pi * f_hz / sr
    a = np.sin(w0) / (2 * q)
    A = 10 ** (db / 40.0)
    return lfilter([1 + a * A, -2 * np.cos(w0), 1 - a * A],
                   [1 + a / A, -2 * np.cos(w0), 1 - a / A], x)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default="/tmp/wav15", help="folder of 15 vocal wavs")
    ap.add_argument("--inject-songs", type=int, default=3)
    ap.add_argument("--no-write-header", action="store_true")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.wav, "*.wav")))
    print(f"{len(files)} takes from {args.wav}")
    per = [frame_curves(f) for f in files]
    names = [os.path.basename(f).split("_-_")[0][:24] for f in files]

    env = np.array([c["env_db"] for c in per])      # songs x bands, NaN where invalid
    ok = np.array([c["env_ok"] for c in per])
    raw = np.array([c["raw_db"] for c in per])
    f0 = np.array([c["f0"] for c in per])
    valid = ok > 0.5                                 # a band counts for a song at >50% frames

    print(f"median F0 per take: {np.round(f0, 0).astype(int).tolist()}")

    # --- target and tolerance in each domain -----------------------------
    tv = np.where(valid, env, np.nan)
    n_env = valid.sum(axis=0)
    with np.errstate(invalid="ignore"):
        target_env = np.nanmean(tv, axis=0) if n_env.any() else np.full(NPOINTS, np.nan)
        sd_env = np.nanstd(tv, axis=0, ddof=1) if (n_env > 1).any() else np.full(NPOINTS, np.nan)
    target_raw = raw.mean(axis=0)
    sd_raw = raw.std(axis=0, ddof=1)

    defined = n_env >= 8                             # at least half the corpus
    target_env[~defined] = UNDEFINED
    sd_env[~defined] = UNDEFINED
    # a band only contributes to a region score if the corpus defines it AND this
    # take actually measured it; without that mask the 999 sentinel reads as a
    # +707 dB "excess" and poisons every region that touches an undefined band
    defined_any = defined.copy()
    print(f"envelope target defined for {int(defined.sum())}/{NPOINTS} bands, "
          f"from {CENTRES[defined][0]:.0f} Hz up" if defined.any() else "nothing defined")

    # --- what each song looks like against that target --------------------
    res = {"regions": {}, "f0": np.round(f0, 1).tolist()}
    for lo, hi in REGIONS:
        e_x, r_x, e_floor, r_floor = [], [], [], []
        for s in range(len(files)):
            e = excess(tv[s], target_env, valid[s] & defined_any, lo, hi, 0.9 * f0[s])
            r = excess(raw[s], target_raw, np.ones(NPOINTS, bool), lo, hi, 1.5 * f0[s])
            e_x.append(np.nan if e is None else e)
            r_x.append(np.nan if r is None else r)
            e_floor.append(0.9 * f0[s])
            r_floor.append(1.5 * f0[s])
        e_x, r_x = np.array(e_x), np.array(r_x)
        key = f"{lo}-{hi}"
        # threshold = the outer edge of his own work: the largest deviation any of
        # the 15 mixes shows against the average of the 15.  By construction this
        # cannot flag the reference material; what it *can* still do is miss a real
        # fault, which is why the injection test below sits right next to it.
        thr_env = float(np.nanmax(np.abs(e_x))) if np.isfinite(e_x).any() else None
        thr_raw = float(np.nanmax(np.abs(r_x))) if np.isfinite(r_x).any() else None
        res["regions"][key] = {
            "env_defined": bool(defined[(CENTRES >= lo) & (CENTRES < hi)].any()),
            "env_spread": round(float(np.nanmedian(sd_env[(CENTRES >= lo) & (CENTRES < hi)])), 2)
            if defined.any() else None,
            "raw_spread": round(float(np.median(sd_raw[(CENTRES >= lo) & (CENTRES < hi)])), 2),
            "env_max_excess": None if thr_env is None else round(thr_env, 2),
            "raw_max_excess": None if thr_raw is None else round(thr_raw, 2),
            "n_env": int(np.isfinite(e_x).sum()), "n_raw": int(np.isfinite(r_x).sum()),
        }
        print(f"  {key:>9} Hz  spread env {res['regions'][key]['env_spread']} / "
              f"raw {res['regions'][key]['raw_spread']} dB   "
              f"max |excess| env {res['regions'][key]['env_max_excess']} / "
              f"raw {res['regions'][key]['raw_max_excess']}  "
              f"(measurable: env {res['regions'][key]['n_env']}/15, raw {res['regions'][key]['n_raw']}/15)")

    # --- did the tightening cost us the ability to see a fault? -----------
    print("\nfault injection: +4 dB @ region centre, same take, both domains")
    inj = {}
    for lo, hi in REGIONS:
        centre = float(np.sqrt(lo * hi))
        region_defined = bool(defined[(CENTRES >= lo) & (CENTRES < hi)].any())
        resp_e, resp_r = [], []
        for path in files[:args.inject_songs]:
            x, sr = sf.read(path, dtype="float32", always_2d=True)
            x = x.mean(axis=1)
            tmp = os.path.join(OUT, "_inj.wav")
            sf.write(tmp, x.astype(np.float32), sr)
            base = frame_curves(tmp, seconds=45)
            sf.write(tmp, inject(x, sr, centre).astype(np.float32), sr)
            bad = frame_curves(tmp, seconds=45)
            # the fault is injected everywhere, so validity does not change; score
            # the delta on the bands the corpus defines
            vmask = defined_any & (ok[:args.inject_songs].mean(axis=0) > 0.5)
            be = excess(bad["env_db"], base["env_db"], vmask, lo, hi, 0.9 * base["f0"])
            ba = excess(base["env_db"], base["env_db"], vmask, lo, hi, 0.9 * base["f0"])
            br = excess(bad["raw_db"], base["raw_db"], np.ones(NPOINTS, bool), lo, hi,
                        1.5 * base["f0"])
            ar = excess(base["raw_db"], base["raw_db"], np.ones(NPOINTS, bool), lo, hi,
                        1.5 * base["f0"])
            if be is not None and ba is not None:
                resp_e.append(be - ba)
            if br is not None and ar is not None:
                resp_r.append(br - ar)
        try:
            os.remove(os.path.join(OUT, "_inj.wav"))
        except OSError:
            pass
        sd_e = res["regions"][f"{lo}-{hi}"]["env_spread"]
        sd_r = res["regions"][f"{lo}-{hi}"]["raw_spread"]
        re = float(np.mean(resp_e)) if (resp_e and region_defined) else None
        rr = float(np.mean(resp_r)) if resp_r else None
        inj[f"{lo}-{hi}"] = {
            "response_env": None if re is None else round(re, 2),
            "response_raw": None if rr is None else round(rr, 2),
            "detectability_env": None if (re is None or not sd_e) else round(re / sd_e, 2),
            "detectability_raw": None if (rr is None or not sd_r) else round(rr / sd_r, 2),
        }
        d = inj[f"{lo}-{hi}"]
        print(f"  {lo:>4}-{hi:<5} Hz  response env {d['response_env']} / raw {d['response_raw']} dB"
              f"   detectability env {d['detectability_env']} / raw {d['detectability_raw']}")
    res["injection"] = inj

    # --- write --------------------------------------------------------------
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "env_target.json"), "w", encoding="utf-8") as fh:
        json.dump({"freqs": CENTRES.tolist(), "target_env_db": np.where(
                       defined, np.round(target_env, 3), None).tolist(),
                   "sd_env_db": np.where(defined, np.round(sd_env, 3), None).tolist(),
                   "target_raw_db": np.round(target_raw, 3).tolist(),
                   "sd_raw_db": np.round(sd_raw, 3).tolist(),
                   "n_env": n_env.tolist(), "names": names, **res},
                  fh, indent=1, default=lambda o: None)
    with open(os.path.join(OUT, "env_target.csv"), "w", encoding="utf-8") as fh:
        fh.write("freq_hz,target_env_db,sd_env_db,target_raw_db,sd_raw_db,n_songs\n")
        for i in range(NPOINTS):
            fh.write(f"{CENTRES[i]:.1f},{target_env[i]:.3f},{sd_env[i]:.3f},"
                     f"{target_raw[i]:.3f},{sd_raw[i]:.3f},{n_env[i]}\n")

    if not args.no_write_header:
        header = os.path.join(ROOT, "plugin", "Source", "model", "EnvTargetCurve.h")
        with open(header, "w", encoding="utf-8") as fh:
            fh.write("// Generated from analysis/out/amir15/env_target.csv - do not edit by hand.\n"
                     "// The same 15 mixes, measured on the HARMONIC ENVELOPE instead of the raw\n"
                     "// spectrum: the vocal-tract curve with the harmonic ladder removed.  Level is\n"
                     "// re the 900-1150 Hz reference, in the same units as TargetCurve.h, so the two\n"
                     "// are not interchangeable - an envelope level sits ~14 dB below the raw level at\n"
                     "// 200 Hz and ~0.5 dB below at 1.5 kHz (docs/14 section 4).\n"
                     "//\n"
                     "// Bands the corpus cannot measure (fewer than 8 of 15 takes have the band\n"
                     "// above 0.9*F0 for most frames) carry kUndefined: a monophonic voice has no\n"
                     "// energy below its fundamental, and inventing a target there would be a lie.\n"
                     "#pragma once\n#include <array>\n#include <cmath>\n#include <algorithm>\n\n"
                     "namespace aeq {\n\nstruct EnvTargetCurve {\n"
                     "    static constexpr int    kPoints      = 86;\n"
                     "    static constexpr double kFmin        = 100.0;\n"
                     "    static constexpr double kPointsPerOct = 12.0;\n"
                     "    static constexpr float  kUndefined   = 999.0f;\n\n")
            for name, arr in (("level", target_env), ("sd", sd_env)):
                fh.write(f"    static const std::array<float, kPoints>& {name}() {{\n"
                         f"        static const std::array<float, kPoints> v {{\n")
                vals = [f"{v:9.3f}f" for v in arr]
                for i in range(0, NPOINTS, 6):
                    fh.write("     " + ", ".join(vals[i:i + 6]) + ",\n")
                fh.write("        };\n        return v;\n    }\n\n")
            fh.write("""    static bool definedAt(double f) {
        const int i = indexOf(f);
        return i >= 0 && level()[i] < kUndefined * 0.5f;
    }
    static int indexOf(double f) {
        const double x = std::log2(std::max(f, 1.0) / kFmin) * kPointsPerOct;
        const int i = static_cast<int>(std::lround(x));
        return (i >= 0 && i < kPoints) ? i : -1;
    }
    // linear in log2(f), same interpolation as TargetCurve::at.  Returns kUndefined
    // where the corpus has no measurement, so callers must check definedAt().
    static double at(double f) { return interp(level(), f); }
    static double sdAt(double f) { return interp(sd(), f); }
private:
    template <typename T>
    static double interp(const T& v, double f) {
        const double x = std::log2(std::max(f, 1.0) / kFmin) * kPointsPerOct;
        if (x <= 0.0) return v.front();
        if (x >= kPoints - 1) return v.back();
        const int i = static_cast<int>(x);
        const double t = x - i;
        return v[i] * (1.0 - t) + v[i + 1] * t;
    }
};

} // namespace aeq
""")
        print(f"\nwrote {header}")
    print(f"wrote {os.path.join(OUT, 'env_target.json')} and .csv")


if __name__ == "__main__":
    main()
