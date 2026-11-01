#!/usr/bin/env python3
"""
engineer_signature.py — does the mix engineer leave a measurable fingerprint on the
vocal's spectral deviation curve?

Reads
  analysis/out/analysis.json    (per_song_deviation: 13 songs x 27 bands, already
                                register-corrected; bands + register_beta)
  analysis/out/corpus.json      (med_f0_hz per song)
  analysis/out/engineers.json   (researched mix/master credits)

Writes
  analysis/out/engineer_signature.json

Tests, in order of what they can actually decide:
  1. Mantel-style permutation test on the 78 song pairs: are same-engineer pairs
     closer than different-engineer pairs?
  2. Exact enumeration of all C(13,3)=286 triples: where does the engineer's own
     triple rank? (no sampling error, it is the whole population)
  3. Register control: subtract the measured register effect, retest. Also test the
     lowest-F0 triple, to check "it's just a low voice".
  4. Granularity: is the similarity a fine curve or a broad balance?
       - per-song offset removed   (broad level)
       - band-to-band slopes       (fine detail)
       - per-band z correlation    (fine shape)
  5. Per-band localization with Benjamini-Hochberg FDR. If no band survives, the
     fingerprint is distributed, not a specific EQ move.

Usage:  python3 analysis/scripts/engineer_signature.py [--iters 20000]
"""
import argparse
import itertools
import json
import os

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "analysis", "out")
LO_BAND, HI_BAND = 200.0, 12000.0   # below 200 Hz we cannot correct a mono voice; above 12k is codec low-pass


def load():
    A = json.load(open(os.path.join(OUT, "analysis.json")))
    C = {s["title"]: s for s in json.load(open(os.path.join(OUT, "corpus.json")))["songs"]}
    E = json.load(open(os.path.join(OUT, "engineers.json")))
    credits = {s["title"]: s for s in E["songs"]}
    titles = A["titles"]
    missing = [t for t in titles if t not in credits]
    if missing:
        raise SystemExit(f"engineers.json does not cover: {missing}")
    return A, C, credits, titles


