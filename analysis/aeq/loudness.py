"""ITU-R BS.1770-4 loudness (LUFS), EBU R128 LRA, PLR and crest metrics.

K-weighting = 4 dB high shelf @1681.97 Hz + RLB high-pass @38.13 Hz; 400 ms
blocks with 75 % overlap; -70 LUFS absolute and -10 LU relative gating.
All block statistics are computed with cumulative sums (memory bounded).
"""
from __future__ import annotations

import numpy as np
from scipy.signal import lfilter

EPS = 1e-20


def _k_weighting(sr: int):
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / sr)
    Vh = 10.0 ** (G / 20.0)
    Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    b1 = np.array([(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0])
    a1 = np.array([1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0])
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / sr)
    b2 = np.array([1.0, -2.0, 1.0])
    a2 = np.array([1.0, 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K), (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K)])
    b2 = b2 / a2[0]
    return np.polymul(b1, b2), np.polymul(a1, a2)


def _block_ms(y: np.ndarray, blk: int, hop: int) -> np.ndarray:
    """Mean square per block via cumulative sum."""
    e = np.sum(y ** 2, axis=1) if y.ndim > 1 else y ** 2
    cs = np.concatenate([[0.0], np.cumsum(e)])
    starts = np.arange(0, len(e) - blk + 1, hop)
    return (cs[starts + blk] - cs[starts]) / blk


def integrated_lufs(x: np.ndarray, sr: int) -> float:
    y = np.atleast_2d(x.T).T if x.ndim == 1 else np.asarray(x, dtype=np.float64)
    b, a = _k_weighting(sr)
    y = lfilter(b, a, y, axis=0)
    blk = int(round(0.400 * sr))
    if y.shape[0] < blk:
        return float("nan")
    ms = _block_ms(y, blk, blk // 4)
    lufs = -0.691 + 10 * np.log10(ms + EPS)
    g = lufs > -70.0
    if not g.any():
        return float("nan")
    rel = -0.691 + 10 * np.log10(np.mean(ms[g]) + EPS) - 10.0
    g &= lufs > rel
    if not g.any():
        return float("nan")
    return float(-0.691 + 10 * np.log10(np.mean(ms[g]) + EPS))


def loudness_range(x: np.ndarray, sr: int) -> float:
    y = np.atleast_2d(x.T).T if x.ndim == 1 else np.asarray(x, dtype=np.float64)
    b, a = _k_weighting(sr)
    y = lfilter(b, a, y, axis=0)
    blk = int(round(3.0 * sr))
    if y.shape[0] < blk:
        return float("nan")
    ms = _block_ms(y, blk, int(0.1 * sr))
    lufs = -0.691 + 10 * np.log10(ms + EPS)
    lufs = lufs[lufs > -70.0]
    if lufs.size < 2:
        return float("nan")
    rel = -0.691 + 10 * np.log10(np.mean(10 ** ((lufs + 0.691) / 10.0))) - 20.0
    lufs = lufs[lufs > rel]
    if lufs.size < 2:
        return float("nan")
    return float(np.percentile(lufs, 95) - np.percentile(lufs, 10))


def dynamics(x: np.ndarray, sr: int) -> dict:
    m = x.mean(axis=1) if x.ndim > 1 else np.asarray(x, dtype=np.float64)
    lufs = integrated_lufs(x, sr)
    peak = float(np.max(np.abs(m)) + EPS)
    plr = 20 * np.log10(peak) - lufs if np.isfinite(lufs) else float("nan")
    w = max(1, int(0.05 * sr))
    n = len(m) // w
    if n < 4:
        return dict(lufs=lufs, plr=plr, lra=float("nan"))
    seg = m[: n * w].reshape(n, w)
    r = np.sqrt(np.mean(seg ** 2, axis=1) + EPS)
    lv = 20 * np.log10(r)
    act = lv[lv > lv.max() - 45]
    crest = float(np.mean(20 * np.log10(np.max(np.abs(seg), axis=1) + EPS) - lv))
    return dict(
        lufs=float(lufs), plr=float(plr), lra=float(loudness_range(x, sr)),
        crest_short_mean=crest,
        st_p10=float(np.percentile(act, 10)), st_p90=float(np.percentile(act, 90)),
        st_iqr=float(np.percentile(act, 75) - np.percentile(act, 25)),
        duty=float(act.size / max(1, lv.size)),
    )
