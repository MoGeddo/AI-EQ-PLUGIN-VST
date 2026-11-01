"""Per-song measurement + corpus aggregation (memory-bounded, streaming STFT)."""
from __future__ import annotations

import glob
import json
import os
import re

import numpy as np

from . import spectral as sp
from . import loudness as ld
from . import reverb as rv
from . import io_audio as io

GRID = sp.log_grid(20.0, 20000.0, 96)
BANDS = sp.octave_centers(40.0, 16000.0, 3)
N_FFT, HOP = 8192, 2048


# --------------------------------------------------------------------------- #
def build_manifest(audio_dir: str) -> list:
    files = sorted(glob.glob(os.path.join(audio_dir, "*.mp3")))
    songs: dict = {}
    for f in files:
        base = os.path.basename(f)
        title = re.split(r"\s*\(Vocals\)", base)[0].strip()
        ent = songs.setdefault(title, {"title": title, "dry": None, "wet": None, "model": ""})
        if "(Reverb)" in base:
            ent["wet"] = f
        else:
            ent["dry"] = f
            m = re.search(r"\((Mel-?Ro[Ff]ormer[^)]*|BS-RoFormer[^)]*)\)", base)
            if m and not ent["model"]:
                ent["model"] = m.group(1)
    return sorted([v for v in songs.values() if v["dry"]], key=lambda d: d["title"])


# --------------------------------------------------------------------------- #
def analyze_song(entry: dict, cache_dir: str | None = None) -> dict:
    key = re.sub(r"\W+", "_", entry["title"])[:80]
    if cache_dir:
        os.makedirs(cache_dir, exist_ok=True)

    def load_cached(path, tag, mono=False):
        cp = os.path.join(cache_dir, f"{key}_{tag}.npy") if cache_dir else None
        if cp and os.path.exists(cp):
            return np.load(cp), json.load(open(cp + ".json"))["sr"]
        x, sr = io.load(path, mono=mono)
        if cp:
            np.save(cp, x)
            json.dump({"sr": sr}, open(cp + ".json", "w"))
        return x, sr

    dry, sr = load_cached(entry["dry"], "dry")
    mon = io.to_mono(dry)
    res: dict = {"title": entry["title"], "model": entry.get("model", ""), "sr": int(sr),
                 "dur_s": float(len(dry) / sr), "n_channels": int(dry.shape[1])}

    # ---- cheap VAD + F0 at 10 ms ----------------------------------------- #
    vad = sp.vad_fast(mon, sr)
    f0, hnr = sp.f0_yin(mon, sr, win=0.040, hop=0.010)
    n10 = min(len(f0), len(vad["rms_db"]))
    f0, hnr = f0[:n10], hnr[:n10]
    act10, voi10, unv10 = vad["active"][:n10], vad["voiced"][:n10], vad["unvoiced"][:n10]
    f0v = f0[voi10 & (f0 > 0)]
    med_f0 = float(np.median(f0v)) if f0v.size > 10 else float("nan")
    low10 = voi10 & (f0 > 0) & (f0 <= med_f0)
    high10 = voi10 & (f0 > med_f0)

    # ---- single spectral pass -------------------------------------------- #
    groups = {"all": act10, "voiced": voi10, "unvoiced": unv10, "low": low10, "high": high10}
    acc = {k: np.zeros(GRID.size) for k in groups}
    cnt = {k: 0 for k in groups}
    band_frames, sib, fr_f0 = [], [], []
    mapper = None
    for blk in sp.iter_stft(mon, sr, N_FFT, HOP, block=96):
        if mapper is None:
            mapper = sp.GridMapper(blk["freqs"], GRID)
        s = blk["start"]
        tb = np.clip(((np.arange(s, s + blk["psd"].shape[0]) * HOP + N_FFT / 2) / sr / 0.010).astype(int), 0, n10 - 1)
        gp = 10 ** (mapper.to_db(blk["psd"]) / 10.0)
        for k, mask in groups.items():
            m = mask[tb]
            if m.any():
                acc[k] += gp[m].sum(axis=0)
                cnt[k] += int(m.sum())
        m = act10[tb]
        if m.any():
            sub = 10 * np.log10(gp[m] + sp.EPS)
            band_frames.append(sp.band_levels(sub, GRID, BANDS))
            fr_f0.append(f0[tb][m])
            sib.append(sub[:, (GRID >= 5000) & (GRID < 10000)].mean(axis=1)
                       - sub[:, (GRID >= 1000) & (GRID < 4000)].mean(axis=1))
    curve = {k: 10 * np.log10(acc[k] / max(cnt[k], 1) + sp.EPS) for k in groups}
    ltas = curve["all"]
    bf = np.concatenate(band_frames, axis=0) if band_frames else np.zeros((0, BANDS.size))
    sib = np.concatenate(sib) if sib else np.zeros(0)

    # ---- F0-conditioned spectra: how the *voice* moves each band ---------- #
    bf_f0 = np.concatenate(fr_f0) if fr_f0 else np.zeros(0)
    f0_bins = []
    if bf.size and bf_f0.size == bf.shape[0]:
        v = bf_f0 > 0
        if v.sum() > 200:
            q = np.percentile(bf_f0[v], np.linspace(5, 95, 10))
            for lo, hi in zip(q[:-1], q[1:]):
                m = v & (bf_f0 >= lo) & (bf_f0 < hi)
                if m.sum() > 30:
                    p = 10 ** (bf[m] / 10.0)
                    f0_bins.append({"f0": float(np.median(bf_f0[m])),
                                    "bands": [float(x) for x in 10 * np.log10(p.mean(axis=0) + sp.EPS)]})

    def band_at(f):
        return float(sp.band_levels(ltas, GRID, np.array([f]), 1)[0])

    bands_db = sp.band_levels(ltas, GRID, BANDS)
    res.update(
        ltas_db=[float(v) for v in bands_db],
        ltas_bins_db=[float(v) for v in (ltas - band_at(1000.0))],
        ltas_raw_bins_db=[float(v) for v in ltas],
        voiced_bins_db=[float(v) for v in (curve["voiced"] - band_at(1000.0))],
        unvoiced_bins_db=[float(v) for v in (curve["unvoiced"] - band_at(1000.0))],
        low_bins_db=[float(v) for v in (curve["low"] - (curve["low"].max()))],
        high_bins_db=[float(v) for v in (curve["high"] - (curve["high"].max()))],
        tilt_db_oct=sp.tilt_db_per_oct(ltas, GRID),
        tilt_100_1k=band_at(1000.0) - band_at(100.0),
        tilt_1k_6k3=band_at(6300.0) - band_at(1000.0),
        centroid_hz=sp.centroid_hz(ltas, GRID),
        rolloff85_hz=sp.rolloff_hz(ltas, GRID),
        residual_db=[float(v) for v in sp.residual_vs_tilt(ltas, GRID)],
        med_f0_hz=med_f0,
        f0_p10=float(np.percentile(f0v, 10)) if f0v.size else float("nan"),
        f0_p90=float(np.percentile(f0v, 90)) if f0v.size else float("nan"),
        hnr_median=float(np.median(hnr[voi10 & (f0 > 0)])) if voi10.any() else float("nan"),
        sibilance_index_db=float(np.median(sib)) if sib.size else float("nan"),
        sibilance_p90_db=float(np.percentile(sib, 90)) if sib.size else float("nan"),
        active_frac=float(act10.mean()),
        voiced_frac=float(voi10.mean()),
        frame_band_p10=[float(v) for v in np.percentile(bf, 10, axis=0)],
        frame_band_p50=[float(v) for v in np.percentile(bf, 50, axis=0)],
        frame_band_p90=[float(v) for v in np.percentile(bf, 90, axis=0)],
        register_diff_db=[float(v) for v in sp.band_levels(curve["high"] - curve["low"], GRID, BANDS)],
        notch_list=sp.find_notches(sp.smooth_logfreq(ltas, 1 / 24), GRID),
        f0_bins=f0_bins,
        sub_leak_db=band_at(50.0) - band_at(200.0),
        air10k_db=band_at(10000.0) - band_at(3150.0),
        air16k_db=band_at(16000.0) - band_at(10000.0),
        hpf_corner_hz=_hpf_corner(ltas),
    )
    res["dyn"] = ld.dynamics(dry, sr)

    if dry.shape[1] > 1:
        L = dry[:, 0].astype(np.float64)
        R = dry[:, 1].astype(np.float64)
        mid, side = (L + R) / 2, (L - R) / 2
        res.update(side_over_mid_db=float(10 * np.log10((np.mean(side ** 2) + sp.EPS) / (np.mean(mid ** 2) + sp.EPS))),
                   lr_corr=float(np.corrcoef(L, R)[0, 1]))

    if entry.get("wet"):
        wet, _ = load_cached(entry["wet"], "wet")
        n = min(len(dry), len(wet))
        d, w = dry[:n], wet[:n]
        rt = rv.rt60_from_offsets(d, w, sr)
        res["wet"] = {**rv.wet_fraction(d, w, sr), **rt,
                      "lufs": ld.integrated_lufs(w, sr),
                      "colour_db": [float(v) for v in sp.band_levels(rv.space_colour(w, sr, GRID), GRID, BANDS)]}
    return res


