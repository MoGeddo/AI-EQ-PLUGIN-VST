#!/usr/bin/env python3
"""gate_stems_stats.py - the numbers under Amir's target are recomputable, not asserted.

`amir_stems_stats.py` produces the three numbers the whole target rests on: the spread
between his own mixes, the leave-one-out shift of the consensus, and the blended curve that
becomes `AmirTarget.h`.  Its output is a single line of prose, which is exactly the kind of
claim that survives by being unreadable.

So the gate does not run it on his audio (the stems are 128 MB and gitignored).  It builds
three curves whose statistics it can work out on paper, hands them over as per-song
profiles, and checks the printed numbers against an independent computation:

  1. the blend is a power mean, not a mean of decibels  (two curves 6 dB apart combine to
     3.26 dB, not 0) - the difference is read back out of the written profile
  2. a song that is the same shape, only louder, changes nothing: the normalisation to
     1 kHz is what makes the spread a claim about *shape*
  3. the leave-one-out shift and the per-band spread equal the analytic values to 0.01 dB

    python3 analysis/scripts/gate_stems_stats.py     # prints STEMS STATS OK
"""
from __future__ import annotations

import json
import math
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "analysis", "scripts", "amir_stems_stats.py")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amir_stems_stats import read_profile, write_profile  # noqa: E402

FREQS = [1000.0 * 2.0 ** ((k - 12) / 12.0) for k in range(25)]     # 500 Hz - 2 kHz
BUMP = 12                                                          # exactly 1 kHz


def analytic(curves: list[list[float]]) -> dict:
    """Power blend, per-band spread and LOO shift, written independently of the script."""
    def pmean(cs):
        return [10.0 * math.log10(sum(10.0 ** (c[i] / 10.0) for c in cs) / len(cs))
                for i in range(len(FREQS))]
    mean = pmean(curves)
    sd = [math.sqrt(sum((c[i] - sum(x[i] for x in curves) / len(curves)) ** 2
                        for c in curves) / len(curves)) for i in range(len(FREQS))]
    loo = []
    for k in range(len(curves)):
        rest = pmean([c for j, c in enumerate(curves) if j != k])
        loo.append(math.sqrt(sum((rest[i] - mean[i]) ** 2 for i in range(len(FREQS)))
                             / len(FREQS)))
    return {"mean": mean, "sd": sd, "loo_mean": sum(loo) / len(loo), "loo_worst": max(loo)}


def run(folder: str, out: str) -> tuple[int, str]:
    p = subprocess.run([sys.executable, SCRIPT, folder, "--reuse", "--out", out,
                        "--name", "GATE CURVES"],
                       cwd=ROOT, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def case(tmp: str, tag: str, curves: list[list[float]]) -> dict:
    """Write one set of per-song profiles, run the script, read the numbers back."""
    folder = os.path.join(tmp, tag)
    stems = os.path.join(folder, "stems")
    os.makedirs(stems)
    for i, c in enumerate(curves):
        write_profile(os.path.join(stems, f"{i:02d}.aeqprof"), FREQS, c, f"gate {i}")
    code, text = run(folder, folder)
    return {"code": code, "text": text, "out": folder}


def read_out(path: str) -> list[float]:
    return read_profile(path)[1]


def main() -> int:
    fails: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        # ---- 1 + 3. two curves 6 dB apart, plus a third 4 dB up at one band ------------
        # The curves are written already referenced to their own 1 kHz (0.0 at BUMP), so
        # the normalisation inside the script is the identity here and the analytic values
        # below need no copy of it.
        a = [6.0 if i < BUMP else 0.0 for i in range(len(FREQS))]
        b = [-6.0 if i < BUMP else 0.0 for i in range(len(FREQS))]
        c = list(a)
        c[BUMP - 3] += 4.0
        res = case(tmp, "power", [a, b, c])
        if res["code"] != 0:
            fails.append(f"the script exited {res['code']}:\n{res['text']}")
        else:
            want = analytic([a, b, c])
            prof = read_out(os.path.join(res["out"], "amir_stems.aeqprof"))
            if len(prof) != len(FREQS):
                fails.append(f"the blended profile has {len(prof)} bands, expected {len(FREQS)}")
            else:
                # band 0 is 500 Hz: two curves at +6/-6 plus one at +6 -> power mean 3.26 dB
                got = prof[0]
                db_mean = (a[0] + b[0] + c[0]) / 3.0
                print(f"  band 500 Hz: power mean {got:.2f} dB, dB mean {db_mean:.2f} dB")
                if abs(got - want["mean"][0]) > 0.01:
                    fails.append(f"the blend at 500 Hz is {got:.2f} dB, the power mean is "
                                 f"{want['mean'][0]:.2f} dB")
                if abs(got - db_mean) < 0.5:
                    fails.append("the blend cannot be told apart from a mean of decibels - "
                                 "the test is not discriminating power from dB")
            m = re.search(r"leave-one-out shift of the consensus: mean ([\d.]+) dB, "
                          r"worst ([\d.]+) dB", res["text"])
            if not m:
                fails.append(f"no leave-one-out line was printed:\n{res['text']}")
            else:
                lm, lw = float(m.group(1)), float(m.group(2))
                print(f"  leave-one-out printed {lm:.2f} / worst {lw:.2f} dB, "
                      f"analytic {want['loo_mean']:.2f} / {want['loo_worst']:.2f}")
                if abs(lm - want["loo_mean"]) > 0.01:
                    fails.append(f"the printed LOO mean {lm:.2f} dB is not the analytic "
                                 f"{want['loo_mean']:.2f} dB")
                if abs(lw - want["loo_worst"]) > 0.01:
                    fails.append(f"the printed LOO worst {lw:.2f} dB is not the analytic "
                                 f"{want['loo_worst']:.2f} dB")
            md = json.load(open(os.path.join(res["out"], "amir_measured.json")))
            worst = max(abs(x - y) for x, y in zip(md["sd_db"], want["sd"]))
            print(f"  per-band spread matches the analytic values to {worst:.4f} dB")
            if worst > 0.01:
                fails.append(f"the per-band spread is off by {worst:.2f} dB")

        # ---- 2. louder, not different -------------------------------------------------
        loud = [x + 7.0 for x in a]          # same shape, 7 dB louder
        quiet = case(tmp, "quiet", [a, b, c])
        noisy = case(tmp, "loud", [loud, b, c])
        if quiet["code"] or noisy["code"]:
            fails.append("the loudness-invariance run did not finish")
        else:
            q, n = read_out(os.path.join(quiet["out"], "amir_stems.aeqprof")), \
                read_out(os.path.join(noisy["out"], "amir_stems.aeqprof"))
            shift = max(abs(x - y) for x, y in zip(q, n)) if q and n else 99.0
            print(f"  one song 7 dB louder moves the blend by {shift:.4f} dB")
            if shift > 0.01:
                fails.append(f"a louder copy of the same shape moved the target by "
                             f"{shift:.2f} dB - normalisation is not running")

    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("STEMS STATS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
