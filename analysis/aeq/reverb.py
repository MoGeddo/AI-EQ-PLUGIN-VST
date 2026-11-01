"""Reverb / space measurement from a de-reverbed-wet stem pair.

The corpus ships a dry vocal and the isolated *reverb* component of the same
vocal (dereverb model output = wet residual).  That makes possible things that
are normally impossible on a finished mix:

  * exact wet/dry balance per song
  * RT60 by Schroeder backward integration on the reverb-only signal
  * early-reflection / pre-delay estimate by envelope cross-correlation
  * the spectral colour of the space (reverb EQ + send filtering)
"""
from __future__ import annotations

import numpy as np

EPS = 1e-20


def wet_fraction(dry: np.ndarray, wet: np.ndarray, sr: int) -> dict:
    """Wet/dry energy balance and coherence."""
    d = dry.mean(axis=1) if dry.ndim > 1 else dry
    w = wet.mean(axis=1) if wet.ndim > 1 else wet
    n = min(len(d), len(w))
    d, w = d[:n].astype(np.float64), w[:n].astype(np.float64)
    ed, ew = np.mean(d ** 2), np.mean(w ** 2)
    return dict(
        wet_over_dry_db=float(10 * np.log10((ew + EPS) / (ed + EPS))),
        wet_fraction=float(ew / (ed + ew + EPS)),
        corr=float(np.corrcoef(d, w)[0, 1]) if ed > 0 and ew > 0 else float("nan"),
    )


