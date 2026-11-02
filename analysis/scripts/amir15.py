#!/usr/bin/env python3
"""
amir15.py — measure what Amir Mahrous's own mixes look like, and test it.

Inputs
  analysis/out/analysis.json       archived 13 songs, measured on RAW stems
                                   (per_song_curve = 27 bands, dB re 1 kHz)
  analysis/out/amir15/corpus.json  15 Amir songs, measured on DRY stems
                                   (ltas_bins_db = 958-point grid, dB re 1 kHz;
                                    ltas_db = 27 bands, absolute)

Output
  analysis/out/amir15/amir15.json

Steps
 1. CALIBRATION: the 3 bridge songs exist in both sets. Their difference is the
    raw-vs-dry measurement bias. Everything else is corrected by it, so the
    comparison is honest.
 2. Amir's consensus curve over 15 songs, with a leave-one-out stability number.
 3. Per-band gap vs the archived 13-song consensus. Where does it exceed the
    2.5 dB dead zone?
 4. Register control: is the gap just because his singers sit in a different
    register than the corpus average?
 5. Clustering: do Amir's 15 sit closer to each other than the 10 other-engineer
    songs sit to each other? Permutation test, and exact where possible.
"""
import json
import os

import numpy as np

ROOT = "/home/user/AI-EQ-PLUGIN-VST"
OUT = os.path.join(ROOT, "analysis", "out")
BANDS = np.array([40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                  1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                  10000, 12500, 16000], dtype=float)
DEADZONE = 2.5
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


