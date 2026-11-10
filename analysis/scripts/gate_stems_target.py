#!/usr/bin/env python3
"""gate_stems_target.py - the stems-to-target command runs end to end and reports honestly.

`measure_amir_stems.py` is the one command that turns a folder of Amir's dry stems into a
target measured by the plugin's own analyser.  It cannot be tested on his material here
(the audio is not in this repository), but every part of it that can fail - the folder
walk, the skip-and-count rule, the profile write, and the comparison against the generated
header - runs on synthetic takes.

The gate also asserts the *shape* of the honesty: when the folder is not his material the
script must say the two curves disagree rather than printing a single comfortable number.

    python3 analysis/scripts/gate_stems_target.py        # prints STEMS TARGET OK
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "analysis", "scripts", "measure_amir_stems.py")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_match_render as vmr  # noqa: E402


def run(args: list[str]) -> tuple[int, str]:
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main() -> int:
    fails: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        stems = os.path.join(tmp, "stems")
        os.makedirs(stems)
        for i, tilt in enumerate((-6.0, -3.0, 0.0)):
            vmr.make_take(os.path.join(stems, f"stem{i}.wav"), tilt)
        # a silent stem: it has to be skipped and counted, never blended
        silent = os.path.join(stems, "silent.wav")
        vmr.make_take(silent, 0.0)
        size = os.path.getsize(silent)
        with open(silent, "r+b") as f:
            f.seek(44)
            f.write(b"\x00" * (size - 44))
        # a file the tool must not try to read as audio
        open(os.path.join(stems, "notes.txt"), "w").write("not audio\n")

        out = os.path.join(tmp, "amir.aeqprof")
        code, text = run([sys.executable, SCRIPT, stems, "--name", "GATE STEMS", "--out", out])
        if code != 0:
            fails.append(f"the command exited {code}:\n{text}")
        m = re.search(r"blended from (\d+) stems \((\d+) skipped\), name \"([^\"]+)\"", text)
        if not m:
            fails.append(f"the command did not report what it blended:\n{text}")
        else:
            used, skipped, name = int(m.group(1)), int(m.group(2)), m.group(3)
            if used != 3:
                fails.append(f"expected 3 measured stems, got {used}")
            if skipped < 1:
                fails.append("the silent stem was not skipped")
            if name != "GATE STEMS":
                fails.append(f"the profile is named {name!r}, not the name given")
        if not os.path.exists(out):
            fails.append("no profile was written")
        else:
            body = open(out, encoding="utf-8").read()
            if "GATE STEMS" not in body:
                fails.append("the name did not reach the file")
            if len([l for l in body.splitlines() if len(l.split()) == 2]) < 8:
                fails.append("the written profile is not a curve")

        # the comparison: it must print a number, and with the wrong material it must say so
        c = re.search(r"(\d+) shared bands, 200 Hz - 13 kHz: ([\d.]+) dB RMS, tilt ([-+][\d.]+) dB/octave",
                      text)
        if not c:
            fails.append(f"no header comparison was printed:\n{text}")
        else:
            print(f"  {c.group(1)} shared bands, {c.group(2)} dB RMS, tilt {c.group(3)} dB/octave")
        if "disagree by more than 3 dB" not in text:
            fails.append("a synthetic folder is not his material, and the script did not say the "
                         "two curves disagree - it is printing comfort instead of a finding")

    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("STEMS TARGET OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
