"""Audio I/O built on PyAV (bundled FFmpeg) -> float32 numpy."""
from __future__ import annotations

import numpy as np
import av


def load(path: str, sr: int | None = None, mono: bool = False) -> tuple[np.ndarray, int]:
    """Decode any file to float32 (frames, channels) at its native rate.

    Resampling (when requested) is done with scipy so no codec delay is added.
    """
    c = av.open(path)
    chunks = []
    try:
        st = next(s for s in c.streams if s.type == "audio")
        native = st.codec_context.sample_rate
        for frame in c.decode(st):
            a = frame.to_ndarray()
            if a.dtype != np.float32:
                a = a.astype(np.float32)
            if a.ndim == 1:
                a = a[None, :]
            # to_ndarray gives (channels, samples) for planar, (1, samples*ch) for packed
            if frame.format.is_planar:
                x = a.T
            else:
                x = a.reshape(-1, int(a.shape[1] / max(1, len(frame.layout.channels)))).reshape(-1, len(frame.layout.channels))
            chunks.append(x)
    finally:
        c.close()
    x = np.concatenate(chunks, axis=0)
    if sr is not None and sr != native:
        from scipy.signal import resample_poly
        from math import gcd

        g = gcd(int(sr), int(native))
        x = resample_poly(x, int(sr // g), int(native // g), axis=0).astype(np.float32)
        native = sr
    if x.shape[1] > 2:
        x = x[:, :2]
    if mono and x.shape[1] > 1:
        x = x.mean(axis=1, keepdims=True)
    return x, int(native)


def to_mono(x: np.ndarray) -> np.ndarray:
    return x.mean(axis=1) if x.ndim > 1 and x.shape[1] > 1 else (x[:, 0] if x.ndim > 1 else x)


def write_wav(path: str, x: np.ndarray, sr: int) -> None:
    from scipy.io import wavfile

    y = np.clip(x, -1.0, 1.0)
    wavfile.write(path, sr, (y * 32767.0).astype(np.int16))
