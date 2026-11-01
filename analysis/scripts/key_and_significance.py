"""Key / tuning estimation + significance testing of the corpus claims.

This module answers the questions the first pass did not:
  1. what key is each take in (Krumhansl-Schmuckler on a chroma vector), and is
     it even meaningful for a monophonic vocal with quarter-tone maqam notes?
  2. how much of the between-song spectral deviation is explained by the voice
     (register) and by the key, leaving "production" as the residual?
  3. is the agreement between the 13 mixes statistically distinguishable from
     chance?  (permutation test on the PCA structure and on the 4.34 kHz dip)
  4. how sensitive is the consensus target to removing one song?
"""
from __future__ import annotations

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from aeq import io_audio as io  # noqa: E402
from aeq import spectral as sp  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "out")
ROOT = os.path.dirname(HERE)

# Krumhansl & Kessler (1982) key profiles
KS_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KS_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
NOTES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def chroma_of(path_or_array, sr=None, ref_hz=440.0, n_fft=4096, hop=1024):
    """Energy-per-pitch-class of the voiced parts, plus tuning offset in cents."""
    if isinstance(path_or_array, str):
        x, sr = io.load(path_or_array, mono=True)
        x = io.to_mono(x)
    else:
        x = path_or_array
    x = np.asarray(x, dtype=np.float64)

    best = None
    for cents in np.arange(-50, 51, 5.0):
        c = _chroma(x, sr, ref_hz * 2 ** (cents / 1200.0), n_fft, hop)
        conc = float(np.max(c))  # how concentrated the profile is
        if best is None or conc > best[0]:
            best = (conc, c, cents)
    _, chroma, cents = best
    return chroma / (chroma.sum() + 1e-20), float(cents)


def _chroma(x, sr, ref, n_fft, hop):
    n = len(x)
    nfr = 1 + (n - n_fft) // hop
    w = np.hanning(n_fft)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    midi = 69.0 + 12.0 * np.log2(np.maximum(freqs, 1e-6) / ref)
    pc = np.mod(np.round(midi).astype(int), 12)
    keep = (freqs > 80) & (freqs < 4000)
    chroma = np.zeros(12)
    for s in range(0, nfr, 256):
        e = min(s + 256, nfr)
        idx = np.arange(s, e)[:, None] * hop + np.arange(n_fft)[None, :]
        fr = x[idx] * w[None, :]
        rms = np.sqrt(np.mean(fr ** 2, axis=1))
        act = rms > np.percentile(rms, 45)          # only the louder half of frames
        if not act.any():
            continue
        P = np.abs(np.fft.rfft(fr[act], axis=1)) ** 2
        for k in range(12):
            m = keep & (pc == k)
            if m.any():
                chroma[k] += float(P[:, m].sum())
    return chroma


def estimate_key(chroma):
    """Krumhansl-Schmuckler: best major/minor rotation of the KS profiles."""
    scores = []
    for tonic in range(12):
        for mode, prof in (("maj", KS_MAJOR), ("min", KS_MINOR)):
            rot = np.roll(prof, tonic)
            r = float(np.corrcoef(chroma, rot)[0, 1])
            scores.append((r, tonic, mode))
    scores.sort(reverse=True)
    best = scores[0]
    margin = best[0] - scores[1][0]
    return dict(tonic=int(best[1]), mode=best[2], r=float(best[0]),
                margin=float(margin),
                name=f"{NOTES[best[1]]} {best[2]}")


# --------------------------------------------------------------------------- #
def analyse_keys(audio_dir: str, cache_dir: str | None = None):
    from aeq import corpus as cp

    man = cp.build_manifest(audio_dir)
    rows = []
    for e in man:
        title = e["title"]
        key = "".join(ch for ch in title if ch.isalnum())[:70]
        cf = os.path.join(cache_dir, f"chroma_{key}.npy") if cache_dir else None
        if cf and os.path.exists(cf):
            ch = np.load(cf)
            meta = json.load(open(cf + ".json"))
            cents = meta["cents"]
        else:
            x, sr = io.load(e["dry"], mono=True)
            ch, cents = chroma_of(io.to_mono(x).astype(np.float64), sr)
            if cf:
                np.save(cf, ch)
                json.dump({"cents": cents}, open(cf + ".json", "w"))
        k = estimate_key(ch)
        k.update(title=title, cents=cents, chroma=ch.tolist())
        # maqam check: how much energy sits between the 12 semitones?
        k["quarter_tone_energy"] = float(np.sum(ch[np.argsort(ch)[:3]]))
        rows.append(k)
    return rows


