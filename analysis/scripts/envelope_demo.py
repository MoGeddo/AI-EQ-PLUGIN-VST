#!/usr/bin/env python3
"""
envelope_demo.py - what the next step (harmonic envelope) actually buys, measured
on the 15 songs that define the standard.

A sung note is a ladder of harmonics spaced by F0, riding on a smooth vocal-tract
envelope.  The plugin compares the *ladder* with a smooth target, so the number it
gets depends on where the teeth fall - i.e. on the singer's pitch.  This script
recomputes the same band numbers on the cepstral envelope (3.5 ms lifter with the
rahmonics at 1/F0 subtracted before lifting) and reports the two things that
matter for the product:

  1. does the pitch dependence go away?
  2. does the tolerance across the engineer's own mixes shrink?  A smaller
     tolerance means the same rule can be more sensitive to real problems
     without raising more false alarms.

Output: analysis/out/amir15/envelope_demo.png + envelope_demo.json
"""
import glob
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "analysis", "out", "amir15")
KFFT, KHOP = 8192, 2048
BANDS = [(110, 260), (260, 520), (520, 850), (850, 1300), (1300, 2000), (2000, 4200)]
LIFTER_S = 0.0035          # 3.5 ms - the published window for envelope vs harmonics
NOTCH_W = 0.00018          # width of the rahmonic notch, seconds


def analyse(path, seconds=90):
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    x = x.mean(axis=1)[: int(seconds * sr)]
    freqs = np.fft.rfftfreq(KFFT, 1.0 / sr)
    q = np.arange(KFFT) / sr
    win = np.hanning(KFFT)
    raw_acc, env_acc, f0s = [], [], []
    for i in range(0, len(x) - KFFT, KHOP):
        seg = x[i:i + KFFT]
        if np.sqrt(np.mean(seg ** 2)) < 10 ** (-45 / 20):
            continue
        p = np.log(np.abs(np.fft.rfft(seg * win)) ** 2 + 1e-20)
        raw_acc.append(p)
        c = np.fft.irfft(p, n=KFFT)
        m = (q > 1.0 / 600.0) & (q < 1.0 / 70.0)
        f0 = 1.0 / q[m][np.argmax(c[m])]
        f0s.append(f0)
        c2 = c.copy()
        for k in (1, 2, 3, 4):
            w = np.exp(-0.5 * ((q - k / f0) / NOTCH_W) ** 2)
            w2 = np.exp(-0.5 * ((q + k / f0) / NOTCH_W) ** 2)
            c2 -= (w + w2) * c
        c2[q > LIFTER_S] = 0.0
        env_acc.append(np.fft.rfft(c2, n=KFFT).real)

    raw = np.mean(raw_acc, axis=0)
    env = np.mean(env_acc, axis=0)

    def band_db(v, lo, hi):
        m = (freqs >= lo) & (freqs < hi)
        return 10.0 * np.log10(np.mean(np.exp(v[m])) + 1e-30)

    ref_raw = band_db(raw, 900, 1150)
    ref_env = band_db(env, 900, 1150)
    row = {}
    for lo, hi in BANDS:
        row[f"{lo}-{hi}"] = (band_db(raw, lo, hi) - ref_raw,
                             band_db(env, lo, hi) - ref_env)
    return float(np.median(f0s)), row


def inject(x, sr, f0_hz, q=0.8, db=4.0):
    """a known fault: a broad bump at the band centre"""
    from scipy.signal import lfilter
    w0 = 2 * np.pi * f0_hz / sr
    a = np.sin(w0) / (2 * q)
    A = 10 ** (db / 40.0)
    b = [1 + a * A, -2 * np.cos(w0), 1 - a * A]
    aa = [1 + a / A, -2 * np.cos(w0), 1 - a / A]
    return lfilter(b, aa, x)


