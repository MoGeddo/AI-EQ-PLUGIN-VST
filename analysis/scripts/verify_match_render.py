#!/usr/bin/env python3
"""verify_match_render.py - does the *rendered* audio measure like Amir's curve?

Every number the plugin prints comes from the same analyser that solved the curve.  That
makes it a good control signal and a bad certificate: an engine that measured itself would
report success even if it were wrong in the same way twice.  This script closes that gap in
the only way it can be closed offline - it renders a take through the plugin, then measures
the *output file* with a different instrument (a plain radix-2 FFT, 1/3-octave sums, its own
gate, written here) and compares that to the curve the plugin was aiming at, parsed out of
`plugin/Source/model/AmirTarget.h`.

    python3 analysis/scripts/verify_match_render.py          # prints RENDER MATCH OK

What this proves: the correction survives the audio path - the file that comes out of the
plugin really is closer to the target than the file that went in, measured by something
other than the plugin.
What it does not prove: that the result sounds like Amir Mahrous.  A synthetic harmonic
source is a test signal, not a singer; the claim about *his* voice needs his stems, and
`aeq_cli profile --from-dir` measuring them with this plugin's own analyser (G75).
"""
from __future__ import annotations

import cmath
import math
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")
HEADER = os.path.join(ROOT, "plugin", "Source", "model", "AmirTarget.h")
SR = 48000
NFFT = 4096


# ---- the take ------------------------------------------------------------------
def make_take(path: str, tilt_db_per_oct: float, sr: int = SR) -> None:
    """A harmonic voice-like signal: 1/f source, four formants, syllables, vibrato.

    `tilt_db_per_oct` is applied above 1 kHz, so -6 is a dull take and 0 is a bright one.
    Deterministic: the same file every run, so the number in the ledger is comparable.
    """
    f0 = 150.0
    formants = [(650.0, 1.0, 110.0), (1080.0, 0.62, 140.0), (2650.0, 0.34, 200.0),
                (3400.0, 0.20, 260.0)]
    out = []
    for syl in range(11):
        n = int(0.32 * sr)
        for i in range(n):
            t = (syl * 0.32) + i / sr
            env = (i / n) ** 0.5 * (1.0 - i / n) ** 1.6
            f = f0 * (1.0 + 0.012 * math.sin(2 * math.pi * 5.2 * t))
            v = 0.0
            for h in range(1, 34):
                fh = f * h
                if fh > 15000.0:
                    break
                amp = 1.0 / h
                for fc, g, bw in formants:
                    amp += g * math.exp(-0.5 * ((fh - fc) / bw) ** 2) / math.sqrt(h)
                if fh > 1000.0:
                    amp *= 10.0 ** (tilt_db_per_oct * math.log2(fh / 1000.0) / 20.0)
                v += amp * math.sin(2 * math.pi * fh * t * (1.0 if h == 1 else 1.0))
            out.append(0.10 * env * v)
        out.extend([0.0] * int(0.16 * sr))
    peak = max(abs(v) for v in out) or 1.0
    data = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v / peak * 0.7)) * 32767))
                    for v in out)
    header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt "
              + struct.pack("<IHHIIHH", 16, 1, 1, sr, sr * 2, 2, 16)
              + b"data" + struct.pack("<I", len(data)))
    with open(path, "wb") as f:
        f.write(header + data)