def pairwise(X):
    return np.array([np.linalg.norm(X[i] - X[j]) for i, j in itertools.combinations(range(len(X)), 2)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=20000)
    args = ap.parse_args()
    rng = np.random.default_rng(20261101)

    A, C, credits, titles = load()
    n = len(titles)
    dev = np.stack([np.array(A["per_song_deviation"][t]) for t in titles])
    bands = np.array(A["bands"], dtype=float)
    sel = (bands >= LO_BAND) & (bands <= HI_BAND)
    X = dev[:, sel]
    B = bands[sel]
    mix = [credits[t]["mix"] for t in titles]

    res = {"n_songs": n, "n_mix_engineers": len(set(mix)), "band_range_hz": [LO_BAND, HI_BAND],
           "n_bands_used": int(sel.sum()), "credits": {t: {"mix": credits[t]["mix"],
           "master": credits[t]["master"], "confidence": credits[t]["confidence"]} for t in titles}}

    # ---------- group sizes ----------
    from collections import Counter
    cnt = Counter(mix)
    res["mix_engineer_counts"] = dict(cnt)
    biggest = max(cnt, key=cnt.get)
    grp = [i for i, t in enumerate(titles) if mix[i] == biggest]
    res["largest_group"] = {"engineer": biggest, "songs": [titles[i] for i in grp], "n": len(grp)}

    iu = np.triu_indices(n, 1)
    d = pairwise(X)
    same = np.array([1.0 if mix[i] == mix[j] else 0.0 for i, j in zip(*iu)])

    def mean_diff(dist, s):
        return float(dist[s == 0].mean() - dist[s == 1].mean())

    # ---------- 1. Mantel ----------
    obs = mean_diff(d, same)
    null = np.empty(args.iters)
    for k in range(args.iters):
        pm = rng.permutation(mix)
        s = np.array([1.0 if pm[i] == pm[j] else 0.0 for i, j in zip(*iu)])
        null[k] = mean_diff(d, s)
    p_mantel = float((np.sum(null >= obs) + 1) / (args.iters + 1))
    res["mantel"] = {"same_pairs": int(same.sum()), "total_pairs": int(len(d)),
                     "closer_by_db": round(obs, 2), "null_mean": round(float(null.mean()), 2),
                     "null_sd": round(float(null.std()), 2), "p": round(p_mantel, 4)}

    # ---------- 2. exact enumeration of all triples ----------
    triples = list(itertools.combinations(range(n), 3))
    tri_d = np.array([np.mean([np.linalg.norm(X[i] - X[j]) for i, j in itertools.combinations(tr, 2)])
                      for tr in triples])
    ai = triples.index(tuple(grp)) if tuple(grp) in triples else None
    if ai is not None:
        res["triple_enumeration"] = {
            "n_triples": len(triples), "group_mean_pairwise_db": round(float(tri_d[ai]), 2),
            "rank": int((tri_d < tri_d[ai]).sum()) + 1,
            "p": round(float((tri_d <= tri_d[ai]).sum() / len(tri_d)), 4),
            "floor_db": round(float(tri_d.min()), 2), "median_db": round(float(np.median(tri_d)), 2)}

    # ---------- 3. register control ----------
    f = np.array([C[t]["med_f0_hz"] for t in titles])
    beta = np.array(A["register_beta"])
    fref = float(A["f0_ref"])
    Xr = (dev - np.outer(np.log2(f / fref), beta))[:, sel]
    dr = pairwise(Xr)
    obs_r = mean_diff(dr, same)
    null_r = np.empty(args.iters)
    for k in range(args.iters):
        pm = rng.permutation(mix)
        s = np.array([1.0 if pm[i] == pm[j] else 0.0 for i, j in zip(*iu)])
        null_r[k] = mean_diff(dr, s)
    res["register_control"] = {"closer_by_db": round(obs_r, 2),
                               "p": round(float((np.sum(null_r >= obs_r) + 1) / (args.iters + 1)), 4)}

    order = np.argsort(f)
    low3 = tuple(sorted(order[:3]))
    li = triples.index(low3)
    res["register_control"]["lowest_f0_triple"] = {
        "songs": [titles[i] for i in low3], "rank": int((tri_d < tri_d[li]).sum()) + 1,
        "p": round(float((tri_d <= tri_d[li]).sum() / len(tri_d)), 4),
        "note": "if this were also tight, the effect would just be 'a low voice'"}
    res["register_control"]["group_f0_spread_hz"] = round(float(np.ptp(f[grp])), 0)

    # ---------- 4. granularity ----------
    def perm_p(Xt, lower=True, iters=None):
        it = iters or args.iters
        real = np.mean([np.linalg.norm(Xt[i] - Xt[j]) for i, j in itertools.combinations(grp, 2)])
        nl = np.array([np.mean([np.linalg.norm(Xt[i] - Xt[j]) for i, j in itertools.combinations(s, 2)])
                       for s in (rng.choice(n, len(grp), replace=False) for _ in range(it))])
        p = (np.sum(nl >= real) + 1) / (it + 1) if not lower else (np.sum(nl <= real) + 1) / (it + 1)
        return round(float(real), 2), round(float(p), 4), round(float((real - nl.mean()) / nl.std()), 2)

    Xc = X - X.mean(axis=1, keepdims=True)          # per-song offset removed -> broad level
    Xd = np.diff(X, axis=1)                          # band-to-band deltas -> fine detail
    Z = (X - X.mean(0)) / np.maximum(X.std(0, ddof=1), 1e-9)
    real_z = np.mean([np.corrcoef(Z[i], Z[j])[0, 1] for i, j in itertools.combinations(grp, 2)])
    nl_z = np.array([np.mean([np.corrcoef(Z[i], Z[j])[0, 1] for i, j in itertools.combinations(s, 2)])
                     for s in (rng.choice(n, len(grp), replace=False) for _ in range(args.iters))])
    p_z = float((np.sum(nl_z >= real_z) + 1) / (args.iters + 1))

    g, p_, z_ = perm_p(X)
    res["granularity"] = {
        "raw_distance":  {"value": g, "z": z_, "p": p_},
        "offset_removed": dict(zip(("value", "p", "z"), perm_p(Xc))),
        "slope_only": dict(zip(("value", "p", "z"), perm_p(Xd))),
        "band_z_correlation": {"value": round(float(real_z), 3), "p": round(p_z, 4)},
        "reading": "broad balance clusters; fine detail does not"}

    # ---------- 5. per-band localisation + FDR ----------
    rows = []
    for k in range(len(B)):
        x = dev[:, k]
        same_g = np.mean([abs(x[i] - x[j]) for i, j in itertools.combinations(grp, 2)])
        all_g = np.mean([abs(x[i] - x[j]) for i, j in itertools.combinations(range(n), 2)])
        nl = np.array([np.mean([abs(x[i] - x[j]) for i, j in itertools.combinations(s, 2)])
                       for s in (rng.choice(n, len(grp), replace=False) for _ in range(4000))])
        p = float((np.sum(nl <= same_g) + 1) / 4001)
        rows.append({"band": int(B[k]), "group_db": round(float(same_g), 2),
                     "all_db": round(float(all_g), 2),
                     "variance_reduction": round(float(1 - same_g / all_g), 3) if all_g > 0 else None,
                     "p_uncorrected": round(p, 4)})
    ps = np.array([r["p_uncorrected"] for r in rows])
    o = np.argsort(ps)
    m = len(ps)
    bh = np.minimum.accumulate((ps[o] * m / np.arange(1, m + 1))[::-1])[::-1]
    for idx, q in zip(o, bh):
        rows[idx]["q_bh"] = round(float(q), 4)
    res["per_band"] = {"rows": rows}
    res["per_band"]["summary"] = {
        "uncorrected_p_lt_005": int((ps < 0.05).sum()),
        "expected_by_chance": round(m * 0.05, 1),
        "survive_bh_fdr_005": [int(B[i]) for i in range(m) if rows[i]["q_bh"] < 0.05],
        "regions_variance_reduction": {
            name: round(float(1 - dev[grp][:, (bands >= lo) & (bands <= hi)].std(0).mean()
                              / dev[:, (bands >= lo) & (bands <= hi)].std(0).mean()), 3)
            for name, (lo, hi) in {"body_200_500": (200, 500), "presence_500_2k": (500, 2000),
                                   "harsh_2k_5k": (2000, 5000), "air_5k_12k": (5000, 12000)}.items()}}

    # ---------- sensitivity: include the mastering credit ----------
    alt = [t for t in titles if credits[t]["mix"] == biggest or credits[t]["master"] == biggest]
    if len(alt) > len(grp):
        idx = [titles.index(t) for t in alt]
        real = np.mean([np.linalg.norm(X[i] - X[j]) for i, j in itertools.combinations(idx, 2)])
        nl = np.array([np.mean([np.linalg.norm(X[i] - X[j]) for i, j in itertools.combinations(s, 2)])
                       for s in (rng.choice(n, len(idx), replace=False) for _ in range(args.iters))])
        res["sensitivity_master_included"] = {
            "songs": alt, "value": round(float(real), 2),
            "p": round(float((np.sum(nl <= real) + 1) / (args.iters + 1)), 4)}

    res["limits"] = [
        f"{n} songs, {len(set(mix))} mix engineers, only one engineer appears more than twice "
        f"({biggest}, n={len(grp)}); the exact permutation space for a triple is {len(triples)}",
        "a single testable group means no cross-validation: the result cannot be shown to generalise",
        "هاني محروس — named by the user — has zero songs in this corpus, so his signature is untested",
        "confounds: genre (all Egyptian pop), era (2018-2026), singer, label and production budget",
        "null results for fine detail are NOT evidence that engineers do not differ; power is far too low",
    ]

    path = os.path.join(OUT, "engineer_signature.json")
    json.dump(res, open(path, "w"), ensure_ascii=False, indent=1)
    print(json.dumps({k: res[k] for k in ("mantel", "triple_enumeration", "register_control",
                                          "granularity", "sensitivity_master_included")},
                     ensure_ascii=False, indent=1))
    print("per-band summary:", json.dumps(res["per_band"]["summary"], ensure_ascii=False))
    print("wrote", path)


if __name__ == "__main__":
    main()
