#!/usr/bin/env python3
"""gate_stems_profile.py - a folder of stems becomes one reference, and it lands.

The product claim behind Match EQ is "point it at a record and it aims there".  The plugin
can only do that if the reference is measured by *its own* analyser (G72) and if a whole
set of stems can be turned into one curve (that is what a record, or Amir's 15 dry stems,
actually is).  Until now that second half was a manual loop over files.

This gate builds the situation rather than describing it:

    three synthetic takes -> `aeq_cli profile --from-dir` -> one profile
    a fourth take, duller than all of them -> `aeq_cli analyze --ref <that profile>` -> Match

and it passes only when the blended profile is readable, carries the name it was given,
and the match it produces moves the take measurably closer than doing nothing.  The
synthetic takes are a harmonic voice-like series with syllable gaps, because the analyser
gates - a steady noise floor is *correctly* refused as "nothing measurable", which is why
the gate also checks that a silent file is skipped and counted instead of blended in.

    python3 analysis/scripts/gate_stems_profile.py         # prints STEMS PROFILE OK on success
"""
from __future__ import annotations

import math
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")


def wav(path: str, bright: float, seed: int, sr: int = 48000) -> None:
    """A voice-like series: 13 harmonics with a syllable envelope and gaps between them.

    `bright` scales how much of the upper harmonics survive: 1.0 keeps them all (bright),
    0.0 keeps only the first five (dull).  Deterministic, so the gate cannot drift.
    """
    f0 = 140.0
    out = []
    for k in range(9):
        n = int(0.35 * sr)
        phase = (seed + k) * 0.7
        for i in range(n):
            t = i / sr
            env = (i / n) ** 0.6 * (1.0 - i / n) ** 2.0
            v = 0.0
            for h in range(1, 14):
                keep = 1.0 if h <= 5 else bright
                v += keep * (0.5 / h) * (1.0 + 0.25 * (h % 3)) * abs(
                    math.sin(math.pi * f0 * h * t + phase))
            out.append(0.22 * env * v)
        out.extend([0.0] * int(0.20 * sr))
    data = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in out)
    header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt "
              + struct.pack("<IHHIIHH", 16, 1, 1, sr, sr * 2, 2, 16)
              + b"data" + struct.pack("<I", len(data)))
    with open(path, "wb") as f:
        f.write(header + data)


def run(args: list[str]) -> tuple[int, str]:
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main() -> int:
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        stems = os.path.join(tmp, "stems")
        os.makedirs(stems)
        for i, bright in enumerate((1.0, 0.6, 0.3)):
            wav(os.path.join(stems, f"take{i}.wav"), bright, seed=i)
        # A silent file must be skipped *by the analyser*, not averaged in: a dead stem
        # blended into a reference drags the whole curve towards the noise floor.
        silent = os.path.join(stems, "silent.wav")
        wav(silent, 0.0, seed=9)
        size = os.path.getsize(silent)
        with open(silent, "r+b") as f:          # all zero samples, valid header
            f.seek(44)
            f.write(b"\x00" * (size - 44))

        prof = os.path.join(tmp, "stems.aeqprof")
        code, out = run([CLI, "profile", "--from-dir", stems, prof, "--name", "STEMS TEST"])
        if code != 0:
            fails.append(f"profile --from-dir exited {code}:\n{out}")
        m = re.search(r"wrote \S+: (\d+) bands blended from (\d+) stems \((\d+) skipped\)", out)
        if not m:
            fails.append(f"the command did not report what it blended:\n{out}")
        else:
            bands, used, skipped = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if used != 3:
                fails.append(f"expected 3 usable stems, blended {used}")
            if skipped < 1:
                fails.append("the silent stem was blended instead of skipped")
            if bands < 8:
                fails.append(f"the blended profile is not a profile ({bands} bands)")

        if os.path.exists(prof):
            text = open(prof, encoding="utf-8", errors="replace").read()
            if "name STEMS TEST" not in text:
                fails.append("the name given on the command line did not reach the file")
        else:
            fails.append("no profile was written")

        # The take is duller than every stem, so Match has something real to do.
        take = os.path.join(tmp, "take_dull.wav")
        wav(take, 0.0, seed=7)
        code, out = run([CLI, "analyze", take, "--ref", prof, "--mode", "match"])
        if code != 0:
            fails.append(f"analyze --ref exited {code}:\n{out}")
        ref = re.search(r"reference: STEMS TEST \((\d+) bands", out)
        if not ref:
            fails.append(f"the profile did not come back as the reference:\n{out}")
        g = re.search(r"match to target  : (.+?), ([\d.]+) -> ([\d.]+) dB RMS\s+\((\d+)% closer\)", out)
        if not g:
            fails.append(f"no match number was printed:\n{out}")
        else:
            name, before, after, closer = g.group(1), float(g.group(2)), float(g.group(3)), int(g.group(4))
            if name != "STEMS TEST":
                fails.append(f"the target is named {name!r}, not the profile's name")
            if after >= before * 0.75:
                fails.append(f"the match moved {before:.2f} -> {after:.2f} dB ({closer}% closer): "
                             "less than a quarter of the gap, i.e. the reference is not being used")
            print(f"  blended 3 stems ({bands} bands), matched the dull take "
                  f"{before:.2f} -> {after:.2f} dB ({closer}% closer)")

    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("STEMS PROFILE OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