def read_wav(path: str) -> tuple[list[float], int]:
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise SystemExit(f"{path} is not a wav")
    pos, sr, ch, bits, data = 12, SR, 1, 16, b""
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = struct.unpack("<I", blob[pos + 4:pos + 8])[0]
        body = blob[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            _, ch, sr = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    # 16-bit integers for the take, 32-bit float for anything the plugin rendered: both
    # are what this toolchain produces, and refusing one of them is how the first run of
    # this script failed ("only 16-bit wav is measured here, got 32").
    if bits == 16:
        width, scale, fmt = 2, 1.0 / 32768.0, "<h"
    elif bits == 24:
        width, scale, fmt = 3, 1.0 / 8388608.0, None
    elif bits == 32:
        width, scale, fmt = 4, 1.0, "<f"
    else:
        raise SystemExit(f"unsupported sample format: {bits} bit")
    frames = len(data) // (width * ch)
    mono = []
    for i in range(frames):
        acc = 0.0
        for c in range(ch):
            off = (i * ch + c) * width
            if fmt is None:
                raw = int.from_bytes(data[off:off + 3], "little", signed=True)
                acc += raw * scale
            else:
                acc += struct.unpack_from(fmt, data, off)[0] * scale
        mono.append(acc / ch)
    return mono, sr


# ---- a second opinion on the spectrum ------------------------------------------
def fft(x: list[complex]) -> list[complex]:
    """Iterative radix-2 FFT.  Deliberately not the plugin's analyser."""
    n = len(x)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        half = size // 2
        step = cmath.exp(-2j * math.pi / size)
        for start in range(0, n, size):
            w = 1 + 0j
            for k in range(start, start + half):
                u = x[k]
                v = x[k + half] * w
                x[k] = u + v
                x[k + half] = u - v
                w *= step
        size *= 2
    return x


def band_levels(sig: list[float], sr: int) -> tuple[list[float], list[float]]:
    """1/3-octave power per band (40 Hz - 16 kHz), gated on frame level."""
    window = [0.5 - 0.5 * math.cos(2 * math.pi * i / NFFT) for i in range(NFFT)]
    acc = [0.0] * (NFFT // 2 + 1)
    used = 0
    hop = NFFT // 2
    for start in range(0, len(sig) - NFFT, hop):
        frame = sig[start:start + NFFT]
        rms = math.sqrt(sum(v * v for v in frame) / NFFT)
        if rms < 0.01:
            continue
        spec = fft([complex(frame[i] * window[i]) for i in range(NFFT)])
        for k in range(NFFT // 2 + 1):
            acc[k] += abs(spec[k]) ** 2
        used += 1
    if used == 0:
        raise SystemExit("no gated frames in the output - the render is silent")
    bin_hz = sr / NFFT
    centres = [40 * 2 ** (k / 3.0) for k in range(27) if 40 * 2 ** (k / 3.0) <= 16000]
    levels = []
    for fc in centres:
        lo = fc * 2 ** (-1 / 6.0)
        hi = fc * 2 ** (1 / 6.0)
        k0 = max(1, int(lo / bin_hz))
        k1 = min(NFFT // 2, int(math.ceil(hi / bin_hz)))
        power = sum(acc[k] for k in range(k0, k1 + 1)) / max(1, used)
        levels.append(10.0 * math.log10(power / used + 1e-20) if False else
                      10.0 * math.log10(power + 1e-20))
    return centres, levels


def shape_relative_to_1k(centres: list[float], levels: list[float]) -> list[float]:
    def interp(f: float) -> float:
        if f <= centres[0]:
            return levels[0]
        if f >= centres[-1]:
            return levels[-1]
        for i in range(len(centres) - 1):
            if centres[i] <= f <= centres[i + 1]:
                t = (math.log2(f) - math.log2(centres[i])) / (
                    math.log2(centres[i + 1]) - math.log2(centres[i]))
                return levels[i] + t * (levels[i + 1] - levels[i])
        return levels[-1]
    ref = interp(1000.0)
    return [v - ref for v in levels]


def amir_shape(freqs: list[float]) -> list[float]:
    """The curve the plugin aims at, read out of the generated header."""
    text = open(HEADER, encoding="utf-8").read()
    block = re.search(r"hz\(\) \{.*?\{\s*(.*?)\s*\};", text, re.S).group(1)
    hz = [float(v) for v in re.findall(r"([\d.]+)f", block)]
    block = re.search(r"db\(\) \{.*?\{\s*(.*?)\s*\};", text, re.S).group(1)
    db = [float(v) for v in re.findall(r"(-?[\d.]+)f", block)]
    if len(hz) != len(db) or len(hz) < 8:
        raise SystemExit("AmirTarget.h did not parse into two equal tables")

    def interp(f: float) -> float:
        if f <= hz[0]:
            return db[0]
        if f >= hz[-1]:
            return db[-1]
        for i in range(len(hz) - 1):
            if hz[i] <= f <= hz[i + 1]:
                t = (math.log2(f) - math.log2(hz[i])) / (math.log2(hz[i + 1]) - math.log2(hz[i]))
                return db[i] + t * (db[i + 1] - db[i])
        return db[-1]

    ref = interp(1000.0)
    return [interp(f) - ref for f in freqs]


def distance_db(centres: list[float], levels: list[float], target: list[float],
                occupied: list[bool]) -> float:
    """RMS difference to the target over the bands the take actually occupies.

    200 Hz - 13 kHz is the range the chain can act on.  Inside it, a band where the take
    itself has nothing (here: above 10 kHz the synthetic source is 40 dB down) cannot be
    reached by a bounded EQ, and counting it measures the fixture, not the plugin - the
    first version of this script did exactly that and reported 11.43 -> 7.83 dB, a number
    dominated by a 12.9 kHz band the source never had.  `occupied` is computed from the
    *input* only, once, and the same mask is applied to both distances, so the comparison
    is between two measurements of the same bands.
    """
    diffs = [l - t for c, l, t, ok in zip(centres, levels, target, occupied)
             if ok and 200.0 <= c <= 13000.0]
    return math.sqrt(sum(d * d for d in diffs) / len(diffs))


def main() -> int:
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2
    if not os.path.exists(HEADER):
        print(f"no {HEADER}")
        return 2
    tmp = tempfile.mkdtemp()
    take = os.path.join(tmp, "dull.wav")
    rendered = os.path.join(tmp, "dull_matched.wav")
    make_take(take, -6.0)

    p = subprocess.run([CLI, "process", take, rendered, "--mode", "match"],
                       cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout + p.stderr)
        print("the render failed")
        return 1
    plugin = re.search(r"match to target  : (.+?), ([\d.]+) -> ([\d.]+) dB RMS\s+\((\d+)% closer\)",
                       p.stdout)
    if not plugin:
        print(p.stdout)
        print("the plugin did not report a match number - is it aiming at anything?")
        return 1
    name, own_before, own_after, own_closer = (plugin.group(1), float(plugin.group(2)),
                                               float(plugin.group(3)), int(plugin.group(4)))

    before_sig, sr1 = read_wav(take)
    after_sig, sr2 = read_wav(rendered)
    if sr1 != sr2:
        print(f"sample rate changed: {sr1} -> {sr2}")
        return 1
    c_in, l_in = band_levels(before_sig, sr1)
    c_out, l_out = band_levels(after_sig, sr2)
    target = amir_shape(c_in)
    in_shape = shape_relative_to_1k(c_in, l_in)
    out_shape = shape_relative_to_1k(c_out, l_out)
    # the take's own occupancy, from the input alone: a band the singer never filled is
    # not the plugin's to reach
    occupied = [v >= -40.0 for v in in_shape]
    d_in = distance_db(c_in, in_shape, target, occupied)
    d_out = distance_db(c_out, out_shape, target, occupied)

    print(f"  target            : {name} (from AmirTarget.h)")
    print(f"  plugin's own meter: {own_before:.2f} -> {own_after:.2f} dB ({own_closer}% closer)")
    print(f"  rendered file     : {d_in:.2f} -> {d_out:.2f} dB from the target, "
          f"measured by a different analyser")
    fails = []
    if d_out >= d_in * 0.80:
        fails.append(f"the rendered file is not clearly closer ({d_in:.2f} -> {d_out:.2f} dB)")
    if d_in < 2.0:
        fails.append(f"the test take is already within {d_in:.2f} dB of the target - "
                     "there is nothing to prove (the fixture, not the plugin)")
    for f in fails:
        print("  FAIL " + f)
        return 1
    print(f"RENDER MATCH OK: rendered audio {100.0 * (1.0 - d_out / d_in):.0f}% closer "
          f"to Amir's curve, measured independently")
    return 0


if __name__ == "__main__":
    sys.exit(main())
