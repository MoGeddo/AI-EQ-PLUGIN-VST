#!/usr/bin/env python3
"""gate_reference_roundtrip.py - a saved reference is the same curve, and the same audio.

The promise a mixer keeps on a session is "reopen it and it sounds like it did".  For a
match EQ that promise is two round trips:

    curve -> file -> curve          (parser and writer agree to the bit)
    same reference -> same audio    (no hidden state in the solve)

This script builds both from one take and fails on any difference.  It is the file-level
half of the plugin state test: the plugin stores the reference *as text* in the project, so
the text surviving this loop is the text surviving a save and a reopen.

    python3 analysis/scripts/gate_reference_roundtrip.py      # prints REFERENCE ROUND TRIP OK
"""
from __future__ import annotations

import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_match_render as vmr  # noqa: E402


def digest(path: str) -> str:
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def run(args: list[str]) -> tuple[int, str]:
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main() -> int:
    if not os.path.exists(CLI):
        print("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
        return 2
    fails: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        take = os.path.join(tmp, "take.wav")
        vmr.make_take(take, -6.0)

        a = os.path.join(tmp, "a.aeqprof")
        b = os.path.join(tmp, "b.aeqprof")
        code, out = run([CLI, "profile", take, a, "--name", "ROUND TRIP"])
        if code != 0:
            fails.append(f"profile failed ({code}):\n{out}")
        code, out = run([CLI, "profile", "--rewrite", a, b])
        if code != 0:
            fails.append(f"rewrite failed ({code}):\n{out}")
        if os.path.exists(a) and os.path.exists(b) and digest(a) != digest(b):
            # print where they differ, because a digest mismatch alone is not a diagnosis
            la = open(a, encoding="utf-8").read().splitlines()
            lb = open(b, encoding="utf-8").read().splitlines()
            diff = next((f"line {i + 1}: {x!r} vs {y!r}"
                         for i, (x, y) in enumerate(zip(la, lb)) if x != y),
                        f"{len(la)} lines vs {len(lb)}")
            fails.append(f"the profile changed when it was read and written back: {diff}")
        else:
            print(f"  curve -> file -> curve: {digest(a)[:12]} both times")

        # same reference, same audio - twice through the whole solve and render path
        o1 = os.path.join(tmp, "out1.wav")
        o2 = os.path.join(tmp, "out2.wav")
        for target in (o1, o2):
            code, out = run([CLI, "process", take, target, "--ref", a, "--mode", "match"])
            if code != 0:
                fails.append(f"process failed ({code}):\n{out}")
                break
        if os.path.exists(o1) and os.path.exists(o2):
            if digest(o1) != digest(o2):
                fails.append("two renders of the same take against the same reference differ")
            else:
                print(f"  same reference -> same audio: {digest(o1)[:12]} both times")
            # and the reference really is the target, not a label on the corpus average
            # ... and the thing the solver targeted carries the reference's name, from the
            # core's own mouth (`core.targetName()`), not from the CLI's loading message -
            # the earlier version of this check passed even when the core was solving for
            # the corpus average, because "reference: ..." is printed by the loader.
            code, out = run([CLI, "analyze", take, "--ref", a, "--mode", "match"])
            m = re.search(r"match to target\s+: (.+?),", out)
            if not m:
                fails.append(f"no target was reported at all:\n{out}")
            elif m.group(1) != "ROUND TRIP":
                fails.append(f"the solve targeted {m.group(1)!r}, not the reference")

    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("REFERENCE ROUND TRIP OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