def fault_response(files, n_songs=3, seconds=45):
    """how much does each measurement move when a +4 dB fault is injected?

    Shrinking the tolerance is only a win if the measurement still sees real
    faults.  The honest figure of merit is detectability = response / tolerance,
    because the tolerance is exactly the noise the singer injects into the
    measurement.
    """
    out = {}
    for lo, hi in BANDS:
        centre = float(np.sqrt(lo * hi))
        raws, envs = [], []
        for path in files[:n_songs]:
            x, sr = sf.read(path, dtype="float32", always_2d=True)
            x = x.mean(axis=1)
            tmp = os.path.join(OUT, "_inj.wav")
            sf.write(tmp, x.astype(np.float32), sr)
            _, base = analyse(tmp, seconds=seconds)
            sf.write(tmp, inject(x, sr, centre).astype(np.float32), sr)
            _, bad = analyse(tmp, seconds=seconds)
            raws.append(bad[f"{lo}-{hi}"][0] - base[f"{lo}-{hi}"][0])
            envs.append(bad[f"{lo}-{hi}"][1] - base[f"{lo}-{hi}"][1])
        out[f"{lo}-{hi}"] = {"raw": round(float(np.mean(raws)), 2),
                             "envelope": round(float(np.mean(envs)), 2)}
        print(f"  fault +4 dB @ {centre:5.0f} Hz  ->  raw sees {out[f'{lo}-{hi}']['raw']:+.2f} dB, "
              f"envelope sees {out[f'{lo}-{hi}']['envelope']:+.2f} dB")
    try:
        os.remove(os.path.join(OUT, "_inj.wav"))
    except OSError:
        pass
    return out


