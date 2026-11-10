#!/usr/bin/env python3
"""gate_loo_match.py - the leave-one-out tabulation reads what the CLI actually prints.

`loo_match.py` is the experiment that decides whether the shipped target is really Amir's
curve, so its failure mode matters more than most: if it cannot read one line of CLI output
it drops that song, prints a table of the survivors, and still exits 0.  That is exactly what
happened - the label is padded (`match to target  : AMIR LOO, 1.19 -> 1.01 dB RMS`) and the
parse looked for a different prefix - so the gate pins the parser to real output, byte for
byte, and then runs the whole thing on a real take through the real binary.

  1. the padded CLI line parses, and the band count comes from `(N bands)`
  2. anything that is not a match line returns None instead of a wrong number
  3. a run on a real take through build/aeq_cli produces both numbers and they move toward
     the target
  4. the verdict rejects a table where nothing closes, and accepts the real 15-song one

    python3 analysis/scripts/gate_loo_match.py       # prints LOO MATCH OK
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "build", "aeq_cli")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_match_render as vmr  # noqa: E402
from loo_match import parse_match, verdict  # noqa: E402

# one song's real output, copied from the run of 2026-11-01 (label padded by two spaces)
REAL = """  target            : AMIR LOO (from /tmp/loo/../loo.aeqprof)
  mode              : match
--- corrective EQ (1 bands)
match to target  : AMIR LOO, 1.19 -> 1.01 dB RMS  (15% closer)
"""

# the 15 rows of that run: (song, before, after, bands)
TABLE = [
    ("اتأخر عتابنا", 1.19, 1.01, 1), ("اسكت", 3.08, 1.77, 7), ("اسمعيني", 2.54, 0.99, 5),
    ("القمر", 4.08, 1.83, 6), ("ايامي", 1.75, 1.15, 2), ("بحاول", 1.80, 1.03, 4),
    ("توبه", 3.34, 1.57, 4), ("جرالك ايه", 4.76, 2.97, 6), ("جيتلك", 1.94, 1.20, 3),
    ("شايف قمر", 3.61, 2.15, 7), ("عمرنا مانسينا", 1.95, 1.07, 5),
    ("قالوا عني ايه", 1.62, 1.01, 3), ("كان برئ", 2.59, 1.21, 5),
    ("من غير كلام", 3.12, 1.24, 5), ("يا لطيف", 5.58, 3.66, 6),
]


def main() -> int:
    fails: list[str] = []

    # ---- 1. the padded line ----------------------------------------------------------
    got = parse_match(REAL)
    if got is None:
        fails.append("the real CLI line was not parsed at all")
    else:
        before, after, bands = got
        print(f"  parsed {before:.2f} -> {after:.2f} dB, {bands} bands")
        if abs(before - 1.19) > 1e-9 or abs(after - 1.01) > 1e-9:
            fails.append(f"parsed the numbers as {before}/{after}, the line says 1.19/1.01")
        if bands != 1:
            fails.append(f"read {bands} bands from a '(1 bands)' header")

    # the un-padded spelling must parse too: the label is padded by a formatting width, and
    # that width is allowed to change
    alt = parse_match("match to target: X, 2.00 -> 1.50 dB RMS  (25% closer)")
    if alt is None or abs(alt[0] - 2.0) > 1e-9 or abs(alt[1] - 1.5) > 1e-9:
        fails.append(f"the unpadded spelling parsed as {alt}, expected (2.00, 1.50, ...)")
    else:
        print("  the unpadded spelling parses to the same two numbers")

    # ---- 2. nothing that is not a match line may be believed --------------------------
    for junk in ("--- corrective EQ (7 bands)", "no reference given", ""):
        if parse_match(junk) is not None:
            fails.append(f"{junk!r} was read as a match line")
    print("  three non-match outputs return None, not a number")

    # ---- 3. one real take, through the real binary -----------------------------------
    if not os.path.exists(CLI):
        fails.append("no build/aeq_cli - run sh scripts/build_linux_tests.sh")
    else:
        with tempfile.TemporaryDirectory() as tmp:
            bright = os.path.join(tmp, "bright.wav")
            dull = os.path.join(tmp, "dull.wav")
            vmr.make_take(bright, 0.0)          # the reference
            vmr.make_take(dull, -6.0)           # a dull take, 6 dB/oct down above 1 kHz
            ref = os.path.join(tmp, "ref.aeqprof")
            p = subprocess.run([CLI, "profile", bright, ref, "--name", "GATE REF"],
                               cwd=ROOT, capture_output=True, text=True)
            if p.returncode != 0:
                fails.append(f"could not measure the reference: {p.stdout[-200:]}")
            else:
                q = subprocess.run([CLI, "analyze", dull, "--ref", ref, "--mode", "match",
                                    "--width", "0.166667"],
                                   cwd=ROOT, capture_output=True, text=True)
                got = parse_match(q.stdout) if q.returncode == 0 else None
                if got is None:
                    fails.append(f"the analyse command produced no readable match line:\n"
                                 f"{q.stdout[-300:]}{q.stderr[-200:]}")
                else:
                    before, after, bands = got
                    print(f"  a real dull take: {before:.2f} -> {after:.2f} dB, "
                          f"{bands} bands")
                    if after >= before:
                        fails.append(f"the curve did not move the take toward the target "
                                     f"({before:.2f} -> {after:.2f} dB)")
                    if bands <= 0:
                        fails.append("the match produced no bands")

    # ---- 4. the verdict ---------------------------------------------------------------
    ok_fails, st = verdict(TABLE)
    print(f"  the real table: mean {st['mean_before']:.2f} -> {st['mean_after']:.2f} dB, "
          f"{st['closer']}/{st['n']} closer")
    if ok_fails:
        fails.append("the real 15-song table is rejected: " + "; ".join(ok_fails))
    if st["closer"] != 15:
        fails.append(f"only {st['closer']} of 15 songs were tabulated as closer - the table "
                     "itself says 15")
    bad = [(n, b, b, k) for n, b, a, k in TABLE]        # nothing closes
    bad_fails, bad_st = verdict(bad)
    print(f"  a table where nothing closes is rejected ({len(bad_fails)} reasons)")
    if not bad_fails:
        fails.append("the verdict accepted a table in which no song got closer")
    far = [(n, 1.0, 2.5, k) for n, b, a, k in TABLE]    # one song made worse
    if not any("farther" in f for f in verdict(far)[0]):
        fails.append("the verdict did not notice a song that came out farther than it went in")

    if fails:
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("LOO MATCH OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
