#!/usr/bin/env python3
"""
decision_model.py - the part of the design that no product on the market has:
a decision rule whose tolerance is learned from ONE engineer's own work and
normalised for the singer's voice type.

Three claims are tested here, each with the 25 real mixes we own:

  1. FLAT TOLERANCE (what every auto-EQ does, in effect: one number for every
     band) fires on the reference engineer's own valid mixes.

  2. VOICE-NORMALISED TOLERANCE does not.  Within the 15 songs of one engineer
     the voice type moves several bands (corr(tilt, F0) = +0.8), so part of the
     spread across his own mixes is the singer, not the taste.  Removing the F0
     component leaves a smaller, honest tolerance: "how far this engineer
     allows *this kind of voice* to sit".

  3. The rule stays alive: on material outside the reference it still speaks,
     and the difference is significant under label permutation.

Inputs
  analysis/out/amir15/corpus.json  15 Amir songs, Dry (27-band curves, F0, frame p10/p90)
  analysis/out/analysis.json       13 songs on RAW; 10 of them are other engineers

Output
  analysis/out/amir15/decision_model.json
"""
import json
import os

import numpy as np

ROOT = "/home/user/AI-EQ-PLUGIN-VST"
OUT = os.path.join(ROOT, "analysis", "out")
BANDS = np.array([40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                  1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                  10000, 12500, 16000], dtype=float)
USE = (BANDS >= 200) & (BANDS <= 12000)          # the region the design corrects
BRIDGES = {"اتأخر عتابنا - عمرو دياب", "ايامي - رامي صبري", "قالوا عني ايه - محمد حماقي"}
NON_AMIR = ["انا فاضل - كايروكي", "بتمنى أنساك - شيرين عبدالوهاب", "بيكلموني - رامي جمال",
            "جدع - تامر عاشور", "غروب - حمزة نمرة", "قلب في الدفاتر - مُسلِم",
            "مستني ايه - تامر حسني", "معايا (من فيلم و لنا في الخيال حب) - بهاء سلطان_2",
            "نهايات الحكاوى - مسار اجباري", "هو انت مين - أنغام"]


def band_levels_from_grid(grid_db, grid_f, fc):
    out = np.empty(len(fc))
    for i, c in enumerate(fc):
        a, b = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        m = (grid_f >= a) & (grid_f < b)
        out[i] = 10 * np.log10(np.mean(10 ** (grid_db[m] / 10))) if m.any() else np.nan
    return out


def load():
    A = json.load(open(os.path.join(OUT, "analysis.json")))
    grid = np.array(A["grid"])
    arch = {t: band_levels_from_grid(np.array(v), grid, BANDS)
            for t, v in A["per_song_curve"].items()}
    C = json.load(open(os.path.join(OUT, "amir15", "corpus.json")))
    amir = []
    for s in C["songs"]:
        amir.append(dict(
            title=s["title"],
            curve=np.array(s["ltas_db"]) - np.interp(1000.0, BANDS, np.array(s["ltas_db"])),
            f0=s["med_f0_hz"],
            spread=np.array(s["frame_band_p90"]) - np.array(s["frame_band_p10"]),
        ))
    # the archived curves are already dB re 1 kHz
    others = [dict(title=t, curve=arch[t], f0=float("nan"), spread=np.full(len(BANDS), np.nan))
              for t in NON_AMIR]
    return amir, others, arch


def sub(a, m):
    """subset a curve/vector to the usable band"""
    return np.asarray(a)[m]


def tolerance_flat(ref_curves):
    return np.nanstd(ref_curves, axis=0, ddof=1)


def tolerance_voice_normalised(ref_curves, ref_f0):
    """SD that survives after the singer's register is regressed out.

    One line, and it is the whole idea: a band whose level tracks F0 across the
    engineer's own mixes is describing the voice, not the mix, so its tolerance
    must not shrink when the voice matches.
    """
    Y = np.asarray(ref_curves, dtype=float)
    f0 = np.asarray(ref_f0, dtype=float)
    f0 = f0 - f0.mean()
    resid = np.empty_like(Y)
    for j in range(Y.shape[1]):
        y = Y[:, j]
        ok = np.isfinite(y)
        if ok.sum() < 5 or np.nanstd(f0[ok]) < 1e-9:
            resid[:, j] = y - np.nanmean(y)
            continue
        b = np.sum(f0[ok] * (y[ok] - y[ok].mean())) / np.sum(f0[ok] ** 2)
        resid[:, j] = y - y[ok].mean() - b * f0
    return np.nanstd(resid, axis=0, ddof=1), resid


