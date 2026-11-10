#!/usr/bin/env python3
"""loo_match.py - leave one of Amir's songs out, match it to the other fourteen.

This is the experiment that answers the user's requirement with his own material rather
than with a synthetic test signal: if the plugin is really aiming at *his* curve, then a
song it has never seen - recorded by him, mixed by him - should land inside the spread his
own songs already have from each other.

For each of the 15 stems:

    target   = power-domain blend of the other 14 (his consensus, minus this song)
    take     = this song, measured by the plugin's analyser
    result   = `aeq_cli analyze take --ref target --mode match`

and the two numbers the plugin publishes - how far the take was from the target, and how
far the curve took it - are collected.  Nothing is applied to audio here; this is the
decision, not the render (verify_match_render.py does the render).

    python3 analysis/scripts/loo_match.py [--width 0.1667] [--jobs 2]
"""
from __future__ import annotations

import argparse
import glob
import os
import statistics
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")
STEMS = os.path.join(ROOT, "analysis", "out", "amir15", "stems")
AUDIO = os.path.join(ROOT, "analysis", "out", "amir15", "wav")     # *.wav is gitignored
SOURCE = os.path.join(ROOT, "google_drive")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amir_stems_stats import normalise, power_blend, read_profile, write_profile  # noqa: E402
from measure_amir_stems import to_wav  # noqa: E402


def find_audio(short: str) -> str | None:
    """The stem file for a song, by its short name (the part before ' (')."""
    os.makedirs(AUDIO, exist_ok=True)
    cached = os.path.join(AUDIO, short + ".wav")
    if os.path.exists(cached):
        return cached
    for path in glob.glob(os.path.join(SOURCE, "*.mp3")) + glob.glob(os.path.join(SOURCE, "*.wav")):
        if os.path.basename(path).split(" (")[0].strip() == short.strip():
            return to_wav(path, AUDIO)
    return None


def parse_match(stdout: str) -> tuple[float, float, int] | None:
    """Read the two numbers the CLI publishes, or None.

    Kept as a function because this is where the experiment lied once: the label is
    padded - `match to target  : ...` - so `startswith` looked for a line that began
    differently from the one being printed, and every song silently dropped out of the
    table while the run still exited 0.  Parsing is now the gate's subject (G82).
    """
    line = next((l for l in stdout.splitlines() if "match to target" in l), "")
    hdr = next((l for l in stdout.splitlines() if "corrective EQ" in l), "")
    if not line:
        return None
    bands = int(hdr.split("(")[1].split()[0]) if "(" in hdr else 0
    try:
        nums = line.split(":", 1)[1].split("dB RMS")[0].rsplit(",", 1)[-1]
        before, after = [float(x) for x in nums.replace("->", " ").split()[:2]]
    except Exception:
        return None
    return before, after, bands


def verdict(rows: list[tuple[str, float, float, int]]) -> tuple[list[str], dict]:
    """The three-rule judgement, separated from the run so a bad table can be tried on it."""
    be = [r[1] for r in rows]
    af = [r[2] for r in rows]
    closer = [100.0 * (1.0 - r[2] / r[1]) if r[1] > 0 else 0.0 for r in rows]
    stats = {
        "n": len(rows),
        "mean_before": statistics.mean(be), "median_before": statistics.median(be),
        "worst_before": max(be),
        "mean_after": statistics.mean(af), "median_after": statistics.median(af),
        "worst_after": max(af),
        "closer": sum(1 for c in closer if c > 1.0), "mean_closer": statistics.mean(closer),
    }
    fails = []
    if any(r[2] > r[1] + 1e-6 for r in rows):
        fails.append("at least one song came out farther from his curve than it went in")
    if statistics.mean(af) > 0.70 * statistics.mean(be):
        fails.append("the curve closes less than a third of the distance on average")
    # Two of his songs stay further out than the rest - a female voice sits in a different
    # register, and his curve is one curve.  That is a finding to report, not a pass to
    # force: the gate fails only if the *worst* case stops being an outlier and becomes the
    # rule.
    if statistics.median(af) > 2.0:
        fails.append(f"the median song lands {statistics.median(af):.2f} dB out - the "
                     "correction is not reaching his curve")
    return fails, stats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=float, default=1.0 / 6.0)
    ap.add_argument("--audio", default=None,
                    help="folder holding the stems (default: google_drive/); naming it in "
                         "the gate's CHECK is what makes this gate UNAVAILABLE - rather "
                         "than wrong - on a checkout without his audio")
    args = ap.parse_args()
    global SOURCE
    if args.audio:
        SOURCE = os.path.abspath(args.audio)
    if not args.audio or not os.path.isdir(SOURCE):
        print(f"no stems in {SOURCE} - run the download step first")
        return 2
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2

    files = sorted(glob.glob(os.path.join(STEMS, "*.aeqprof")))
    if len(files) < 3:
        print("no per-song profiles - run analysis/scripts/amir_stems_stats.py first")
        return 2
    data = [read_profile(f) for f in files]
    freqs = data[0][0]
    names = [d[2] for d in data]
    curves = [normalise(d[1], freqs) for d in data]

    rows = []
    for i, name in enumerate(names):
        audio = find_audio(name)
        if audio is None:
            print(f"  {name}: no audio for this profile - skipped")
            continue
        rest = power_blend([c for j, c in enumerate(curves) if j != i])
        target = os.path.join(tempfile.mkdtemp(), "loo.aeqprof")
        write_profile(target, freqs, rest, "AMIR LOO")
        p = subprocess.run([CLI, "analyze", audio, "--ref", target, "--mode", "match",
                            "--width", f"{args.width:.6f}"], cwd=ROOT,
                           capture_output=True, text=True)
        got = parse_match(p.stdout)
        if got is None:
            print(f"  {name}: could not read a match line from the CLI output")
            continue
        before, after, bands = got
        rows.append((name, before, after, bands))
        print(f"  {name:<34} {before:5.2f} -> {after:5.2f} dB  ({bands} bands)")

    if not rows:
        print("nothing measured")
        return 3
    fails, st = verdict(rows)
    print()
    print(f"  {st['n']} songs, comparison width {args.width:.4f} octave")
    print(f"  distance to his own consensus   before {st['mean_before']:.2f} dB (median "
          f"{st['median_before']:.2f}, worst {st['worst_before']:.2f})")
    print(f"                                  after  {st['mean_after']:.2f} dB (median "
          f"{st['median_after']:.2f}, worst {st['worst_after']:.2f})")
    print(f"  closer in {st['closer']}/{st['n']} songs, mean {st['mean_closer']:.0f}%")
    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print(f"LOO MATCH OK: a song he has never seen lands {st['mean_after']:.2f} dB from "
          f"his own consensus, inside his {st['worst_before']:.2f} dB spread")
    return 0


if __name__ == "__main__":
    sys.exit(main())