def _hpf_corner(curve_db: np.ndarray, ref=(150.0, 400.0)) -> float:
    ref_lvl = float(sp.band_levels(curve_db, GRID, np.array([np.sqrt(ref[0] * ref[1])]), 1)[0])
    m = (GRID >= 25.0) & (GRID <= 300.0)
    g, c = GRID[m], curve_db[m]
    above = c > ref_lvl - 3.0
    return float(g[int(np.argmax(above))]) if above.any() else float("nan")


# --------------------------------------------------------------------------- #
def aggregate(songs: list) -> dict:
    bins = np.stack([np.array(s["ltas_bins_db"]) for s in songs])
    res = np.stack([np.array(s["residual_db"]) for s in songs])
    bands = np.stack([np.array(s["ltas_db"]) for s in songs])
    regd = np.stack([np.array(s["register_diff_db"]) for s in songs])
    n = bins.shape[0]
    mu = bands.mean(axis=0)
    se = bands.std(axis=0, ddof=1) / np.sqrt(n)
    return {
        "n_songs": n,
        "titles": [s["title"] for s in songs],
        "grid": [float(v) for v in GRID],
        "band_centers": [float(v) for v in BANDS],
        "mean_curve_db": [float(v) for v in bins.mean(axis=0)],
        "median_curve_db": [float(v) for v in np.median(bins, axis=0)],
        "std_curve_db": [float(v) for v in bins.std(axis=0, ddof=1)],
        "p10_curve_db": [float(v) for v in np.percentile(bins, 10, axis=0)],
        "p90_curve_db": [float(v) for v in np.percentile(bins, 90, axis=0)],
        "mean_residual_db": [float(v) for v in res.mean(axis=0)],
        "mean_bands_db": [float(v) for v in mu],
        "std_bands_db": [float(v) for v in bands.std(axis=0, ddof=1)],
        "band_t": [float(v) for v in mu / np.maximum(se, 1e-9)],
        "mean_register_diff_db": [float(v) for v in regd.mean(axis=0)],
        "register_diff_std_db": [float(v) for v in regd.std(axis=0, ddof=1)],
    }
