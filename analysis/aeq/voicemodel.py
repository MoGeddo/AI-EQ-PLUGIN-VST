"""Voice-model correction, consensus statistics and parametric EQ fitting.

Why this module exists
----------------------
A finished vocal stem is  spectrum = voice(source+tract) x mic x engineer-EQ x comp.
You cannot invert that without the raw take, so the only identifiable question is:

    "which spectral features are SHARED across singers with different voices?"

Anything that survives (a) removal of each song's own level and tilt, and
(b) regression of the pitch (register) confound, is production - i.e. the mix
signature - because 13 different larynxes and 13 different mics would not agree
on the same bump by accident.

The output of this module is therefore a *target curve* plus its confidence
interval, not a claim about any single plugin setting.
"""
from __future__ import annotations

import numpy as np

EPS = 1e-20


# --------------------------------------------------------------------------- #
# register (pitch) confound
# --------------------------------------------------------------------------- #
def register_slopes(song: dict, band_centers: np.ndarray) -> np.ndarray | None:
    """d(dB)/d(log2 F0) per band for one song, from its F0-binned spectra."""
    bins = song.get("f0_bins") or []
    if len(bins) < 4:
        return None
    f0 = np.array([b["f0"] for b in bins])
    B = np.array([b["bands"] for b in bins])          # (n_bins, n_bands)
    x = np.log2(f0)
    x = x - x.mean()
    A = np.vstack([x, np.ones_like(x)]).T
    coef, *_ = np.linalg.lstsq(A, B, rcond=None)
    return coef[0]


def corpus_register_slope(songs: list, band_centers: np.ndarray):
    """Mean voice-driven slope per band + consistency across songs."""
    S = [s for s in (register_slopes(x, band_centers) for x in songs) if s is not None]
    if not S:
        return None, None, None
    S = np.stack(S)
    return S.mean(axis=0), S.std(axis=0, ddof=1), S


def voice_normalised_curve(song: dict, grid: np.ndarray, beta: np.ndarray,
                           f0_ref: float, band_centers: np.ndarray) -> np.ndarray:
    """Remove the register effect from a song's normalised LTAS (grid domain).

    The correction is evaluated per 1/3-octave band and interpolated back to the
    fine grid, so the curve keeps its shape but loses the 'this singer is higher
    pitched' component.
    """
    from . import spectral as sp

    curve = np.array(song["ltas_bins_db"], dtype=float)
    ltas = np.array(song["ltas_raw_bins_db"], dtype=float)
    bands = sp.band_levels(ltas, grid, band_centers)
    df0 = np.log2(max(song["med_f0_hz"], 1.0) / f0_ref)
    # below ~160 Hz a 1/3-octave band holds only one or two harmonics, so the band
    # level depends on harmonic alignment rather than on the voice - the estimate
    # there is dominated by noise (between-song sd 13-29 dB), so it is not applied.
    valid = band_centers >= 160.0
    corr_bands = np.where(valid, beta * df0, 0.0)
    corr = np.interp(np.log2(grid), np.log2(band_centers), corr_bands)
    return curve - corr


# --------------------------------------------------------------------------- #
# RBJ biquads (identical maths to what the plugin will run)
# --------------------------------------------------------------------------- #
def _rbj(kind: str, f0: float, p: float, gain_db: float, fs: float = 48000.0):
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2 * np.pi * min(f0, fs * 0.49) / fs
    cw, sw = np.cos(w0), np.sin(w0)
    if kind == "peak":
        Q = max(p, 0.1)
        alpha = sw / (2 * Q)
        b = [1 + alpha * A, -2 * cw, 1 - alpha * A]
        a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    elif kind == "lowshelf":
        S = max(p, 0.1)
        alpha = sw / 2 * np.sqrt((A + 1 / A) * (1 / S - 1) + 2)
        sq = 2 * np.sqrt(A) * alpha
        b = [A * ((A + 1) - (A - 1) * cw + sq), 2 * A * ((A - 1) - (A + 1) * cw),
             A * ((A + 1) - (A - 1) * cw - sq)]
        a = [(A + 1) + (A - 1) * cw + sq, -2 * ((A - 1) + (A + 1) * cw),
             (A + 1) + (A - 1) * cw - sq]
    elif kind == "highshelf":
        S = max(p, 0.1)
        alpha = sw / 2 * np.sqrt((A + 1 / A) * (1 / S - 1) + 2)
        sq = 2 * np.sqrt(A) * alpha
        b = [A * ((A + 1) + (A - 1) * cw + sq), -2 * A * ((A - 1) + (A + 1) * cw),
             A * ((A + 1) + (A - 1) * cw - sq)]
        a = [(A + 1) - (A - 1) * cw + sq, 2 * ((A - 1) - (A + 1) * cw),
             (A + 1) - (A - 1) * cw - sq]
    else:
        raise ValueError(kind)
    b = np.array(b) / a[0]
    a = np.array(a) / a[0]
    return b, a