def decay_time(wet: np.ndarray, sr: int, fmin: float = 150.0, fmax: float = 8000.0,
               slope_win_db: float = 25.0) -> dict:
    """RT60 estimate from reverb-only signal.

    Uses the ensemble of decay segments that follow vocal offsets: locate offsets
    from the wet envelope itself (the wet stem is silence when nothing rings),
    take T20-style slopes between -5 and -25 dB, and robustly average.
    """
    w = wet.mean(axis=1) if wet.ndim > 1 else wet
    w = np.asarray(w, dtype=np.float64)
    # band-limit
    from scipy.signal import butter, sosfilt

    ny = sr / 2
    sos = butter(4, [fmin / ny, min(fmax / ny, 0.99)], btype="band", output="sos")
    w = sosfilt(sos, w)
    win = int(0.010 * sr)  # 10 ms resolution
    n = len(w) // win
    if n < 50:
        return dict(rt60=float("nan"), n_decays=0)
    e = np.mean(w[: n * win].reshape(n, win) ** 2, axis=1)
    db = 10 * np.log10(e + EPS)
    peak = db.max()
    # segments: search windows of 1.2 s where level falls monotonically
    seg_len = int(1.2 / 0.010)
    slopes = []
    for s in range(0, n - seg_len, seg_len // 2):
        seg = db[s : s + seg_len]
        if seg[0] < peak - 30 or seg[0] > peak - 3:
            continue
        t = np.arange(seg_len) * 0.010
        # fit until 25 dB below the segment start, requiring at least 0.15 s
        rel = seg - seg[0]
        idx = np.where(rel <= -slope_win_db)[0]
        end = idx[0] if idx.size else seg_len
        if end < 15:
            continue
        y = rel[:end]
        if y[0] - y[end - 1] < 12:  # not a clean decay
            continue
        A = np.vstack([t[:end], np.ones(end)]).T
        k = np.linalg.lstsq(A, y, rcond=None)[0][0]
        if k < -1.0:  # ignore absurd
            continue
        slopes.append(k)
    if not slopes:
        return dict(rt60=float("nan"), n_decays=0)
    k = float(np.median(slopes))
    return dict(rt60=float(-60.0 / k) if k < 0 else float("nan"),
                n_decays=len(slopes), decay_slope_db_per_s=k)


def predelay_ms(dry: np.ndarray, wet: np.ndarray, sr: int, max_ms: float = 120.0) -> dict:
    """Cross-correlate envelopes to find the reverb's onset offset (pre-delay proxy)."""
    d = dry.mean(axis=1) if dry.ndim > 1 else dry
    w = wet.mean(axis=1) if wet.ndim > 1 else wet
    n = min(len(d), len(w))
    d, w = d[:n].astype(np.float64), w[:n].astype(np.float64)
    hop = int(0.002 * sr)  # 2 ms
    m = n // hop
    if m < 64:
        return dict(predelay_ms=float("nan"))
    ed = np.sqrt(np.mean(d[: m * hop].reshape(m, hop) ** 2, axis=1))
    ew = np.sqrt(np.mean(w[: m * hop].reshape(m, hop) ** 2, axis=1))
    ed = ed / (ed.max() + EPS)
    ew = ew / (ew.max() + EPS)
    maxlag = int(max_ms / 2.0)
    best, bl = -1e9, 0
    for lag in range(0, maxlag + 1):
        a = ed[: m - lag] if lag else ed
        b = ew[lag:] if lag else ew
        v = float(np.dot(a, b))
        if v > best:
            best, bl = v, lag
    return dict(predelay_ms=float(bl * 2.0), corr=float(best / max(1e-9, np.dot(ed, ed))))


def space_colour(wet: np.ndarray, sr: int, grid: np.ndarray) -> np.ndarray:
    """LTAS of the reverb-only signal (dB) on the analysis grid (streamed)."""
    from . import spectral as sp

    mapper, acc, n = None, np.zeros(grid.size), 0
    for blk in sp.iter_stft(wet, sr, 8192, 2048, block=96):
        if mapper is None:
            mapper = sp.GridMapper(blk["freqs"], grid)
        acc += (10 ** (mapper.to_db(blk["psd"]) / 10.0)).sum(axis=0)
        n += blk["psd"].shape[0]
    return 10 * np.log10(acc / max(n, 1) + EPS)


# --------------------------------------------------------------------------- #
# impulse response estimation  (the strong method)
# --------------------------------------------------------------------------- #
def estimate_ir(dry: np.ndarray, wet: np.ndarray, sr: int, n_fft: int = 65536) -> dict:
    """Least-squares (Wiener) estimate of the reverb path: wet ~ dry * h.

    Because the dry and wet stems are sample-aligned, a single cross-spectral
    division over minutes of signal gives a very low-variance IR estimate:
        Pwd = E[W D*],  Pdd = E[|D|^2],  H = Pwd / (Pdd + eps)
    Returns the IR, its band magnitude in dB, and a fit-quality figure.
    """
    d = np.asarray(dry.mean(axis=1) if dry.ndim > 1 else dry, dtype=np.float64)
    w = np.asarray(wet.mean(axis=1) if wet.ndim > 1 else wet, dtype=np.float64)
    n = min(len(d), len(w))
    d, w = d[:n], w[:n]
    hop = n_fft // 2
    win = np.hanning(n_fft)
    Pdd = np.zeros(n_fft // 2 + 1)
    Pwd = np.zeros(n_fft // 2 + 1, dtype=np.complex128)
    blocks = 0
    for s in range(0, n - n_fft + 1, hop):
        D = np.fft.rfft(d[s:s + n_fft] * win)
        W = np.fft.rfft(w[s:s + n_fft] * win)
        Pdd += np.abs(D) ** 2
        Pwd += W * np.conj(D)
        blocks += 1
    if blocks == 0:
        return dict(ok=False, reason="too short")
    eps = 1e-4 * float(np.mean(Pdd))
    H = Pwd / (Pdd + eps)
    ir = np.fft.irfft(H, n_fft)
    # fit quality: reconstruct wet from dry with the estimated IR (overlap-add)
    err = 0.0
    num = 0.0
    for s in range(0, n - n_fft + 1, hop):
        D = np.fft.rfft(d[s:s + n_fft] * win)
        W = np.fft.rfft(w[s:s + n_fft] * win)
        Y = D * H
        err += float(np.sum(np.abs(W - Y) ** 2))
        num += float(np.sum(np.abs(W) ** 2))
    return dict(ok=True, ir=ir, H=H, sr=sr, blocks=blocks,
                fit_r2=max(0.0, 1.0 - err / max(num, EPS)))


def ir_metrics(ir: np.ndarray, sr: int) -> dict:
    """Pre-delay, RT60 (Schroeder, T20 extrapolated) and D/R ratio from an IR."""
    e = ir.astype(np.float64) ** 2
    pk = int(np.argmax(e))
    tot = e.sum() + EPS
    # pre-delay: first sample (from 0) exceeding 1 % of the IR's peak amplitude
    thr = 0.01 * np.sqrt(e[pk])
    pre = int(np.argmax(np.abs(ir) > thr)) if np.any(np.abs(ir) > thr) else pk
    pre_ms = pre * 1000.0 / sr
    # early/late balance
    early = e[: int(0.080 * sr)].sum()
    dr = 10 * np.log10((early + EPS) / (tot - early + EPS))
    # Schroeder backwards integration -> T20 in [-5,-25] dB
    sch = np.cumsum(e[::-1])[::-1]
    with np.errstate(divide="ignore"):
        sdb = 10 * np.log10(sch / (sch[0] + EPS) + EPS)
    try:
        i5 = int(np.argmax(sdb < -5.0))
        i25 = int(np.argmax(sdb < -25.0))
        if i25 > i5 + 10:
            t = np.arange(i5, i25) / sr
            k = np.polyfit(t, sdb[i5:i25], 1)[0]
            rt60 = float(-60.0 / k) if k < 0 else float("nan")
        else:
            rt60 = float("nan")
    except Exception:
        rt60 = float("nan")
    # energy centre of the tail
    tt = np.arange(len(e)) / sr
    cent = float(np.sum(tt * e) / (tot))
    return dict(predelay_ms=float(pre_ms), rt60=rt60, dr_db=float(dr),
                ir_peak_ms=float(pk * 1000 / sr), tail_centroid_s=cent,
                ir_len_ms=float(1000 * len(ir) / sr))


def ir_colour(ir: np.ndarray, sr: int, grid: np.ndarray, fc: np.ndarray) -> np.ndarray:
    """Band magnitudes of the IR (the 'colour' of the space), in dB."""
    from . import spectral as sp

    n_fft = 1 << int(np.ceil(np.log2(max(len(ir), 4096))))
    H = np.abs(np.fft.rfft(ir, n_fft))
    fr = np.fft.rfftfreq(n_fft, 1 / sr)
    db = 10 * np.log10(np.interp(grid, fr, H) ** 2 + EPS)
    return sp.band_levels(db, grid, fc)


def rt60_ensemble(wet: np.ndarray, sr: int, fmin: float = 200.0, fmax: float = 8000.0,
                  win_s: float = 2.0) -> dict:
    """RT60 by ensemble-averaged decay (the standard room-acoustics approach).

    The reverb-only stem is silent except where reverb rings; every vocal offset
    therefore starts a free decay.  We detect offsets, cut decays, aligned on the
    -5 dB crossing, average in the power domain (noise-robust) and Schroeder
    integrate the ensemble mean to get T30 -> RT60.
    """
    from scipy.signal import butter, sosfilt

    w = np.asarray(wet.mean(axis=1) if wet.ndim > 1 else wet, dtype=np.float64)
    ny = sr / 2
    sos = butter(4, [fmin / ny, min(fmax / ny, 0.99)], btype="band", output="sos")
    w = sosfilt(sos, w)
    hop_s = 0.005
    hop = int(hop_s * sr)
    n = len(w) // hop
    if n < 400:
        return dict(rt60=float("nan"), n_decays=0)
    e = np.mean(w[: n * hop].reshape(n, hop) ** 2, axis=1)
    db = 10 * np.log10(e + EPS)
    # smooth
    k = np.ones(3) / 3
    dbs = np.convolve(db, k, mode="same")
    nseg = int(win_s / hop_s)
    curves, starts = [], []
    for i in range(2, n - nseg):
        # offset: level now >= 20 dB above 100 ms later, i.e. a clear drop follows
        if dbs[i] - dbs[i + 20] < 12.0:
            continue
        seg = dbs[i : i + nseg]
        # find -5 dB crossing point within first 300 ms
        c = np.where(seg <= seg[0] - 5.0)[0]
        if c.size == 0:
            continue
        c0 = int(c[0])
        if c0 + int(1.5 / hop_s) >= nseg:
            continue
        curves.append(seg[c0:])
        starts.append(i + c0)
    curves = [c[: int(1.5 / hop_s)] for c in curves]
    if len(curves) < 8:
        return dict(rt60=float("nan"), n_decays=len(curves))
    M = np.mean(np.stack([10 ** (c / 10.0) for c in curves], axis=0), axis=0)
    sch = np.cumsum(M[::-1])[::-1]
    sdb = 10 * np.log10(sch / sch[0] + EPS)
    t = np.arange(len(sdb)) * hop_s
    try:
        i5 = int(np.argmax(sdb < -5))
        i25 = int(np.argmax(sdb < -25))
        if i25 <= i5 + 5:
            return dict(rt60=float("nan"), n_decays=len(curves))
        kk = np.polyfit(t[i5:i25], sdb[i5:i25], 1)[0]
        rt = float(-60.0 / kk) if kk < 0 else float("nan")
    except Exception:
        rt = float("nan")
    return dict(rt60=rt, n_decays=len(curves), decay_db_per_s=float(kk) if 'kk' in dir() else float("nan"))


def rt60_from_offsets(dry: np.ndarray, wet: np.ndarray, sr: int, fmin: float = 200.0,
                      fmax: float = 8000.0, min_gap_ms: float = 200.0,
                      tail_s: float = 1.5) -> dict:
    """RT60 from ensemble-averaged free decays after true phrase offsets.

    Offsets are detected on the DRY stem (the separated dry vocal is gated in the
    gaps, which gives exact offset times); the wet stem there is pure decay.
    """
    from scipy.signal import butter, sosfilt

    d = np.asarray(dry.mean(axis=1) if dry.ndim > 1 else dry, dtype=np.float64)
    w = np.asarray(wet.mean(axis=1) if wet.ndim > 1 else wet, dtype=np.float64)
    n = min(len(d), len(w))
    d, w = d[:n], w[:n]
    ny = sr / 2
    sos = butter(4, [fmin / ny, min(fmax / ny, 0.99)], btype="band", output="sos")
    w = sosfilt(sos, w)
    hop = int(0.005 * sr)
    m = n // hop
    ed = np.sqrt(np.mean(d[: m * hop].reshape(m, hop) ** 2, axis=1) + EPS)
    ew = np.mean(w[: m * hop].reshape(m, hop) ** 2, axis=1)
    lvl = 20 * np.log10(ed + EPS)
    thr = max(np.percentile(lvl, 95) - 40.0, -80.0)
    silent = lvl < thr
    # find offset = first frame of silence runs longer than min_gap
    need = int(min_gap_ms / 5.0)
    ntail = int(tail_s / 0.005)
    segs = []
    i = 0
    while i < m - need:
        if silent[i] and not silent[max(0, i - 1)]:
            j = i
            while j < m and silent[j]:
                j += 1
            if j - i >= need and i + ntail < m:
                segs.append(ew[i : i + ntail])
            i = j
        else:
            i += 1
    if len(segs) < 3:
        return dict(rt60=float("nan"), n_decays=len(segs))
    M = np.mean(np.stack(segs, axis=0), axis=0)
    if M[0] <= 0:
        return dict(rt60=float("nan"), n_decays=len(segs))
    # Schroeder integration from the end of the window
    sch = np.cumsum(M[::-1])[::-1]
    sdb = 10 * np.log10(sch / sch[0] + EPS)
    t = np.arange(len(sdb)) * 0.005
    # ignore the last 10 % (noise floor)
    lim = int(0.9 * len(sdb))
    sdb, t = sdb[:lim], t[:lim]
    i5 = int(np.argmax(sdb < -5.0))
    i25 = int(np.argmax(sdb < -25.0))
    if i25 <= i5 + 6:
        i25 = int(np.argmax(sdb < -15.0))
        scale = 60.0 / 10.0
    else:
        scale = 60.0 / 20.0
    if i25 <= i5 + 4:
        return dict(rt60=float("nan"), n_decays=len(segs))
    k = np.polyfit(t[i5:i25], sdb[i5:i25], 1)[0]
    rt = float(-scale / k) if k < 0 else float("nan")
    return dict(rt60=rt, n_decays=len(segs),
                tail_20ms_db=[float(v) for v in sdb[: int(0.4 / 0.005): int(0.02 / 0.005)]])
