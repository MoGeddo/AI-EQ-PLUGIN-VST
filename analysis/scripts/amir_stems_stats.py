#!/usr/bin/env python3
"""amir_stems_stats.py - measure each of Amir's 15 dry stems with the plugin's own analyser.

`AmirTarget.h` was generated from the analysis pipeline (1/3-octave tables).  Once the stems
are on the machine, the honest thing is to measure them with the *same instrument* that will
later run inside the plugin, so the target and the take are compared on one ruler (G72's
rule, applied to the target itself).

This script produces, from a folder of stems:

    analysis/out/amir15/stems/<song>.aeqprof     one profile per song, plugin-measured
    analysis/out/amir15/amir_stems.aeqprof       their power-domain blend (this is the target)
    analysis/out/amir15/amir_measured.json       mean, per-band spread, leave-one-out shift

The spread is what the plugin uses as its natural dead zone (`AmirTarget::sd`): the amount
by which *his own* mixes differ from each other in each band.  Anything inside it is his
range, not a fault.

    .venv/bin/pip install miniaudio
    python3 analysis/scripts/amir_stems_stats.py google_drive
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")
OUT = os.path.join(ROOT, "analysis", "out", "amir15")
STEMS = os.path.join(OUT, "stems")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from measure_amir_stems import to_wav  # noqa: E402  (one decoder, one place)


def read_profile(path: str) -> tuple[list[float], list[float], str]:
    freqs, vals, name = [], [], ""
    for line in open(path, encoding="utf-8"):
        if line.startswith("name "):
            name = line[5:].strip()
        parts = line.split()
        if len(parts) == 2:
            try:
                f, d = float(parts[0]), float(parts[1])
            except ValueError:
                continue
            freqs.append(f)
            vals.append(d)
    return freqs, vals, name


def normalise(curve: list[float], freqs: list[float]) -> list[float]:
    """Each song relative to its own 1 kHz - the same normalisation `Profile::normalised()`
    applies before blending.  Without it the spread between his mixes is dominated by how
    loud each mix is, not by how it is shaped, and the dead zone would be invented."""
    ref = None
    for i in range(len(freqs) - 1):
        if freqs[i] <= 1000.0 <= freqs[i + 1]:
            t = (math.log(1000.0) - math.log(freqs[i])) / (math.log(freqs[i + 1]) - math.log(freqs[i]))
            ref = curve[i] + t * (curve[i + 1] - curve[i])
            break
    ref = curve[0] if ref is None else ref
    return [c - ref for c in curve]


def power_blend(curves: list[list[float]], weights: list[float] | None = None) -> list[float]:
    """The same blend the plugin's Profile::blend performs: weighted mean of linear power,
    so two references 6 dB apart combine to 3.96 dB, not 3."""
    w = weights or [1.0] * len(curves)
    n = len(curves[0])
    out = []
    for i in range(n):
        acc, wsum = 0.0, 0.0
        for c, k in zip(curves, w):
            acc += k * 10.0 ** (c[i] / 10.0)
            wsum += k
        out.append(10.0 * math.log10(acc / wsum))
    return out


def write_profile(path: str, freqs: list[float], vals: list[float], name: str) -> None:
    with open(path, "w", encoding="utf-8") as f:
        f.write("AEQPROFILE 1\n")
        f.write(f"name {name}\n")
        f.write("loud 0\nframes 0\n")
        f.write(f"points {len(freqs)}\n")
        for fr, d in zip(freqs, vals):
            f.write(f"{fr:.17g} {d:.17g}\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--name", default="AMIR 15")
    ap.add_argument("--out", default=OUT,
                    help="where the per-song profiles and the statistics go (default: "
                         "analysis/out/amir15); the gate points this at a temp folder so "
                         "it can test the arithmetic on known curves")
    ap.add_argument("--reuse", action="store_true",
                    help="recompute the statistics from the profiles already on disk, "
                         "without decoding and measuring the stems again")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out)
    stems_dir = os.path.join(out_dir, "stems")
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2
    os.makedirs(stems_dir, exist_ok=True)

    files = [] if args.reuse else sorted(
        f for f in os.listdir(args.folder)
        if os.path.splitext(f)[1].lower() in (".mp3", ".wav", ".flac", ".aiff"))
    if not files and not args.reuse:
        print(f"no audio in {args.folder}")
        return 2

    work = tempfile.mkdtemp()
    per_song: dict[str, tuple[list[float], list[float]]] = {}
    if args.reuse:
        for name in sorted(os.listdir(stems_dir)):
            if not name.endswith(".aeqprof"):
                continue
            grid, vals, song = read_profile(os.path.join(stems_dir, name))
            per_song[song or name] = (grid, vals)
        files = []          # nothing left to decode

    for name in files:
        wav = to_wav(os.path.join(args.folder, name), work)
        if wav is None:
            print(f"  skip {name[:40]} (could not decode)")
            continue
        song = os.path.splitext(name)[0]
        # the song name, trimmed to the part before the stem tags, is what a person reads
        short = song.split(" (")[0]
        prof = os.path.join(stems_dir, f"{len(per_song) + 1:02d}.aeqprof")
        p = subprocess.run([CLI, "profile", wav, prof, "--name", short],
                           cwd=ROOT, capture_output=True, text=True)
        if p.returncode != 0:
            print(f"  skip {short} ({p.stdout.strip().splitlines()[-1] if p.stdout else ''})")
            continue
        freqs, vals, _ = read_profile(prof)
        per_song[short] = (freqs, vals)
        print(f"  {len(per_song):2d}. {short:<34} {len(vals)} bands")

    if len(per_song) < 2:
        print("need at least two measurable songs")
        return 3

    freqs = next(iter(per_song.values()))[0]
    for grid, vals in per_song.values():
        if len(vals) != len(freqs) or any(abs(a - b) > 1e-6 for a, b in zip(grid, freqs)):
            print("the stems were not measured on one grid - refusing to blend")
            return 4

    names = list(per_song)
    # every song relative to its own 1 kHz: shape, not loudness (Profile::normalised())
    curves = [normalise(per_song[n][1], freqs) for n in names]
    mean = power_blend(curves)

    # per-band spread of his own mixes, and the leave-one-out shift of the consensus: the
    # same two numbers the analysis pipeline reports (LOO 1.05 dB), now measured by the
    # plugin's instrument on the same material.
    sd = []
    for i in range(len(freqs)):
        col = [c[i] for c in curves]
        mu = sum(col) / len(col)
        sd.append(math.sqrt(sum((x - mu) ** 2 for x in col) / len(col)))
    loo = []
    for k in range(len(curves)):
        rest = power_blend([c for j, c in enumerate(curves) if j != k])
        d = [rest[i] - mean[i] for i in range(len(freqs))]
        loo.append(math.sqrt(sum(x * x for x in d) / len(d)))

    keep = [i for i, f in enumerate(freqs) if 200.0 <= f <= 13000.0]
    print(f"\n  {len(names)} stems, {len(freqs)} bands")
    print(f"  spread between his own mixes (200 Hz-13 kHz): "
          f"median {sorted(sd[i] for i in keep)[len(keep) // 2]:.2f} dB, "
          f"max {max(sd[i] for i in keep):.2f} dB at {freqs[max(keep, key=lambda i: sd[i])]:.0f} Hz")
    print(f"  leave-one-out shift of the consensus: mean {sum(loo) / len(loo):.2f} dB, "
          f"worst {max(loo):.2f} dB")

    out_prof = os.path.join(out_dir, "amir_stems.aeqprof")
    write_profile(out_prof, freqs, mean, args.name)
    with open(os.path.join(out_dir, "amir_measured.json"), "w", encoding="utf-8") as f:
        json.dump({
            "n_songs": len(names),
            "songs": names,
            "freqs": freqs,
            "mean_db": mean,
            "sd_db": sd,
            "leave_one_out_rms_db": loo,
            "loo_mean_db": sum(loo) / len(loo),
            "instrument": "aeq_cli profile (the plugin's own analyser)",
        }, f, ensure_ascii=False, indent=1)
    print(f"  wrote {os.path.relpath(out_prof, ROOT)} and amir_measured.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
