#!/usr/bin/env python3
"""compare_rulers.py - do the plugin's analyser and an independent FFT agree?

G76 proved that the rendered audio gets measurably closer to Amir's curve, but the two
meters still disagreed on the same file: the plugin reported 6.36 dB of residual where the
independent analysis saw 3.69 dB.  Two instruments reading the same signal differently is
the one defect a "match EQ" cannot afford, because every decision is a subtraction of one
from the other.

This script measures one file two ways and prints the difference:

    plugin  : aeq_cli profile -> the analyser's own band levels (its gate, its window,
              its 8192-point FFT, mean *spectral density* per 1/12-octave band)
    outside : a 4096-point FFT written here, band power summed per band, both as
              power-per-band (the convention a 1/3-octave table uses) and as
              power-per-Hz (density, the convention the analyser reports)

The two conventions differ by 10*log10 of the band width, and a constant-Q grid's width is
proportional to frequency - so the same signal reads 3.01 dB/octave apart under the two
rulers.  That is a *tilt*, and a tilt is exactly what a Match solver is asked to remove
from a voice.  This script fits that tilt and prints it, so the answer is a number.

    python3 analysis/scripts/compare_rulers.py            # prints which convention agrees
"""
from __future__ import annotations

import math
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_match_render as vmr  # noqa: E402  (the take generator, so both see one file)

ROOT = vmr.ROOT
CLI = vmr.CLI


def read_profile(path: str) -> tuple[list[float], list[float]]:
    freqs, vals = [], []
    for line in open(path, encoding="utf-8"):
        parts = line.split()
        if len(parts) == 2:
            try:
                f, d = float(parts[0]), float(parts[1])
            except ValueError:
                continue
            freqs.append(f)
            vals.append(d)
    return freqs, vals


def measure_outside(sig: list[float], sr: int, centres: list[float]
                    ) -> tuple[list[float], list[float]]:
    """Band power (sum of bins) and density (mean of bins) on the same centres.

    Uses the plugin's own 8192-point/hop-2048 layout so that the only difference left is
    the *convention*, not the window length - otherwise the script would be measuring its
    own choices and calling it the plugin's fault.
    """
    n = 8192
    hop = 2048
    window = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    acc = [0.0] * (n // 2 + 1)
    used = 0
    for start in range(0, max(0, len(sig) - n), hop):
        frame = sig[start:start + n]
        rms = math.sqrt(sum(x * x for x in frame) / n)
        if rms < 0.02:
            continue
        spec = vmr.fft([complex(frame[i] * window[i]) for i in range(n)])
        for k in range(n // 2 + 1):
            acc[k] += abs(spec[k]) ** 2
        used += 1
    if used == 0:
        raise SystemExit("no loud frames - the file is silent")
    bin_hz = sr / n
    power, density = [], []
    r = 2 ** (1 / 24.0)
    for fc in centres:
        k0 = max(1, int(math.floor(fc / r / bin_hz)))
        k1 = min(n // 2, int(math.ceil(fc * r / bin_hz)))
        total = sum(acc[k] for k in range(k0, k1 + 1)) / used
        width = max(1, k1 - k0 + 1)
        power.append(10.0 * math.log10(total + 1e-20))
        density.append(10.0 * math.log10(total / width + 1e-20))
    return power, density


def rel1k(freqs: list[float], vals: list[float]) -> list[float]:
    def interp(f: float) -> float:
        if f <= freqs[0]:
            return vals[0]
        if f >= freqs[-1]:
            return vals[-1]
        for i in range(len(freqs) - 1):
            if freqs[i] <= f <= freqs[i + 1]:
                t = (math.log2(f) - math.log2(freqs[i])) / (
                    math.log2(freqs[i + 1]) - math.log2(freqs[i]))
                return vals[i] + t * (vals[i + 1] - vals[i])
        return vals[-1]
    ref = interp(1000.0)
    return [x - ref for x in vals]


def fit_tilt_db_per_oct(freqs: list[float], diff: list[float]) -> float:
    """Least-squares slope of `diff` against log2(f)."""
    xs = [math.log2(f) for f in freqs]
    mx = sum(xs) / len(xs)
    my = sum(diff) / len(diff)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, diff))
    den = sum((x - mx) ** 2 for x in xs)
    return num / den if den else 0.0


def rms(diff: list[float]) -> float:
    return math.sqrt(sum(d * d for d in diff) / len(diff))


def main() -> int:
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2
    tmp = tempfile.mkdtemp()
    take = os.path.join(tmp, "take.wav")
    prof = os.path.join(tmp, "take.aeqprof")
    vmr.make_take(take, -6.0)

    p = subprocess.run([CLI, "profile", take, prof], cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout + p.stderr)
        return 1
    freqs, plugin = read_profile(prof)
    sig, sr = vmr.read_wav(take)
    power, density = measure_outside(sig, sr, freqs)

    # compare over the range a vocal EQ actually acts on
    keep = [i for i, f in enumerate(freqs) if 100.0 <= f <= 13000.0]
    fs = [freqs[i] for i in keep]
    a = rel1k(freqs, plugin)
    b_pow = rel1k(freqs, power)
    b_den = rel1k(freqs, density)
    d_pow = [a[i] - b_pow[i] for i in keep]
    d_den = [a[i] - b_den[i] for i in keep]

    print(f"  {len(freqs)} bands of the plugin's own analyser vs a 4096-point FFT, "
          f"{len(keep)} compared (100 Hz - 13 kHz)")
    print(f"  vs power-per-band : RMS difference {rms(d_pow):5.2f} dB, "
          f"tilt {fit_tilt_db_per_oct(fs, d_pow):+5.2f} dB/octave")
    print(f"  vs power-per-Hz   : RMS difference {rms(d_den):5.2f} dB, "
          f"tilt {fit_tilt_db_per_oct(fs, d_den):+5.2f} dB/octave")
    winner = "density" if rms(d_den) < rms(d_pow) else "band power"
    print(f"  the plugin reports {winner} ({min(rms(d_pow), rms(d_den)):.2f} dB RMS)")

    # A tilt of 3 dB/octave between the two candidates is not noise - it is the band width
    # law.  If the plugin agrees with one convention and the target table was built with
    # the other, the solver will faithfully remove a tilt nobody played.
    # --bandpower forces the *other* convention, so the claim in the ledger ("the plugin
    # reports density") can be falsified by flipping the ruler rather than by trusting it.
    if "--bandpower" in sys.argv:
        print(f"  forced to band power: {rms(d_pow):.2f} dB RMS, "
              f"{fit_tilt_db_per_oct(fs, d_pow):+.2f} dB/octave")
        return 0 if rms(d_pow) <= 2.0 else 1
    if "--strict" in sys.argv and min(rms(d_pow), rms(d_den)) > 2.0:
        print("  FAIL neither convention agrees with the analyser to within 2 dB RMS")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
