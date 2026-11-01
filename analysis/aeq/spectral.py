"""Spectral measurement core: chunked STFT, LTAS, octave bands, tilts, VAD, F0, HNR."""
from __future__ import annotations

import numpy as np

EPS = 1e-20


# --------------------------------------------------------------------------- #
# chunked STFT
# --------------------------------------------------------------------------- #
def n_frames(n: int, n_fft: int, hop: int) -> int:
    return max(0, 1 + (n - n_fft) // hop) if n >= n_fft else 0


def iter_stft(x: np.ndarray, sr: int, n_fft: int = 8192, hop: int = 2048, block: int = 128):
    """Yield dicts of {start, psd, rms_db, flatness} — memory bounded."""
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n = len(x)
    if n < n_fft:
        x = np.pad(x, (0, n_fft - n))
        n = n_fft
    w = np.hanning(n_fft)
    wss = np.sum(w ** 2)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    total = n_frames(n, n_fft, hop)
    for s in range(0, total, block):
        e = min(s + block, total)
        idx = np.arange(s, e)[:, None] * hop + np.arange(n_fft)[None, :]
        fr = x[idx] * w[None, :]
        spec = np.fft.rfft(fr, axis=1)
        psd = (np.abs(spec) ** 2) / (sr * wss)
        rms = np.sqrt(np.mean(fr ** 2, axis=1))
        rms_db = 20 * np.log10(rms + EPS)
        p = psd + EPS
        flat = np.exp(np.mean(np.log(p), axis=1)) / (np.mean(p, axis=1) + EPS)
        yield dict(start=s, psd=psd, freqs=freqs, rms_db=rms_db, flatness=flat)
        del fr, spec, psd, p


def stft_psd(x: np.ndarray, sr: int, n_fft: int = 8192, hop: int = 2048):
    """Convenience full-file STFT for short signals (< ~60 s)."""
    blocks = list(iter_stft(x, sr, n_fft, hop, block=256))
    if not blocks:
        raise ValueError("signal too short")
    return (np.concatenate([b["psd"] for b in blocks]),
            blocks[0]["freqs"],
            np.concatenate([b["rms_db"] for b in blocks]),
            np.concatenate([b["flatness"] for b in blocks]))


# --------------------------------------------------------------------------- #
# log grid + vectorized resampling / band integration
# --------------------------------------------------------------------------- #
def log_grid(fmin: float = 20.0, fmax: float = 20000.0, ppb: int = 96) -> np.ndarray:
    n = int(np.ceil(np.log2(fmax / fmin) * ppb)) + 1
    return fmin * 2.0 ** (np.arange(n) / ppb)


class GridMapper:
    """Vectorized linear interpolation of many PSD frames onto a log grid (dB)."""

    def __init__(self, freqs: np.ndarray, grid: np.ndarray):
        self.grid = grid
        lf = np.log(np.maximum(freqs[1:], 1e-6))
        self.lf = lf
        u = np.log(grid)
        i = np.clip(np.searchsorted(lf, u) - 1, 0, len(lf) - 2)
        self.i = i
        self.w = (u - lf[i]) / (lf[i + 1] - lf[i])
        self.valid = (u >= lf[0]) & (u <= lf[-1])

    def to_db(self, psd: np.ndarray) -> np.ndarray:
        p = psd[:, 1:]
        a = p[:, self.i]
        b = p[:, self.i + 1]
        v = a + self.w[None, :] * (b - a)
        db = 10.0 * np.log10(v + EPS)
        db[:, ~self.valid] = db[:, self.valid][:, -1:] if self.valid.any() else -200.0
        return db


def band_edges(fc: np.ndarray, fraction: int = 3):
    r = 2.0 ** (1.0 / (2 * fraction))
    return fc / r, fc * r


def octave_centers(fmin: float = 40.0, fmax: float = 16000.0, fraction: int = 3) -> np.ndarray:
    if fraction == 3:
        base = np.array([10.0, 12.5, 16.0, 20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0,
                         125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0,
                         1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0,
                         10000.0, 12500.0, 16000.0, 20000.0])
        return base[(base >= fmin) & (base <= fmax)]
    n = int(np.ceil(np.log2(fmax / fmin) * fraction))
    return fmin * 2.0 ** (np.arange(n) / fraction)


def band_levels(curve_db, grid: np.ndarray, fc: np.ndarray, fraction: int = 3,
                power_avg: bool = True) -> np.ndarray:
    """Integrate dB curve(s) on the log grid into fractional-octave bands (dB).

    Accepts shape (n,) or (n, grid).
    """
    lo, hi = band_edges(np.asarray(fc), fraction)
    c = np.atleast_2d(curve_db)
    out = np.empty((c.shape[0], len(fc)))
    for i, (a, b) in enumerate(zip(lo, hi)):
        m = (grid >= a) & (grid < b)
        if not m.any():
            centre = 0.5 * (np.log(a) + np.log(b))
            out[:, i] = np.interp(centre, np.log(grid), c[0]) if c.shape[0] == 1 else np.nan
        elif power_avg:
            out[:, i] = 10 * np.log10(np.mean(10 ** (c[:, m] / 10.0), axis=1) + EPS)
        else:
            out[:, i] = np.mean(c[:, m], axis=1)
    return out[0] if np.ndim(curve_db) == 1 else out


# --------------------------------------------------------------------------- #
# VAD
# --------------------------------------------------------------------------- #
def voice_activity(rms_db: np.ndarray, flatness: np.ndarray) -> dict:
    p5, p50, p95 = np.percentile(rms_db, [5, 50, 95])
    floor = max(p5, p95 - 55.0)
    active = (rms_db > floor + 9.0) & (rms_db > p95 - 42.0)
    if not active.any():
        active = rms_db > (p95 - 20.0)
    fl = np.percentile(flatness[active], 80) if active.any() else np.inf
    unvoiced = active & (flatness > fl)
    voiced = active & ~unvoiced
    return dict(active=active, voiced=voiced, unvoiced=unvoiced,
                floor_db=float(floor), p50=float(p50), p95=float(p95))


def vad_fast(x: np.ndarray, sr: int, win: float = 0.020, hop: float = 0.010):
    """Cheap 10 ms time-domain VAD (RMS + 512-pt spectral flatness). Returns dict of
    arrays aligned to hop grid, frame centre = k*hop + win/2.
    """
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n, h = int(win * sr), int(hop * sr)
    total = max(0, 1 + (len(x) - n) // h)
    rms_db = np.empty(total)
    flat = np.empty(total)
    nfft = 1024
    w = np.hanning(n)
    for s in range(0, total, 2048):
        e = min(s + 2048, total)
        idx = np.arange(s, e)[:, None] * h + np.arange(n)[None, :]
        fr = x[idx]
        rms_db[s:e] = 20 * np.log10(np.sqrt(np.mean(fr ** 2, axis=1)) + EPS)
        sp = np.abs(np.fft.rfft(fr * w[None, :], nfft, axis=1)) ** 2 + EPS
        flat[s:e] = np.exp(np.mean(np.log(sp), axis=1)) / (np.mean(sp, axis=1) + EPS)
    act = voice_activity(rms_db, flat)
    act["rms_db"] = rms_db
    act["flatness"] = flat
    act["hop_s"] = hop
    act["win_s"] = win
    return act


# --------------------------------------------------------------------------- #
# F0 / HNR (chunked)
# --------------------------------------------------------------------------- #
def f0_autocorr(x: np.ndarray, sr: int, win: float = 0.045, hop: float = 0.010,
                fmin: float = 65.0, fmax: float = 520.0, block: int = 512):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n = int(win * sr)
    h = int(hop * sr)
    if len(x) < n:
        return np.zeros(0), np.zeros(0)
    total = 1 + (len(x) - n) // h
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    w = np.hanning(n)
    lag_lo = max(1, int(sr / fmax))
    lag_hi = min(int(sr / fmin), n - 2)
    f0s, peaks = [], []
    for s in range(0, total, block):
        e = min(s + block, total)
        idx = np.arange(s, e)[:, None] * h + np.arange(n)[None, :]
        fr = x[idx]
        fr = (fr - fr.mean(axis=1, keepdims=True)) * w[None, :]
        S = np.fft.rfft(fr, nfft, axis=1)
        ac = np.fft.irfft(np.abs(S) ** 2, nfft, axis=1)[:, :n]
        ac = ac / (ac[:, :1] + EPS)
        seg = ac[:, lag_lo:lag_hi]
        k = np.argmax(seg, axis=1)
        peak = seg[np.arange(e - s), k]
        # sub-octave correction: prefer the shortest lag whose peak is within 15 %
        # of the global maximum (avoids locking onto a harmonic of F0)
        thr = 0.85 * peak[:, None]
        above = seg >= thr
        first = np.argmax(above, axis=1)
        k = np.where(above.any(axis=1), first, k)
        peak = seg[np.arange(e - s), k]
        lag = k + lag_lo
        a = ac[np.arange(e - s), np.maximum(lag - 1, 1)]
        b = ac[np.arange(e - s), lag]
        c = ac[np.arange(e - s), np.minimum(lag + 1, n - 2)]
        den = a - 2 * b + c
        shift = np.where(np.abs(den) > 1e-9, 0.5 * (a - c) / np.where(np.abs(den) > 1e-9, den, 1), 0.0)
        lag_f = lag + np.clip(shift, -1, 1)
        f0 = sr / np.maximum(lag_f, 1.0)
        f0s.append(f0)
        peaks.append(np.clip(peak, 0, 1))
    f0 = np.concatenate(f0s)
    pk = np.concatenate(peaks)
    voiced = pk > 0.35
    return np.where(voiced, f0, 0.0), pk


# --------------------------------------------------------------------------- #
# shape descriptors
# --------------------------------------------------------------------------- #
def tilt_db_per_oct(curve_db: np.ndarray, grid: np.ndarray, f1: float = 100.0, f2: float = 8000.0) -> float:
    m = (grid >= f1) & (grid <= f2)
    if m.sum() < 8:
        return float("nan")
    x = np.log2(grid[m])
    A = np.vstack([x, np.ones_like(x)]).T
    return float(np.linalg.lstsq(A, curve_db[m], rcond=None)[0][0])


def fit_line(curve_db: np.ndarray, grid: np.ndarray, f1: float = 80.0, f2: float = 12000.0):
    m = (grid >= f1) & (grid <= f2)
    x = np.log2(grid[m])
    A = np.vstack([x, np.ones_like(x)]).T
    k, b = np.linalg.lstsq(A, curve_db[m], rcond=None)[0]
    return float(k), float(b)


def residual_vs_tilt(curve_db: np.ndarray, grid: np.ndarray, f1: float = 80.0, f2: float = 12000.0) -> np.ndarray:
    k, b = fit_line(curve_db, grid, f1, f2)
    return curve_db - (k * np.log2(grid) + b)


def centroid_hz(curve_db: np.ndarray, grid: np.ndarray) -> float:
    p = 10 ** (curve_db / 10.0)
    return float(np.sum(grid * p) / (np.sum(p) + EPS))


def rolloff_hz(curve_db: np.ndarray, grid: np.ndarray, pct: float = 0.85) -> float:
    p = 10 ** (curve_db / 10.0)
    c = np.cumsum(p)
    i = int(np.searchsorted(c, pct * c[-1]))
    return float(grid[min(i, grid.size - 1)])


def smooth_logfreq(curve_db: np.ndarray, octaves: float = 1 / 12, ppb: int = 96) -> np.ndarray:
    from scipy.ndimage import gaussian_filter1d

    return gaussian_filter1d(np.asarray(curve_db), sigma=max(0.5, octaves * ppb), mode="nearest")


def find_notches(curve_db: np.ndarray, grid: np.ndarray, f1: float = 3000.0, f2: float = 14000.0,
                 min_prom_db: float = 1.0) -> list:
    from scipy.signal import find_peaks

    m = (grid >= f1) & (grid <= f2)
    pk, props = find_peaks(-curve_db[m], prominence=min_prom_db)
    return [dict(freq=float(grid[m][i]), depth_db=float(props["prominences"][j])) for j, i in enumerate(pk)]


def f0_yin(x: np.ndarray, sr: int, win: float = 0.040, hop: float = 0.010,
           fmin: float = 65.0, fmax: float = 1000.0, thresh: float = 0.15,
           block: int = 512):
    """YIN pitch tracker (de Cheveigne & Kawahara 2002): difference function,
    cumulative-mean normalisation, absolute threshold, parabolic refinement.

    Returns (f0, aperiodicity) per hop frame; f0 = 0 where unvoiced.
    Robust to a weak fundamental (high-passed vocals), unlike raw autocorrelation.
    """
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n, h = int(win * sr), int(hop * sr)
    if len(x) < 2 * n:
        return np.zeros(0), np.ones(0)
    total = 1 + (len(x) - 2 * n) // h
    tau_min = max(2, int(sr / fmax))
    tau_max = int(sr / fmin)
    W = n  # integration window
    f0s, apers = [], []
    for s in range(0, total, block):
        e = min(s + block, total)
        idx = np.arange(s, e)[:, None] * h + np.arange(n)[None, :]
        fr = x[idx]
        fr = fr - fr.mean(axis=1, keepdims=True)
        nfft = 1 << int(np.ceil(np.log2(n + tau_max + 1)))
        S = np.fft.rfft(fr, nfft, axis=1)
        ac = np.fft.irfft(np.abs(S) ** 2, nfft, axis=1)
        e0 = ac[:, :1]                                   # r(0)
        taus = np.arange(tau_max + 1)
        # r_tau(0): energy of the shifted window
        cs = np.concatenate([np.zeros((fr.shape[0], 1)), np.cumsum(fr ** 2, axis=1)], axis=1)
        etau = cs[:, np.minimum(taus + n, n)] - cs[:, taus]
        d = np.maximum(e0 + etau - 2 * ac[:, : tau_max + 1], 0.0)
        d[:, 0] = 0.0
        cm = np.ones_like(d)
        run = np.cumsum(d[:, 1:], axis=1)
        t = np.arange(1, tau_max + 1)
        cm[:, 1:] = d[:, 1:] / (run / t[None, :] + 1e-12)
        seg = cm[:, tau_min:tau_max]
        below = seg < thresh
        k = np.where(below.any(axis=1), np.argmax(below, axis=1), np.argmin(seg, axis=1))
        aper = seg[np.arange(e - s), k]
        tau = k + tau_min
        # parabolic interpolation on the CMND dip
        a = cm[np.arange(e - s), np.maximum(tau - 1, 1)]
        b = cm[np.arange(e - s), tau]
        c = cm[np.arange(e - s), np.minimum(tau + 1, tau_max)]
        den = a - 2 * b + c
        shift = np.where(np.abs(den) > 1e-12, 0.5 * (a - c) / np.where(np.abs(den) > 1e-12, den, 1), 0.0)
        f0 = sr / np.maximum(tau + np.clip(shift, -1, 1), 1.0)
        voiced = (aper < 0.35)
        f0s.append(np.where(voiced, f0, 0.0))
        apers.append(aper)
    return np.concatenate(f0s), np.concatenate(apers)