def main():
    A = json.load(open(os.path.join(OUT, "analysis.json")))
    N = json.load(open(os.path.join(OUT, "amir15", "corpus.json")))
    grid = np.array(A["grid"])

    new = {}
    for s in N["songs"]:
        curve = band_levels_from_grid(np.array(s["ltas_bins_db"]), grid, BANDS)
        new[s["title"]] = {"curve": curve, "f0": s["med_f0_hz"], "tilt": s["tilt_db_oct"],
                           "lufs": s["dyn"]["lufs"], "plr": s["dyn"]["plr"],
                           "lra": s["dyn"]["lra"], "dur": s["dur_s"],
                           "sib": s["sibilance_index_db"], "hnr": s["hnr_median"]}

    # archived per-song curves live on the 958-point grid -> bring them to bands
    arch_band = {t: band_levels_from_grid(np.array(v), grid, BANDS)
                 for t, v in A["per_song_curve"].items()}

    res = {"n_songs": len(new), "songs": sorted(new)}

    # ---------- 1. calibration on the bridges ----------
    rows = []
    for t in BRIDGES:
        if t in new and t in A["per_song_curve"]:
            old = arch_band[t]
            d = new[t]["curve"] - old
            rows.append({"title": t, "mean_gap_db": float(np.nanmean(d)),
                         "rms_gap_db": float(np.sqrt(np.nanmean(d ** 2))),
                         "max_gap_db": float(np.nanmax(np.abs(d))),
                         "per_band": [float(x) for x in d]})
    usable0 = (BANDS >= 200) & (BANDS <= 12000)
    if rows:
        rms_usable = float(np.mean([np.sqrt(np.nanmean(np.array(r["per_band"])[usable0] ** 2))
                                    for r in rows]))
        mean_usable = float(np.mean([np.nanmean(np.array(r["per_band"])[usable0]) for r in rows]))
    else:
        rms_usable = mean_usable = 0.0
    res["calibration_raw_vs_dry"] = {
        "rms_bias_usable_db": round(rms_usable, 2),
        "mean_bias_usable_db": round(mean_usable, 2),
        "note": "new(DRY) minus archived(RAW) on the same 3 songs; this is the "
                "measurement bias that must be removed before comparing sets",
        "rows": rows,
        "mean_bias_db": float(np.mean([r["mean_gap_db"] for r in rows])) if rows else 0.0,
        "rms_bias_db": float(np.mean([r["rms_gap_db"] for r in rows])) if rows else 0.0,
    }

    # ---------- 2. Amir consensus ----------
    X = np.stack([new[t]["curve"] for t in sorted(new)])
    bias = np.nanmean(np.stack([new[t]["curve"] - arch_band[t]
                                for t in BRIDGES if t in new]), axis=0)
    Xc = X - bias                                    # bias-corrected, comparable to the archive
    mean = np.nanmean(X, axis=0)
    sd = np.nanstd(X, axis=0, ddof=1)
    loo = [np.nanmax(np.abs(np.nanmean(np.delete(X, i, axis=0), axis=0) - mean))
           for i in range(len(X))]
    res["amir_consensus"] = {
        "bands_hz": BANDS.tolist(),
        "mean_db": [round(float(v), 2) for v in mean],
        "sd_db": [round(float(v), 2) for v in sd],
        "loo_max_shift_db": round(float(np.median(loo)), 2),
        "loo_worst_song_shift_db": round(float(np.max(loo)), 2),
        "median_sd_db": round(float(np.nanmedian(sd)), 2),
    }

    # ---------- 3. gap vs the archived consensus ----------
    arch = np.array(A["consensus_band_db"])
    gap = mean - arch
    usable = (BANDS >= 200) & (BANDS <= 12000)
    res["gap_vs_archived_consensus"] = {
        "per_band_db": [round(float(v), 2) for v in gap],
        "bands_over_deadzone": [int(BANDS[i]) for i in np.where(np.abs(gap) > DEADZONE)[0]],
        "rms_gap_usable_db": round(float(np.sqrt(np.nanmean(gap[usable] ** 2))), 2),
        "max_gap_db": round(float(np.nanmax(np.abs(gap[usable]))), 2),
        "max_gap_at_hz": int(BANDS[usable][int(np.nanargmax(np.abs(gap[usable])))]),
    }

    # ---------- 4. register control ----------
    f = np.array([new[t]["f0"] for t in sorted(new)])
    beta = np.array(A["register_beta"])
    fref = float(A["f0_ref"])
    Xr = Xc - np.outer(np.log2(f / fref), beta)
    gap_r = np.nanmean(Xr, axis=0) - arch
    res["register_control"] = {
        "median_f0": round(float(np.median(f)), 1),
        "f0_range": [round(float(f.min()), 1), round(float(f.max()), 1)],
        "gap_rms_before_db": res["gap_vs_archived_consensus"]["rms_gap_usable_db"],
        "gap_rms_after_register_removal_db": round(float(np.sqrt(np.nanmean(gap_r[usable] ** 2))), 2),
    }

    # ---------- 5. clustering: Mantel test on real engineer labels ----------
    # The earlier version drew 15 songs from a pool that CONTAINED the 15 Amir
    # songs, so the null was contaminated by the signal and the p-value was
    # meaningless. The honest test keeps the real labels and permutes them.
    def spread(M):
        m = len(M)
        return float(np.mean([np.linalg.norm(M[i] - M[j]) for i in range(m) for j in range(i + 1, m)]))

    eng = {s["title"]: s["mix"] for s in json.load(open(os.path.join(OUT, "engineers.json")))["songs"]}
    ctrl = np.stack([arch_band[t] - bias for t in NON_AMIR])
    pool = np.vstack([Xc, ctrl])
    labels = [eng[t] for t in sorted(new)] + [eng[t] for t in NON_AMIR]
    n = len(pool)
    D = np.array([[np.linalg.norm(pool[i] - pool[j]) for j in range(n)] for i in range(n)])
    iu = np.triu_indices(n, 1)
    d = D[iu]
    same = np.array([1.0 if labels[i] == labels[j] else 0.0 for i, j in zip(*iu)])
    obs = float(d[same == 0].mean() - d[same == 1].mean())
    rng = np.random.default_rng(3)
    nl = np.empty(20000)
    lab0 = np.array(labels)
    for k in range(20000):
        pm = rng.permutation(lab0)
        sm = np.array([1.0 if pm[i] == pm[j] else 0.0 for i, j in zip(*iu)])
        nl[k] = d[sm == 0].mean() - d[sm == 1].mean()
    res["clustering"] = {
        "test": "Mantel-style permutation over the real mix-engineer labels, 25 songs",
        "n_songs": n, "n_same_engineer_pairs": int(same.sum()), "n_pairs": int(len(d)),
        "same_pairs_closer_by_db": round(obs, 2),
        "null_mean": round(float(nl.mean()), 2), "null_sd": round(float(nl.std()), 2),
        "p": round(float((np.sum(nl >= obs) + 1) / 20001), 4),
        "amir_within_db": round(spread(Xc), 2),
        "single_engineer_pair_distance_db": round(float(d[same == 1].mean()), 2),
        "different_engineer_distance_db": round(float(d[same == 0].mean()), 2),
    }

    # ---------- 6. what is distinctive ----------
    top = np.argsort(-np.abs(np.where(usable, gap, 0)))[:8]
    res["signature_candidates"] = [
        {"hz": int(BANDS[i]), "gap_db": round(float(gap[i]), 2),
         "amir_sd_db": round(float(sd[i]), 2),
         "over_deadzone": bool(abs(gap[i]) > DEADZONE)} for i in top]

    os.makedirs(os.path.join(OUT, "amir15"), exist_ok=True)
    json.dump(res, open(os.path.join(OUT, "amir15", "amir15.json"), "w"),
              ensure_ascii=False, indent=1)

    print(json.dumps({k: res[k] for k in ("calibration_raw_vs_dry", "register_control",
                                          "clustering")}, ensure_ascii=False, indent=1))
    print("\ngap vs archived consensus (dB), 200 Hz - 12 kHz:")
    for i in np.where(usable)[0]:
        flag = " <-- over dead zone" if abs(gap[i]) > DEADZONE else ""
        print(f"  {int(BANDS[i]):6d} Hz  {gap[i]:+6.2f}{flag}")
    print("\nsignature candidates:", json.dumps(res["signature_candidates"], ensure_ascii=False))


if __name__ == "__main__":
    main()