def biquad_mag_db(kind: str, f0: float, p: float, gain_db: float, freqs: np.ndarray,
                  fs: float = 48000.0) -> np.ndarray:
    b, a = _rbj(kind, f0, p, gain_db, fs)
    z = np.exp(-1j * 2 * np.pi * np.clip(freqs, 1.0, fs * 0.499) / fs)
    num = b[0] + b[1] * z + b[2] * z ** 2
    den = a[0] + a[1] * z + a[2] * z ** 2
    return 20 * np.log10(np.abs(num / den) + EPS)


def eq_curve(bands: list, freqs: np.ndarray, fs: float = 48000.0) -> np.ndarray:
    out = np.zeros_like(freqs, dtype=float)
    for b in bands:
        out += biquad_mag_db(b["type"], b["f0"], b.get("q", b.get("s", 1.0)), b["gain_db"], freqs, fs)
    return out


# --------------------------------------------------------------------------- #
# parametric fit
# --------------------------------------------------------------------------- #
def _place_band(resid_db, freqs, kind: str, fmin: float, fmax: float) -> dict:
    """Place one band at the strongest remaining extremum (engineer-style)."""
    m = (freqs >= fmin) & (freqs <= fmax)
    r = np.where(m, resid_db, 0.0)
    k = int(np.argmax(np.abs(r)))
    f0 = float(np.clip(freqs[k], fmin, fmax))
    g = float(np.clip(r[k], -12.0, 12.0))
    if kind == "peak":
        # width from the half-prominence of the local extremum
        half = abs(g) * 0.5
        lo = k
        while lo > 0 and abs(r[lo]) > half and np.sign(r[lo]) == np.sign(g):
            lo -= 1
        hi = k
        while hi < len(r) - 1 and abs(r[hi]) > half and np.sign(r[hi]) == np.sign(g):
            hi += 1
        bw_oct = np.log2(freqs[min(hi, len(freqs) - 1)] / max(freqs[lo], 1e-3))
        q = float(np.clip(1.0 / max(bw_oct, 1e-3) * 0.7, 0.4, 4.0))
        return {"type": "peak", "f0": f0, "q": q, "gain_db": g}
    return {"type": kind, "f0": float(np.clip(f0, 60.0, 16000.0)), "s": 1.0, "gain_db": g}


def fit_parametric(freqs: np.ndarray, target_db: np.ndarray, n_peaks: int = 5,
                   fmin: float = 40.0, fmax: float = 16000.0, fs: float = 48000.0) -> list:
    """Fit shelf + n_peaks + shelf to a curve: greedy placement, then joint refine."""
    from scipy.optimize import least_squares

    m = (freqs >= fmin) & (freqs <= fmax)
    f, y = freqs[m], target_db[m]
    kinds = ["lowshelf"] + ["peak"] * n_peaks + ["highshelf"]
    bands: list = []
    resid = y.copy()
    for kind in kinds:
        b = _place_band(resid, f, kind, fmin, fmax)
        bands.append(b)
        resid = resid - biquad_mag_db(b["type"], b["f0"], b.get("q", 1.0), b["gain_db"], f, fs)

    def unpack(p):
        out = []
        for i, b in enumerate(bands):
            if b["type"] == "peak":
                q = float(np.clip(p[3 * i + 1], 0.4, 4.0))
                out.append({"type": "peak", "f0": float(np.exp(p[3 * i])), "q": q,
                            "gain_db": float(p[3 * i + 2])})
            else:
                out.append({"type": b["type"], "f0": float(np.exp(p[3 * i])), "s": 1.0,
                            "gain_db": float(p[3 * i + 2])})
        return out

    def model(p):
        bb = [{**b, "q": b.get("q", 1.0)} for b in unpack(p)]
        return eq_curve(bb, f, fs)

    x0 = np.array([[np.log(b["f0"]), b.get("q", 1.0), b["gain_db"]] for b in bands]).ravel()
    lo = np.tile([np.log(fmin), 0.4, -14.0], len(bands))
    hi = np.tile([np.log(fmax), 4.0, 14.0], len(bands))
    try:
        r = least_squares(lambda p: model(p) - y, x0, bounds=(lo, hi), loss="soft_l1", f_scale=0.5)
        if r.cost <= least_squares(lambda p: model(p) - y, x0, bounds=(lo, hi)).cost:
            bands = unpack(r.x)
    except Exception:
        pass
    bands = [{k: (1.0 if k == "q" and v is None else v) for k, v in b.items()} for b in bands]
    bands = [{k: v for k, v in b.items() if v is not None} for b in bands]
    bands.sort(key=lambda b: b["f0"])
    return bands


def fit_quality(freqs: np.ndarray, target_db: np.ndarray, bands: list,
                fmin: float = 40.0, fmax: float = 16000.0, fs: float = 48000.0) -> dict:
    m = (freqs >= fmin) & (freqs <= fmax)
    mod = eq_curve(bands, freqs[m], fs)
    err = mod - target_db[m]
    return dict(rms_db=float(np.sqrt(np.mean(err ** 2))),
                max_err_db=float(np.max(np.abs(err))),
                r2=float(1 - np.sum(err ** 2) / max(np.sum((target_db[m] - target_db[m].mean()) ** 2), EPS)))