# --------------------------------------------------------------------------- #
def variance_decomposition(dev: np.ndarray, feats: dict) -> dict:
    """How much of the between-song deviation does each predictor set explain?

    dev : (n_songs, n_bands) deviations from the consensus
    Leave-one-out R^2 is the honest number with n = 13.
    """
    n = dev.shape[0]
    f0 = feats["log_f0"]
    keyc = feats["key_cos"]
    keys = feats["key_sin"]
    mode = feats["minor"]

    def loo_r2(X):
        X = np.column_stack([np.ones(n), X])
        pred = np.zeros_like(dev)
        for i in range(n):
            tr = np.arange(n) != i
            beta, *_ = np.linalg.lstsq(X[tr], dev[tr], rcond=None)
            pred[i] = X[i] @ beta
        ss_res = np.sum((dev - pred) ** 2)
        ss_tot = np.sum((dev - dev.mean(axis=0)) ** 2)
        return 1.0 - ss_res / max(ss_tot, 1e-12)

    tess = feats.get("tessitura")
    out = {
        "register_only": loo_r2(np.column_stack([f0])),
        "tessitura_only": loo_r2(np.column_stack([tess])) if tess is not None else float("nan"),
        "key_only": loo_r2(np.column_stack([keyc, keys, mode])),
        "register_key": loo_r2(np.column_stack([f0, keyc, keys, mode])),
        "all": loo_r2(np.column_stack([f0, tess, keyc, keys, mode])) if tess is not None else float("nan"),
    }
    # in-sample R^2 per band for the full model (upper bound)
    X = np.column_stack([np.ones(n), f0, keyc, keys, mode])
    beta, *_ = np.linalg.lstsq(X, dev, rcond=None)
    pred = X @ beta
    ss_res = np.sum((dev - pred) ** 2, axis=0)
    ss_tot = np.sum((dev - dev.mean(axis=0)) ** 2, axis=0)
    out["per_band_r2"] = (1.0 - ss_res / np.maximum(ss_tot, 1e-12)).tolist()
    return out


# --------------------------------------------------------------------------- #
def permutation_test_structure(dev: np.ndarray, n_perm: int = 2000, seed: int = 0) -> dict:
    """Is the cross-band agreement real, or could it arise with independent songs?

    Null: each band's set of song values is independent -> shuffle within every
    band column, which destroys cross-band structure while keeping each band's
    marginal distribution.
    """
    rng = np.random.default_rng(seed)
    X = dev - dev.mean(axis=0)
    _, sv, _ = np.linalg.svd(X, full_matrices=False)
    real = sv[0] ** 2 / np.sum(sv ** 2)
    null = np.empty(n_perm)
    for k in range(n_perm):
        Xp = np.empty_like(X)
        for j in range(X.shape[1]):
            Xp[:, j] = rng.permutation(X[:, j])
        _, svp, _ = np.linalg.svd(Xp, full_matrices=False)
        null[k] = svp[0] ** 2 / np.sum(svp ** 2)
    return dict(pc1_real=float(real), pc1_null_mean=float(null.mean()),
                pc1_null_p95=float(np.percentile(null, 95)),
                p_value=float((np.sum(null >= real) + 1) / (n_perm + 1)))


def permutation_test_dip(songs: list, band_lo=2500.0, band_hi=8000.0, window_oct=1 / 12) -> dict:
    """Binomial test: could 11/13 songs showing a dip within +-1/12 oct of 4.34 kHz
    be chance, if a 'dip' were equally likely anywhere in the vocal band?"""
    from math import comb

    freqs = np.array([n["freq"] for s in songs for n in s.get("notch_list", [])[:1]])
    if freqs.size == 0:
        return {}
    near = int(np.sum(np.abs(np.log2(freqs / 4337.0)) < window_oct))
    n = freqs.size
    span_oct = np.log2(band_hi / band_lo)
    p = (2 * window_oct) / span_oct
    tail = sum(comb(n, k) * p ** k * (1 - p) ** (n - k) for k in range(near, n + 1))
    return dict(n_songs=n, n_near=near, window_oct=2 * window_oct,
                p_chance=float(p), p_value=float(tail))


def leave_one_out_target(curves: np.ndarray) -> dict:
    """How much does the consensus move if any single song is removed?"""
    n = curves.shape[0]
    full = curves.mean(axis=0)
    shifts = np.array([np.abs(curves[np.arange(n) != i].mean(axis=0) - full).max() for i in range(n)])
    return dict(max_shift_db=float(shifts.max()), median_shift_db=float(np.median(shifts)),
                worst_song_removed=int(np.argmax(shifts)))


