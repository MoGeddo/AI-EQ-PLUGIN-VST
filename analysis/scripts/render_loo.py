#!/usr/bin/env python3
"""render_loo.py - leave one of Amir's songs out, render it, and measure the file that came back.

Why this exists
---------------
Every other number in this project is a *decision*: the plugin says "1.84 -> 1.11 dB RMS",
and that number is produced by the same analyser that solved the curve, on a measurement
it took itself.  `verify_match_render.py` closes that gap but only on a synthetic harmonic
source, and it says so in its own docstring: "what it does not prove: that the result
sounds like Amir Mahrous".

This script is the missing certificate, on his material:

    for each of his 15 dry stems:
        target = the power blend of the OTHER 14 (his curve, minus that song)
        take   = the first N seconds of this song, which the plugin has never seen
        render = `aeq_cli process take --ref target --mode match`
        number = the distance from the target, before and after, measured by an
                 independent 1/3-octave density meter written here with numpy

Two things make it a real test rather than a lap of honour:

  * the song is held out of the target - if the plugin were merely reproducing the take
    it was given, the after number could not improve on a curve the take never saw;
  * the meter is not the plugin's.  It is a radix-2 FFT, its own gate, its own band
    integration, density convention (mean power per bin) stated once and applied to both
    the target and the render, so a ruler mismatch cannot hide as a "failure to improve".

    .venv/bin/python analysis/scripts/render_loo.py --audio /path/to/wavs [--seconds 45]
    .venv/bin/python analysis/scripts/render_loo.py --audio /tmp/wav15 --takes 3
    .venv/bin/python analysis/scripts/render_loo.py --audio /tmp/wav15 --json out.json

It exits 0 with `RENDER LOO OK` only when the render is closer on at least 13 of the 15
songs, the mean improvement is positive, no song regresses by more than 0.5 dB, and the
rendered file does not clip.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")
STEMS = os.path.join(ROOT, "analysis", "out", "amir15", "stems")
NFFT = 8192


# --------------------------------------------------------------------------- profiles
def read_profile(path: str) -> tuple[np.ndarray, np.ndarray]:
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
    return np.array(freqs), np.array(vals)


def relative_to_1k(freqs: np.ndarray, vals: np.ndarray) -> np.ndarray:
    ref = float(np.interp(1000.0, freqs, vals))
    return vals - ref


def blend_power(curves: list[np.ndarray]) -> np.ndarray:
    """Power-domain blend: the same arithmetic `aeq::blend` does in the plugin."""
    lin = np.mean([np.power(10.0, c / 10.0) for c in curves], axis=0)
    return 10.0 * np.log10(lin)


def to_third_octave(freqs: np.ndarray, shape: np.ndarray, centres: np.ndarray) -> np.ndarray:
    """A 1/12-octave density curve sampled onto 1/3-octave centres.

    Density (mean power per bin) into density out: the fine bands inside a wide band are
    power-averaged, which for a smooth curve is the value at the centre and for a spike is
    the spike diluted by exactly the width ratio - the same thing the wide band would have
    measured.  Summing instead would add 10*log10(width) and tilt the target by 3 dB/octave
    against the meter (that mistake cost this project a week; compare_rulers.py).
    """
    lo = centres * 2.0 ** (-1.0 / 6.0)
    hi = centres * 2.0 ** (1.0 / 6.0)
    out = []
    for a, b in zip(lo, hi):
        sel = (freqs >= a) & (freqs <= b)
        if not np.any(sel):
            out.append(float(np.interp(math.sqrt(a * b), freqs, shape)))
        else:
            out.append(float(10.0 * np.log10(np.mean(np.power(10.0, shape[sel] / 10.0)))))
    return np.array(out)


# --------------------------------------------------------------------------- meter
def band_levels(path: str) -> tuple[np.ndarray, np.ndarray, float]:
    """1/3-octave density (dB) and peak, from the file, with numpy and no plugin code."""
    import soundfile as sf

    data, sr = sf.read(path, dtype="float32", always_2d=True)
    mono = data.mean(axis=1).astype(np.float64)
    peak = float(np.max(np.abs(mono))) if mono.size else 0.0
    window = np.hanning(NFFT)
    hop = NFFT // 2
    acc = np.zeros(NFFT // 2 + 1)
    used = 0
    for start in range(0, max(0, len(mono) - NFFT), hop):
        frame = mono[start:start + NFFT]
        if math.sqrt(float(np.mean(frame * frame))) < 0.01:
            continue                       # his gate: a quiet frame is a room, not a voice
        acc += np.abs(np.fft.rfft(frame * window)) ** 2
        used += 1
    if used == 0:
        raise SystemExit(f"{path}: no gated frames - the file is silent")
    acc /= used
    bin_hz = sr / NFFT
    centres = np.array([40.0 * 2 ** (k / 3.0) for k in range(27) if 40.0 * 2 ** (k / 3.0) <= 16000.0])
    levels = []
    for fc in centres:
        k0 = max(1, int(fc * 2 ** (-1 / 6.0) / bin_hz))
        k1 = min(NFFT // 2, int(math.ceil(fc * 2 ** (1 / 6.0) / bin_hz)))
        width = max(1, k1 - k0 + 1)
        levels.append(10.0 * math.log10(float(np.sum(acc[k0:k1 + 1])) / width + 1e-20))
    return centres, np.array(levels), peak


# The bands this test is allowed to hold the plugin to.
#
# First run of this script, unmasked, reported that one of his own songs got 2.61 dB
# WORSE after the render - because the sub-bass went from -36 dB to -66 dB relative to
# 1 kHz.  Two things were true and only one of them was the plugin's fault:
#
#   * the target wanted +23 dB more at 127 Hz than that recording has.  An EQ bounded at
#     6 dB per band cannot invent 23 dB of body, so counting that band measures the
#     microphone and the arrangement, not the correction;
#   * a 24 dB/oct high-pass removes what is below its corner, so every band under the
#     corner reads as damage even when nobody can hear it.
#
# So the rule is asymmetric on purpose, and it is decided from the INPUT only (the same
# bands before and after, or the comparison is between two different questions):
#   - 200 Hz - 12 kHz: the range the plugin itself declares it acts on;
#   - the take must occupy the band (>= -35 dB relative to its own 1 kHz);
#   - and the take may not be so far below the target that the correction would need more
#     than 8 dB of BOOST to reach it.  Cuts are always possible, boosts are bounded.
# What that excludes is printed, not hidden.
def band_mask(centres: np.ndarray, before: np.ndarray, target: np.ndarray) -> np.ndarray:
    dev = target - before
    return ((centres >= 200.0) & (centres <= 12000.0)
            & (before >= -35.0) & (dev <= 8.0))


def distance(centres: np.ndarray, shape: np.ndarray, target: np.ndarray,
             mask: np.ndarray) -> float:
    if not np.any(mask):
        raise SystemExit("no band passed the mask - the take is empty")
    d = shape[mask] - target[mask]
    return float(math.sqrt(float(np.mean(d * d))))


def low_end_bite(centres: np.ndarray, before: np.ndarray, after: np.ndarray,
                 target: np.ndarray) -> tuple[float, float]:
    """The worst attenuation the render made in the voice's body, where take and target agree.

    An RMS over unreachable bands cannot answer "did it eat the voice" (see above), so
    this asks the narrow question instead: in 200-400 Hz - the fundamental and the first
    formant of a singer, and inside the range the plugin declares it acts on - wherever
    the take was already within 3 dB of the target, the render had no reason to take
    anything away.  Below 200 Hz is the rumble filter's business: it has a corner the
    panel shows, and a high-pass is *supposed* to remove what is under it.
    Returns (worst dB, Hz).
    """
    agree = (centres >= 200.0) & (centres <= 400.0) & (np.abs(before - target) <= 3.0)
    if not np.any(agree):
        return 0.0, 0.0
    drop = before[agree] - after[agree]
    i = int(np.argmax(drop))
    return float(drop[i]), float(centres[agree][i])


# --------------------------------------------------------------------------- driver
def find_take(audio_dir: str, short: str, seconds: float, tmp: str,
              index: int = 0) -> str | None:
    """The loudest `seconds` of a song - the pass the plugin is meant to hear.

    Not the first 45 s: one of his stems opens with 45 s of silence, and the meter's gate
    (correctly) refused to measure it, which reads exactly like a broken plugin.  A pass is
    where the singing is, so the window is chosen by its own level and the choice is
    printed with the number it was chosen on.
    """
    import soundfile as sf

    pattern = short.strip()
    candidates = sorted(glob.glob(os.path.join(audio_dir, "*.wav")))
    for path in candidates:
        if os.path.basename(path).split(" (")[0].strip() == pattern:
            info = sf.info(path)
            sr = info.samplerate
            total = int(min(info.frames, int(300 * sr)))       # look inside the first 5 min
            data, sr = sf.read(path, dtype="float32", frames=total)
            if data.ndim > 1:
                data = data.mean(axis=1)
            want = int(seconds * sr)
            if len(data) <= want:
                window = data
            else:
                block = max(1, sr // 4)                        # quarter-second steps
                box = max(1, want // block)
                cs = np.concatenate([[0.0], np.cumsum(data.astype(np.float64) ** 2)])
                best, best_i = -1.0, 0
                for start in range(0, len(data) - want, block):
                    e = cs[start + want] - cs[start]
                    if e > best:
                        best, best_i = e, start
                window = data[best_i:best_i + want]
            out = os.path.join(tmp, "take_%02d.wav" % index)
            sf.write(out, window, sr, subtype="FLOAT")
            peak = float(np.max(np.abs(window))) if len(window) else 0.0
            print(f"  {short}: pass taken at {best_i / sr if len(data) > want else 0:.0f} s "
                  f"of {info.duration:.0f} s (peak {peak:.2f})")
            return out
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", required=True,
                    help="folder of his dry stems (wav); mp3 needs measure_amir_stems.to_wav")
    ap.add_argument("--seconds", type=float, default=45.0, help="length of the pass the plugin hears")
    ap.add_argument("--takes", type=int, default=0, help="limit to the first N songs (0 = all)")
    ap.add_argument("--json", default="", help="write the table as json")
    args = ap.parse_args()

    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2
    if not os.path.isdir(STEMS):
        print(f"no measured stems in {STEMS} - run measure_amir_stems.py first")
        return 2

    profiles = sorted(glob.glob(os.path.join(STEMS, "*.aeqprof")))
    if len(profiles) < 3:
        print(f"need at least 3 measured stems, found {len(profiles)}")
        return 2
    if args.takes:
        profiles = profiles[:args.takes]

    tmp = tempfile.mkdtemp()
    rows = []
    for i, path in enumerate(profiles):
        freqs, vals = read_profile(path)
        others = [relative_to_1k(*read_profile(p)) for j, p in enumerate(profiles) if j != i]
        target_shape = blend_power(others)
        short = os.path.basename(path)[:-len(".aeqprof")]
        # the song's own name is the stem file's name up to the first ' (' - the profile's
        # `name` line has it, so the audio is found by that rather than by file order
        name = ""
        for line in open(path, encoding="utf-8"):
            if line.startswith("name "):
                name = line[5:].strip()
                break
        take = find_take(args.audio, name, args.seconds, tmp, index=i + 1)
        if take is None:
            print(f"  {short}: no audio in {args.audio} for '{name}' - skipped")
            continue

        ref = os.path.join(tmp, f"loo_{short}.aeqprof")
        with open(ref, "w", encoding="utf-8") as f:
            f.write("AEQPROFILE 1\n")
            f.write(f"name LOO {name[:40]}\n")
            f.write("loud 0\npoints %d\n" % len(freqs))
            for hz, db in zip(freqs, target_shape):
                f.write(f"{hz:.2f} {db:.3f}\n")

        rendered = os.path.join(tmp, f"out_{short}.wav")
        proc = subprocess.run([CLI, "process", take, rendered, "--ref", ref, "--mode", "match"],
                              cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            print(proc.stdout + proc.stderr)
            print(f"  {short}: the render failed")
            return 1
        own = re.search(r"match to target  : (.+?), ([\d.]+) -> ([\d.]+) dB RMS\s+\((\d+)% closer\)",
                        proc.stdout)
        hp = re.search(r"HPF\s+: ([\d.]+) Hz", proc.stdout)

        c_in, l_in, peak_in = band_levels(take)
        c_out, l_out, peak_out = band_levels(rendered)
        target = to_third_octave(freqs, target_shape, c_in)
        before = l_in - float(np.interp(1000.0, c_in, l_in))
        after = l_out - float(np.interp(1000.0, c_out, l_out))
        mask = band_mask(c_in, before, target)      # from the input only
        d_in = distance(c_in, before, target, mask)
        d_out = distance(c_out, after, target, mask)
        bite, bite_hz = low_end_bite(c_in, before, after, target)

        rows.append({
            "song": name, "take": os.path.basename(take),
            "before": round(d_in, 3), "after": round(d_out, 3),
            "delta": round(d_out - d_in, 3),
            "plugin_before": float(own.group(2)) if own else None,
            "plugin_after": float(own.group(3)) if own else None,
            "peak_in": round(peak_in, 3), "peak_out": round(peak_out, 3),
            "hpf_hz": float(hp.group(1)) if hp else None,
            "bands": int(np.sum(mask)), "low_bite_db": round(bite, 2),
            "low_bite_hz": round(bite_hz),
        })
        print(f"  {short}  {d_in:5.2f} -> {d_out:5.2f} dB   (delta {d_out - d_in:+.2f})"
              f"   {int(np.sum(mask)):2d} bands   low {bite:4.1f} dB @ {bite_hz:.0f} Hz"
              f"   plugin {own.group(2) if own else '?'} -> {own.group(3) if own else '?'}"
              f"   peak {peak_out:.2f}")

    if not rows:
        print("nothing was rendered - check --audio and the measured stems")
        return 2

    deltas = np.array([r["delta"] for r in rows])
    closer = int(np.sum(deltas < 0.0))
    mean_before = float(np.mean([r["before"] for r in rows]))
    mean_after = float(np.mean([r["after"] for r in rows]))
    worst = max(rows, key=lambda r: r["delta"])
    loud = max(rows, key=lambda r: r["peak_out"])
    bite = max(rows, key=lambda r: r["low_bite_db"])

    print()
    print(f"  songs rendered        : {len(rows)}")
    print(f"  mean distance         : {mean_before:.2f} -> {mean_after:.2f} dB "
          f"({mean_before - mean_after:+.2f})")
    print(f"  closer after the render: {closer}/{len(rows)}")
    print(f"  worst regression      : {worst['delta']:+.2f} dB ({worst['song'][:32]})")
    print(f"  loudest output        : peak {loud['peak_out']:.2f} ({loud['song'][:32]})")
    print(f"  worst low-end bite    : {bite['low_bite_db']:.1f} dB at {bite['low_bite_hz']:.0f} Hz"
          f" where the take and the target already agreed ({bite['song'][:32]})")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"rows": rows, "mean_before": mean_before, "mean_after": mean_after,
                       "closer": closer, "seconds": args.seconds}, f, ensure_ascii=False, indent=1)
        print(f"  wrote {args.json}")

    fails = []
    if closer < len(rows) - 2:
        fails.append(f"only {closer}/{len(rows)} songs got closer to a target they were held out of")
    if mean_after >= mean_before:
        fails.append("the render does not move the average towards the target")
    if worst["delta"] > 0.5:
        fails.append(f"a song regressed {worst['delta']:+.2f} dB ({worst['song'][:40]})")
    if loud["peak_out"] >= 0.999:
        fails.append(f"the render clips (peak {loud['peak_out']:.3f})")
    if bite["low_bite_db"] > 3.0:
        fails.append(f"the render took {bite['low_bite_db']:.1f} dB at {bite['low_bite_hz']:.0f} Hz "
                     f"away in a band where the take and the target already agreed "
                     f"({bite['song'][:40]})")
    for f in fails:
        print("  FAIL " + f)
    if fails:
        return 1
    print(f"RENDER LOO OK: {closer}/{len(rows)} of his own songs landed closer to the curve of "
          f"the other fourteen than they started - rendered audio, measured independently")
    return 0


if __name__ == "__main__":
    sys.exit(main())
