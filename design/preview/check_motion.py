#!/usr/bin/env python3
"""check_motion.py - the motion spec is the spec, and it compiles.

Two repos fed this, and neither of them is about plugins:

  * transitions.dev - a motion token scale (durations, easings, distances) that the
    whole web design community has already argued about, so our numbers can be
    checked against an outside table instead of against my taste;
  * gsap-skills - the performance rule underneath any motion library: animate
    transform and opacity, never layout, and never animate a thing the user is
    currently dragging.

So plugin/Source/ui/MotionSpec.h is checked three ways:

  1. TOKENS MATCH THE TABLE.  Every duration and easing control point in the header
     must equal the value recorded here from the transitions.dev skill.  If someone
     "tunes" 250 ms to 220 ms, this fails and they have to update the table in the
     same commit - which is the point.
  2. THE PRODUCT'S MOTIONS ARE ON TOKENS.  The three motions the plugin has (clamp
     drop, seal word, LED) may only reference named durations and named easings, and
     none of them may use a bounce curve.
  3. IT COMPILES.  The header is dependency-free C++ on purpose; if it does not
     compile on its own, it is a sketch, not a spec.  Compiled with -std=c++17 -Wall
     -Wextra -Werror in a temp dir, and a tiny program checks that the easing curve
     actually eases (f(0)=0, f(1)=1, monotonic, and the midpoint is where the curve
     says it is).

    /tmp/venv/bin/python design/preview/check_motion.py
"""
from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SPEC_PATH = ROOT / "plugin" / "Source" / "ui" / "MotionSpec.h"

# recorded from the transitions.dev polish skill (docs/21_SKILLS_APPLIED_AR.md §6)
TOKENS = {
    "stagger": 40, "micro": 80, "quick": 150, "fast": 250,
    "medium": 350, "slow": 400, "verySlow": 500,
}
EASINGS = {
    "smoothOut":    (0.22, 1.00, 0.36, 1.00),
    "inOut":        (0.42, 0.00, 0.58, 1.00),
    "out":          (0.00, 0.00, 0.58, 1.00),
    "linear":       (0.00, 0.00, 1.00, 1.00),
    "bounce":       (0.34, 1.36, 0.64, 1.00),
    "bounceStrong": (0.34, 3.85, 0.64, 1.00),
}
DISTANCES = {"distMicro": 4.0, "distBase": 8.0, "distMedium": 12.0, "distLarge": 30.0}

problems: list[str] = []
notes: list[str] = []

if not SPEC_PATH.exists():
    print(f"FAIL {SPEC_PATH.relative_to(ROOT)} is missing")
    sys.exit(1)
spec = SPEC_PATH.read_text()

# ------------------------------------------------------------------ 1. the tokens
for name, want in TOKENS.items():
    m = re.search(rf"inline constexpr int {name}\s*=\s*(\d+)\s*;", spec)
    if not m:
        problems.append(f"duration token '{name}' is not declared")
    elif int(m.group(1)) != want:
        problems.append(f"duration '{name}' is {m.group(1)} ms, the table says {want} ms")

for name, want in EASINGS.items():
    m = re.search(rf"inline constexpr Bezier {name}\s*\{{\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\s*\}}", spec)
    if not m:
        problems.append(f"easing '{name}' is not declared")
        continue
    got = tuple(float(g) for g in m.groups())
    if any(abs(a - b) > 1e-6 for a, b in zip(got, want)):
        problems.append(f"easing '{name}' is {got}, the table says {want}")

for name, want in DISTANCES.items():
    m = re.search(rf"inline constexpr float {name}\s*=\s*([\d.]+)f\s*;", spec)
    if not m:
        problems.append(f"distance '{name}' is not declared")
    elif abs(float(m.group(1)) - want) > 1e-6:
        problems.append(f"distance '{name}' is {m.group(1)} px, the table says {want} px")
notes.append(f"{len(TOKENS)} durations + {len(EASINGS)} easings + {len(DISTANCES)} distances "
             "match the token table")

# ------------------------------------------------------------------ 2. product motions
motions = re.findall(r'inline constexpr Motion (\w+)\s*\{ "([\w-]+)", motion::(\w+), (\w+), ([\w.]+) \}',
                     spec)
if len(motions) != 3:
    problems.append(f"expected 3 product motions, found {len(motions)}")
for var, label, dur, ease, dist in motions:
    if dur not in TOKENS:
        problems.append(f"'{label}' uses duration {dur}, which is not a token")
    if ease not in EASINGS:
        problems.append(f"'{label}' uses easing {ease}, which is not a token")
    if ease in ("bounce", "bounceStrong"):
        problems.append(f"'{label}' uses a bounce curve - banned for product motion")
    if dist not in DISTANCES and dist != "0.0f":
        problems.append(f"'{label}' uses distance {dist}, which is not a token")
labels = [m[1] for m in motions]
notes.append("3 product motions: " + ", ".join(labels))

for flag in ("animateWindow", "animateKnobDrag", "animateMeters"):
    if not re.search(rf"inline constexpr bool {flag}\s*=\s*false", spec):
        problems.append(f"{flag} must be false - this product does not animate that")

# ------------------------------------------------------------------ 3. it compiles
cxx = shutil.which("g++") or shutil.which("clang++")
if not cxx:
    problems.append("no C++ compiler to verify the header with")
else:
    test = r"""
#include "MotionSpec.h"
#include <cstdio>
using namespace aeq::ui::motion;
int main()
{
    const Bezier curve = smoothOut;
    const double a = curve.at (0.0), b = curve.at (1.0), mid = curve.at (0.5);
    if (a < -1e-9 || a > 1e-9) { std::printf ("at(0)=%f\n", a); return 1; }
    if (b < 1.0 - 1e-9 || b > 1.0 + 1e-9) { std::printf ("at(1)=%f\n", b); return 2; }
    // smooth-out must be ahead of linear at the midpoint, and monotonic overall
    if (! (mid > 0.5 && mid < 1.0)) { std::printf ("mid=%f\n", mid); return 3; }
    double prev = -1.0;
    for (int i = 0; i <= 100; ++i)
    {
        const double v = curve.at (i / 100.0);
        if (v < prev - 1e-9) { std::printf ("not monotonic at %d\n", i); return 4; }
        prev = v;
    }
    // the three motions must be resolvable at compile time and inside the token set
    static_assert (seal::clampDrop.durationMs == fast, "clamp must use the fast token");
    static_assert (! bounceAllowedInProductMotion, "bounce must be banned");
    std::printf ("curve ok: at(0)=%.3f at(.5)=%.3f at(1)=%.3f\n", a, mid, b);
    return 0;
}
"""
    with tempfile.TemporaryDirectory() as td:
        tdp = pathlib.Path(td)
        (tdp / "MotionSpec.h").write_text(spec)
        (tdp / "t.cpp").write_text(test)
        r = subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", td,
                            str(tdp / "t.cpp"), "-o", str(tdp / "t")],
                           capture_output=True, text=True)
        if r.returncode != 0:
            problems.append("MotionSpec.h does not compile:\n" + (r.stderr or r.stdout)[-500:])
        else:
            run = subprocess.run([str(tdp / "t")], capture_output=True, text=True)
            if run.returncode != 0:
                problems.append("the easing curve is wrong: " + (run.stdout + run.stderr).strip())
            else:
                curve = run.stdout.strip()
                notes.append(f"{curve} (compiled with {cxx.split('/')[-1]} -Werror)")

# ------------------------------------------------------------------ verdict
if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print("motion ok: " + "; ".join(notes))
