#!/usr/bin/env python3
"""measure_amir_stems.py - turn Amir's 15 dry stems into a target the plugin measured itself.

`plugin/Source/model/AmirTarget.h` was built by the *analysis pipeline* (1/3-octave tables,
its own gating, its own window).  The plugin's analyser is a different instrument - 1/12
octave, gated frames, 8192-point FFT, mean power per bin.  G77 proved the two rulers agree
to 0.96 dB RMS on synthetic audio, but the honest target is the one measured by the
instrument that will later be compared against the take.

This script does that, in one command, on a folder of stems:

    python3 analysis/scripts/measure_amir_stems.py <folder> [--name "AMIR 15"] [--out ...]

    .wav   read directly
    .mp3   decoded with `miniaudio` (bundled decoder, no ffmpeg needed):
           .venv/bin/pip install miniaudio
    .aiff / .aif / .flac   decoded the same way

It writes `analysis/out/amir15/amir_stems.aeqprof` - a text profile the plugin loads with
its REFERENCE pill, or the CLI uses with `--ref` - and then prints the difference between
that curve and the generated header, over the bands both describe.  A small difference
means the analysis table and the plugin agree on his material; a large one means the header
should be regenerated from this measurement (the script says how).

Audio is never committed: `.gitignore` drops wav/mp3, and only the text profile is written
into the repository.
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")
HEADER = os.path.join(ROOT, "plugin", "Source", "model", "AmirTarget.h")
DEFAULT_OUT = os.path.join(ROOT, "analysis", "out", "amir15", "amir_stems.aeqprof")


# ---- decode anything into a 16-bit wav the CLI can read --------------------------------
def decode_with_miniaudio(path: str, out_wav: str) -> bool:
    try:
        import miniaudio  # type: ignore
    except ImportError:
        return False
    dec = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.SIGNED16,
                               nchannels=1, sample_rate=48000)
    pcm = dec.samples.tobytes() if hasattr(dec.samples, "tobytes") else bytes(dec.samples)
    with open(out_wav, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt "
                + struct.pack("<IHHIIHH", 16, 1, 1, 48000, 48000 * 2, 2, 16)
                + b"data" + struct.pack("<I", len(pcm)) + pcm)
    return True


def to_wav(path: str, workdir: str) -> str | None:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".wav":
        return path
    out = os.path.join(workdir, os.path.splitext(os.path.basename(path))[0] + ".wav")
    if decode_with_miniaudio(path, out):
        return out
    return None


# ---- the header, as a curve ------------------------------------------------------------
def header_curve() -> tuple[list[float], list[float]]:
    import re
    text = open(HEADER, encoding="utf-8").read()
    hz = [float(v) for v in re.findall(r"([\d.]+)f",
          re.search(r"hz\(\) \{.*?\{\s*(.*?)\s*\};", text, re.S).group(1))]
    db = [float(v) for v in re.findall(r"(-?[\d.]+)f",
          re.search(r"db\(\) \{.*?\{\s*(.*?)\s*\};", text, re.S).group(1))]
    if len(hz) != len(db) or len(hz) < 8:
        raise SystemExit("AmirTarget.h did not parse into two equal tables")
    return hz, db


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


def interp(freqs: list[float], vals: list[float], f: float) -> float:
    if f <= freqs[0]:
        return vals[0]
    if f >= freqs[-1]:
        return vals[-1]
    for i in range(len(freqs) - 1):
        if freqs[i] <= f <= freqs[i + 1]:
            t = (math.log2(f) - math.log2(freqs[i])) / (math.log2(freqs[i + 1]) - math.log2(freqs[i]))
            return vals[i] + t * (vals[i + 1] - vals[i])
    return vals[-1]


def shape1k(freqs: list[float], vals: list[float]) -> list[float]:
    ref = interp(freqs, vals, 1000.0)
    return [v - ref for v in vals]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--name", default="AMIR 15")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--keep", action="store_true", help="keep the decoded wavs for debugging")
    args = ap.parse_args()

    if not os.path.isdir(args.folder):
        print(f"not a folder: {args.folder}")
        return 2
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2

    work = args.folder if args.keep else tempfile.mkdtemp()
    stems, skipped = [], []
    for name in sorted(os.listdir(args.folder)):
        path = os.path.join(args.folder, name)
        if not os.path.isfile(path):
            continue
        if os.path.splitext(name)[1].lower() not in (".wav", ".mp3", ".flac", ".aiff", ".aif"):
            continue
        wav = to_wav(path, work)
        if wav is None:
            skipped.append(name)
            continue
        stems.append(wav)

    if skipped:
        print(f"  {len(skipped)} file(s) could not be decoded - install the decoder with "
              f"'.venv/bin/pip install miniaudio':")
        for s in skipped[:5]:
            print(f"    {s}")
    if not stems:
        print("nothing to measure")
        return 3

    # one folder of measured wavs -> one profile, through the plugin's own analyser
    stage = tempfile.mkdtemp()
    for s in stems:
        dst = os.path.join(stage, os.path.basename(s))
        if os.path.abspath(s) != os.path.abspath(dst):
            with open(s, "rb") as f, open(dst, "wb") as g:
                g.write(f.read())
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    p = subprocess.run([CLI, "profile", "--from-dir", stage, args.out, "--name", args.name],
                       cwd=ROOT, capture_output=True, text=True)
    print(p.stdout.rstrip())
    if p.returncode != 0:
        print(p.stderr.rstrip())
        return p.returncode

    # ---- and the question this script exists for: does the table agree with the instrument?
    hz, db = header_curve()
    pf, pv = read_profile(args.out)
    table = shape1k(hz, db)
    measured = shape1k(pf, pv)
    diffs, tilts, xs = [], [], []
    for f in pf:
        if not (200.0 <= f <= 13000.0):
            continue
        d = interp(pf, measured, f) - interp(hz, table, f)
        diffs.append(d)
        xs.append(math.log2(f))
    rms = math.sqrt(sum(d * d for d in diffs) / len(diffs))
    mx = sum(xs) / len(xs)
    my = sum(diffs) / len(diffs)
    tilt = sum((x - mx) * (y - my) for x, y in zip(xs, diffs)) / sum((x - mx) ** 2 for x in xs)
    print(f"  header (analysis pipeline) vs this measurement (plugin's analyser):")
    print(f"    {len(diffs)} shared bands, 200 Hz - 13 kHz: {rms:.2f} dB RMS, tilt {tilt:+.2f} dB/octave")
    worst = max(range(len(diffs)), key=lambda i: abs(diffs[i]))
    print(f"    worst band {2 ** xs[worst]:.0f} Hz at {diffs[worst]:+.2f} dB")
    if rms > 3.0:
        print("    the two instruments disagree by more than 3 dB - regenerate the header from")
        print("    this profile before trusting Match, or the plugin aims at one curve and")
        print("    measures another.")
    print(f"  wrote {os.path.relpath(args.out, ROOT)} - load it with the REFERENCE pill")
    return 0


if __name__ == "__main__":
    sys.exit(main())