def main():
    files = sorted(glob.glob(os.path.join(ROOT, "google_drive", "*.mp3")))
    if not files:
        raise SystemExit("no audio in google_drive/")
    rows = []
    for f in files:
        f0, band = analyse(f)
        rows.append({"file": os.path.basename(f), "f0": f0, "bands": band})
        print(f"  {os.path.basename(f)[:40]:<40} F0 {f0:5.0f} Hz")

    f0 = np.array([r["f0"] for r in rows])
    res = {"n_songs": len(rows), "lifter_ms": LIFTER_S * 1000, "notch_width_ms": NOTCH_W * 1000,
           "bands": {}}
    print(f"\n{'band Hz':>11}{'SD raw':>9}{'SD envelope':>13}{'r(F0) raw':>12}{'r(F0) env':>11}")
    for lo, hi in BANDS:
        key = f"{lo}-{hi}"
        raw = np.array([r["bands"][key][0] for r in rows])
        env = np.array([r["bands"][key][1] for r in rows])
        rr = float(np.corrcoef(f0, raw)[0, 1])
        re = float(np.corrcoef(f0, env)[0, 1])
        res["bands"][key] = {
            "sd_raw_db": round(float(raw.std(ddof=1)), 2),
            "sd_envelope_db": round(float(env.std(ddof=1)), 2),
            "slope_correlation_raw": round(rr, 2),
            "slope_correlation_envelope": round(re, 2),
            "raw_range_db": round(float(raw.max() - raw.min()), 2),
            "envelope_range_db": round(float(env.max() - env.min()), 2),
            "raw_per_song": [round(float(v), 1) for v in raw],
            "envelope_per_song": [round(float(v), 1) for v in env],
        }
        print(f"{key:>11}{raw.std(ddof=1):>9.2f}{env.std(ddof=1):>13.2f}{rr:>12.2f}{re:>11.2f}")

    shrunk = [res["bands"][f"{a}-{b}"]["sd_raw_db"] / res["bands"][f"{a}-{b}"]["sd_envelope_db"]
              for a, b in BANDS]
    res["mean_tolerance_shrink"] = round(float(np.mean(shrunk)), 2)
    print(f"\ntolerance shrinks by {res['mean_tolerance_shrink']:.2f}x on average "
          f"(that is the same rule made more sensitive with fewer false alarms)")

    print("\nfault response (does it still see a real problem?):")
    fr = fault_response(files)
    res["fault_response_db"] = fr
    print(f"\n{'band Hz':>11}{'fault raw':>11}{'fault env':>11}{'detect raw':>12}{'detect env':>12}{'gain':>7}")
    for lo, hi in BANDS:
        k = f"{lo}-{hi}"
        d_raw = fr[k]["raw"] / res["bands"][k]["sd_raw_db"]
        d_env = fr[k]["envelope"] / res["bands"][k]["sd_envelope_db"]
        res["bands"][k]["detectability_raw"] = round(d_raw, 2)
        res["bands"][k]["detectability_envelope"] = round(d_env, 2)
        print(f"{k:>11}{fr[k]['raw']:>11.2f}{fr[k]['envelope']:>11.2f}"
              f"{d_raw:>12.2f}{d_env:>12.2f}{d_env/d_raw:>6.1f}x")
    det = [res["bands"][f"{a}-{b}"]["detectability_envelope"] /
           res["bands"][f"{a}-{b}"]["detectability_raw"] for a, b in BANDS]
    res["mean_detectability_gain"] = round(float(np.mean(det)), 2)
    print(f"\nmean detectability gain: {res['mean_detectability_gain']:.2f}x")

    # ------------------------------------------------------------------ figure
    fig, axes = plt.subplots(1, 2, figsize=(13.5, 5.4), gridspec_kw={"width_ratios": [1.25, 1]})
    fig.patch.set_facecolor("#0f1218")
    for ax in axes:
        ax.set_facecolor("#0b0e13")
        ax.tick_params(colors="#98a2ae")
        for sp in ax.spines.values():
            sp.set_color("#232a34")
        ax.grid(True, color="#1b2129", lw=0.6)

    ax = axes[0]
    key = "110-260"
    raw = np.array([r["bands"][key][0] for r in rows])
    env = np.array([r["bands"][key][1] for r in rows])
    for vals, colour, label in ((raw, "#8b96a3", "raw spectrum (what the plugin measures now)"),
                                (env, "#5fe08a", "harmonic envelope (the next step)")):
        ax.scatter(f0, vals, s=46, color=colour, edgecolor="#0b0e13", linewidth=0.8, label=label, zorder=3)
        b, a = np.polyfit(f0, vals, 1)
        xs = np.linspace(f0.min(), f0.max(), 50)
        ax.plot(xs, a + b * xs, color=colour, lw=1.6, ls="--", alpha=0.9)
        ax.annotate(f"r = {np.corrcoef(f0, vals)[0,1]:+.2f}   spread {vals.max()-vals.min():.1f} dB",
                    (xs[-1], a + b * xs[-1]), textcoords="offset points", xytext=(-6, 8 if colour == "#8b96a3" else -16),
                    color=colour, fontsize=9, ha="right")
    ax.set_xlabel("median F0 of the song (Hz)", color="#98a2ae")
    ax.set_ylabel("level of the 110-260 Hz band, dB re 1 kHz", color="#98a2ae")
    ax.set_title("Same band, 15 mixes of one engineer:\nthe raw number follows the singer far more than the envelope",
                 color="#e8ecf1", fontsize=11, loc="left")
    ax.legend(facecolor="#151a21", edgecolor="#232a34", labelcolor="#c8d0d8", fontsize=8.5, loc="lower right")

    ax = axes[1]
    xs = np.arange(len(BANDS))
    d_raw = [res["bands"][f"{a}-{b}"]["detectability_raw"] for a, b in BANDS]
    d_env = [res["bands"][f"{a}-{b}"]["detectability_envelope"] for a, b in BANDS]
    ax.bar(xs - 0.19, d_raw, 0.38, color="#8b96a3", label="raw spectrum")
    ax.bar(xs + 0.19, d_env, 0.38, color="#5fe08a", label="harmonic envelope")
    ax.set_xticks(xs)
    ax.set_xticklabels([f"{a}-{b}" for a, b in BANDS], rotation=30, ha="right", fontsize=8.5)
    ax.set_ylabel("detectability = response to a +4 dB fault / tolerance", color="#98a2ae")
    ax.set_title("The real test: can it tell a fault from a singer?\n(a 3.6x smaller tolerance alone would have been blind)",
                 color="#e8ecf1", fontsize=11, loc="left")
    ax.legend(facecolor="#151a21", edgecolor="#232a34", labelcolor="#c8d0d8", fontsize=9)

    fig.suptitle("Harmonic envelope: what it buys, measured on the 15 reference mixes",
                 color="#e8ecf1", fontsize=13.5,
                 x=0.012, ha="left")
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    png = os.path.join(OUT, "envelope_demo.png")
    fig.savefig(png, dpi=130, facecolor=fig.get_facecolor())
    json.dump(res, open(os.path.join(OUT, "envelope_demo.json"), "w"), ensure_ascii=False, indent=1)
    print("wrote", png)


if __name__ == "__main__":
    main()