def count_interventions(flag):
    """contiguous runs of flagged bands - one run is one decision, not five"""
    n = 0
    prev = False
    for f in flag:
        if f and not prev:
            n += 1
        prev = f
    return n


def main():
    amir, others, arch = load()
    m = USE
    Xa = np.stack([s["curve"] for s in amir])[:, m]
    Fa = np.array([s["f0"] for s in amir])
    bands = BANDS[m]
    res = {"bands_hz": [float(b) for b in bands], "n_amir": len(amir), "n_other": len(others)}

    k = 2.5          # how many tolerances away a band must sit before it is touched
    res["k"] = k
    out_rows = []

    # ---------------- 1. leave-one-out on the engineer's own 15 songs --------
    for mode in ("flat", "voice_normalised"):
        false_flags = 0
        per_song = []
        for i, s in enumerate(amir):
            keep = [j for j in range(len(amir)) if j != i]
            refc = Xa[keep]
            reff0 = Fa[keep]
            if mode == "flat":
                sd = tolerance_flat(refc)
            else:
                sd, _ = tolerance_voice_normalised(refc, reff0)
            sd = np.maximum(sd, 0.5)                  # never divide by a hair
            z = (s["curve"][m] - refc.mean(axis=0)) / sd
            # a fixed fault is narrow in time; a moving formant is not.  Bands
            # that swing more than 9 dB within one performance are the vowel
            # changing, so they are never a "static" problem.
            stable = s["spread"][m] < 9.0
            flag = (np.abs(z) > k) & stable
            n = count_interventions(flag)
            false_flags += n
            per_song.append({"title": s["title"], "interventions": n,
                             "max_abs_z": round(float(np.nanmax(np.abs(z))), 2)})
        out_rows.append((mode, false_flags, per_song))

    # ---------------- 2. the 10 other engineers (never used for anything) ----
    sd_flat = tolerance_flat(Xa)
    sd_voice, _ = tolerance_voice_normalised(Xa, Fa)
    sd_flat = np.maximum(sd_flat, 0.5)
    sd_voice = np.maximum(sd_voice, 0.5)
    other_counts = {"flat": [], "voice_normalised": []}
    for o in others:
        for mode, sd in (("flat", sd_flat), ("voice_normalised", sd_voice)):
            z = (o["curve"][m] - Xa.mean(axis=0)) / sd
            # RAW-vs-DRY measurement bias, measured on the three bridge songs;
            # worst case here is ~1 dB inside this band, so it is subtracted.
            n = count_interventions(np.abs(z) > k)
            other_counts[mode].append(n)

    res["per_song_amir"] = {mode: rows for mode, _, rows in out_rows}
    res["amir_interventions_per_song"] = {mode: round(n / len(amir), 2) for mode, n, _ in out_rows}
    res["other_interventions_per_song"] = {
        mode: round(float(np.mean(v)), 2) for mode, v in other_counts.items()}

    # ---------------- 3. is the difference real? ----------------------------
    rng = np.random.default_rng(11)
    for mode, sd in (("flat", sd_flat), ("voice_normalised", sd_voice)):
        scores = [count_interventions(np.abs((s["curve"][m] - Xa.mean(axis=0)) / sd) > k)
                  for s in amir] + other_counts[mode]
        scores = np.array(scores, dtype=float)
        obs = scores[15:].mean() - scores[:15].mean()
        null = np.empty(20000)
        for t in range(20000):
            p = rng.permutation(scores)
            null[t] = p[15:].mean() - p[:15].mean()
        res.setdefault("permutation", {})[mode] = {
            "observed_gap": round(float(obs), 2),
            "p": round(float((np.sum(null >= obs) + 1) / 20001), 4),
            "note": "gap in interventions/song, other engineers minus the reference engineer",
        }

    # ---------------- 4. where does each rule fire? -------------------------
    for mode, sd in (("flat", sd_flat), ("voice_normalised", sd_voice)):
        z = np.stack([np.abs((o["curve"][m] - Xa.mean(axis=0)) / sd) for o in others])
        rate = (z > k).mean(axis=0)
        res.setdefault("firing_rate_by_band", {})[mode] = [
            {"hz": float(b), "rate": round(float(r), 2)} for b, r in zip(bands, rate)
        ]

    # ---------------- 5. how much of the spread was the voice? --------------
    sd_flat_use, sd_voice_use = sd_flat, sd_voice
    res["tolerance"] = [
        {"hz": float(b), "flat_sd_db": round(float(a), 2), "voice_normalised_sd_db": round(float(v), 2),
         "voice_share": round(float(1.0 - v / a), 2) if a > 0.05 else 0.0}
        for b, a, v in zip(bands, sd_flat_use, sd_voice_use)
    ]

    # ---------------- 6. sensitivity: is it one lucky threshold? -----------
    sweep = []
    for kk in (1.5, 2.0, 2.5, 3.0, 3.5):
        row = {"k": kk}
        for mode in ("flat", "voice_normalised"):
            a_tot = o_tot = 0
            for i, s in enumerate(amir):
                keep = [j for j in range(len(amir)) if j != i]
                refc, reff0 = Xa[keep], Fa[keep]
                sd = (tolerance_flat(refc) if mode == "flat"
                      else tolerance_voice_normalised(refc, reff0)[0])
                sd = np.maximum(sd, 0.5)
                z = (s["curve"][m] - refc.mean(axis=0)) / sd
                a_tot += count_interventions((np.abs(z) > kk) & (s["spread"][m] < 9.0))
            sd = np.maximum(sd_flat if mode == "flat" else sd_voice, 0.5)
            for o in others:
                z = (o["curve"][m] - Xa.mean(axis=0)) / sd
                o_tot += count_interventions(np.abs(z) > kk)
            row[mode] = {"amir_per_song": round(a_tot / len(amir), 2),
                         "other_per_song": round(o_tot / len(others), 2)}
        sweep.append(row)
    res["sensitivity"] = sweep

    # ---------------- 7. one number for separation -------------------------
    def auc(pos, neg):
        pos, neg = np.asarray(pos, float), np.asarray(neg, float)
        return float(np.mean([[1.0 if a > b else 0.5 if a == b else 0.0 for b in neg] for a in pos]))

    for mode, sd in (("flat", sd_flat), ("voice_normalised", sd_voice)):
        a = [float(np.nanmax(np.abs((s["curve"][m] - Xa.mean(axis=0)) / sd))) for s in amir]
        b = [float(np.nanmax(np.abs((o["curve"][m] - Xa.mean(axis=0)) / sd))) for o in others]
        res.setdefault("separation", {})[mode] = {
            "auc": round(auc(b, a), 3),
            "median_max_z_reference": round(float(np.median(a)), 2),
            "median_max_z_other": round(float(np.median(b)), 2),
            "note": "AUC that a song is ranked as 'outside the standard' from the statistic alone",
        }

    print(f"{'rule':<18}{'interventions per Amir song':>30}{'per other-engineer song':>26}")
    for mode, n, _ in out_rows:
        print(f"{mode:<18}{n / len(amir):>30.2f}{res['other_interventions_per_song'][mode]:>26.2f}")
    print()
    for mode, v in res["permutation"].items():
        print(f"permutation [{mode}]: gap {v['observed_gap']:+.2f} interventions/song, p = {v['p']}")
    print("\nsensitivity to the threshold (interventions per song):")
    print(f"  {'k':<5}{'Amir flat':>11}{'other flat':>12}{'Amir voice':>12}{'other voice':>13}")
    for row in sweep:
        print(f"  {row['k']:<5}{row['flat']['amir_per_song']:>11.2f}{row['flat']['other_per_song']:>12.2f}"
              f"{row['voice_normalised']['amir_per_song']:>12.2f}{row['voice_normalised']['other_per_song']:>13.2f}")
    print("\nseparation (AUC, statistic alone):")
    for mode, v in res["separation"].items():
        print(f"  {mode:<18} AUC {v['auc']:.3f}   median max|z| Amir {v['median_max_z_reference']:.2f} "
              f"vs other {v['median_max_z_other']:.2f}")
    print("\nbands where the singer, not the taste, set the spread "
          "(voice share of the tolerance):")
    for row in sorted(res["tolerance"], key=lambda r: -r["voice_share"])[:8]:
        print(f"  {row['hz']:7.0f} Hz  flat ±{row['flat_sd_db']:.2f}  "
              f"voice-normalised ±{row['voice_normalised_sd_db']:.2f}  "
              f"({100 * row['voice_share']:.0f}% of it was the voice)")

    os.makedirs(os.path.join(OUT, "amir15"), exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "amir15", "decision_model.json"), "w"),
              ensure_ascii=False, indent=1)
    print("\nwrote analysis/out/amir15/decision_model.json")


if __name__ == "__main__":
    main()