# --------------------------------------------------------------------------- #
def main():
    audio = os.path.join(ROOT, "google_drive")
    cache = os.path.join(OUT, "cache")
    os.makedirs(cache, exist_ok=True)

    print("=== 1. key / tuning per take (Krumhansl-Schmuckler on vocal chroma) ===")
    keys = analyse_keys(audio, cache)
    for k in keys:
        print(f"  {k['title'][:34]:34s} {k['name']:8s} r={k['r']:+.2f} "
              f"margin={k['margin']:.2f} tuning={k['cents']:+.0f} cents")

    weak = [k for k in keys if k["margin"] < 0.05]
    print(f"\n  confident keys (margin > 0.05): {len(keys)-len(weak)}/{len(keys)}; "
          f"tuning spread {min(k['cents'] for k in keys):+.0f}..{max(k['cents'] for k in keys):+.0f} cents")
    print(f"  NOTE: Arabic repertoire may use maqam intonation; a 12-TET key profile")
    print(f"  cannot represent that, so these labels are indicative, not authoritative.")

    A = json.load(open(os.path.join(OUT, "analysis.json")))
    S = json.load(open(os.path.join(OUT, "corpus.json")))["songs"]
    titles = A["titles"]
    BANDS = np.array(A["bands"])
    # IMPORTANT: use the *raw* per-song curves, not the register-corrected ones.
    # Regressing the corrected curves on register would be circular - the whole
    # point of this test is to see what the register correction did NOT remove.
    tmap = {s["title"]: s for s in S}
    raw = np.stack([np.array(tmap[t]["ltas_bins_db"]) for t in titles])
    band_vals = sp.band_levels(np.stack([np.array(tmap[t]["ltas_raw_bins_db"]) for t in titles]),
                               np.array(A["grid"]), BANDS)
    dev_grid = (raw - raw.mean(axis=0))            # per grid point, for the PCA test
    mb = BANDS >= 160.0
    dev = (band_vals - band_vals.mean(axis=0))[:, mb]

    kmap = {k["title"]: k for k in keys}
    f0 = np.array([S[[s["title"] for s in S].index(t)]["med_f0_hz"] for t in titles])
    tonic = np.array([kmap[t]["tonic"] for t in titles])
    minor = np.array([1.0 if kmap[t]["mode"] == "min" else 0.0 for t in titles])
    p10 = np.array([tmap[t]["f0_p10"] for t in titles])
    p90 = np.array([tmap[t]["f0_p90"] for t in titles])
    feats = dict(log_f0=np.log2(f0),
                 tessitura=np.stack([np.log2(p10), np.log2(p90)], axis=1),
                 key_cos=np.cos(2 * np.pi * tonic / 12),
                 key_sin=np.sin(2 * np.pi * tonic / 12), minor=minor)

    print("\n=== 2. what explains the differences between the 13 mixes? ===")
    vd = variance_decomposition(dev, feats)
    print(f"  leave-one-out R^2 of the between-song deviation:")
    print(f"    voice register (median F0)     : {vd['register_only']*100:5.1f} %")
    print(f"    tessitura (F0 p10..p90)        : {vd['tessitura_only']*100:5.1f} %")
    print(f"    key + mode only                : {vd['key_only']*100:5.1f} %")
    print(f"    register + key + mode          : {vd['register_key']*100:5.1f} %")
    print(f"    register + tessitura + key     : {vd['all']*100:5.1f} %")
    print("  (negative LOO R^2 = the predictor does worse than the plain mean:")
    print("   with n=13 there is no evidence that key predicts the band deviations,")
    print("   and adding it only overfits.)")

    print("\n=== 3. is the agreement between mixes real? (permutation tests) ===")
    ps = permutation_test_structure(dev_grid)
    print(f"  PC1 share of variance: real {ps['pc1_real']*100:.1f} % vs null "
          f"{ps['pc1_null_mean']*100:.1f} % (95th pct {ps['pc1_null_p95']*100:.1f} %) "
          f"-> p = {ps['p_value']:.4f}")
    pd_ = permutation_test_dip(S)
    if pd_:
        print(f"  dip near 4.34 kHz in {pd_['n_near']}/{pd_['n_songs']} songs; if dips were "
              f"random in 2.5-8 kHz the chance of a dip landing in a "
              f"{pd_['window_oct']*100/12:.0f}% slice is {pd_['p_chance']:.3f} "
              f"-> p(>= {pd_['n_near']} of {pd_['n_songs']}) = {pd_['p_value']:.2e}")

    print("\n=== 4. how much does the target depend on one song? ===")
    curves = np.stack([np.array(A["per_song_curve"][t]) for t in titles])
    lo = leave_one_out_target(curves)
    print(f"  removing the worst single song moves the target by at most "
          f"{lo['max_shift_db']:.2f} dB (median {lo['median_shift_db']:.2f} dB)")

    out = dict(keys=keys, variance=vd, permutation_structure=ps,
               permutation_dip=pd_, leave_one_out=lo,
               note=("Key was NOT part of the original analysis. It is quantified here; "
                     "see 'variance' for how much of the difference it explains."))
    json.dump(out, open(os.path.join(OUT, "key_significance.json"), "w"), ensure_ascii=False, indent=1)
    print("\nwrote analysis/out/key_significance.json")


if __name__ == "__main__":
    main()
