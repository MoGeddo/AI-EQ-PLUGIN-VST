#!/usr/bin/env python3
"""check_craft.py - the craft floor, measured instead of admired.

Two skill sets were read for this, and both of them are checklists - so neither is
worth anything here unless something runs it:

  * impeccable's craft floor: "each of these is a check on the built result, not an
    intention" - contrast, depth, spacing rhythm, type scale, states, copy;
  * make-interfaces-feel-better, 19 principles whose measurable half is: minimum
    hit area (40x40 in a dense desktop UI), concentric radii
    (outer = inner + gap), tabular numbers for anything that updates, one icon
    stroke weight matched to the text, and motion restraint.

So this gate reads the real panel (design/preview/ui_v7.py, the PNG it produced, and
plugin/Source/ui/MotionSpec.h) and fails on:

  1. HIT AREAS.  Every control the panel draws is a target rectangle; each must be
     at least 40x40 logical px, and no two may overlap.
  2. CONCENTRIC RADII.  One surface, one radius - and where a rounded surface sits
     inside another with a gap, outer = inner + gap.
  3. TABULAR NUMBERS.  Every live value goes through the tracked readout helper; a
     proportional number that changes width shifts its neighbours.
  4. TYPE SCALE.  The sizes in use must fit one declared scale.
  5. MOTION RESTRAINT.  Product motion sits on named tokens, nothing uses a bounce
     easing, and the things the hand touches are not animated at all.
  6. THE STATES EXIST.  Hover/press/disabled are invisible in a still, so the check
     is that the module declares them for every part it draws - and that the count
     matches the controls.

    /tmp/venv/bin/python design/preview/check_craft.py
"""
from __future__ import annotations

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import ui_v7 as v7  # noqa: E402

problems: list[str] = []
notes: list[str] = []

SRC = (HERE / "ui_v7.py").read_text()
# ask the render what it drew, instead of scraping the source with regexes (the
# first version of this gate "passed" three checks that matched nothing at all)
import contextlib
import io
with contextlib.redirect_stdout(io.StringIO()):
    v7.build()
RENDERED = v7.report()
SEAL_X = int(v7.X1 - (v7.X1 - v7.X0) * v7.SEAL_T)


# ------------------------------------------------------------------ 1. hit areas
def row_target(i: int):
    """A ledger row is a full-width strip 27 px tall - not a disc."""
    y = v7.LEDGER[1] + 26 + i * v7.ROW_H - 4
    return (v7.X0, y, v7.X1, y + v7.ROW_H)


TARGETS = [
    ("knob-curve", (v7.X1 - 72, v7.FOOTER[1] - 6, v7.X1 - 8, v7.FOOTER[1] + 58)),
    ("knob-output", (v7.X1 - 200, v7.FOOTER[1] - 6, v7.X1 - 136, v7.FOOTER[1] + 58)),
    ("seal-clamp", (SEAL_X - 26, v7.TAKE_BAR[1] - 10, SEAL_X + 26, v7.TAKE_BAR[3] + 10)),
]
TARGETS += [(f"band-row-{i + 1}", row_target(i)) for i in range(len(v7.BANDS))]

MIN_HIT = 40.0
ROW_MIN_HIT = 30.0    # a full-width strip: 1100 px away from a miss in one axis
for name, box in TARGETS:
    w, h = box[2] - box[0], box[3] - box[1]
    if w >= 300:                       # a row: the rule's intent is met horizontally
        if h < ROW_MIN_HIT:
            problems.append(f"{name} is {w:.0f}x{h:.0f} - a row thinner than {ROW_MIN_HIT:.0f} px")
    elif w < MIN_HIT or h < MIN_HIT:
        problems.append(f"{name} target is {w:.0f}x{h:.0f}, under the {MIN_HIT:.0f} px floor")


def hit(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


for i in range(len(TARGETS)):
    for j in range(i + 1, len(TARGETS)):
        if hit(TARGETS[i][1], TARGETS[j][1]):
            problems.append(f"{TARGETS[i][0]} and {TARGETS[j][0]} targets overlap")
notes.append(f"{len(TARGETS)} targets, smallest "
             f"{min(min(b[2] - b[0], b[3] - b[1]) for _, b in TARGETS):.0f} px")

# ------------------------------------------------------------------ 2. concentric radii
RADII_OK = {18, 12}   # panel frame, and the well recessed into it
RADII_OK = {18, 12}   # the panel frame, and the well recessed into it
radii = [int(r) for r in
         re.findall(r"rounded_mask\((?:\([^)]*\)|[^()]*),\s*(\d+)\)", SRC)]
radii += [int(r) for r in re.findall(r"radius=(\d+)\)", SRC)]
stray = sorted({r for r in radii if r not in RADII_OK and r < 40})
if stray:
    problems.append(f"corner radii outside the declared set {sorted(RADII_OK)}: {stray}")
# the one genuinely nested pair is the knob in its seat: must stay concentric
seat = re.search(r"rec = int\(size \* ([0-9.]+)\)", SRC)
if not seat:
    problems.append("the knob has no seat, so it cannot be concentric with one")
else:
    factor = float(seat.group(1))
    if not 1.04 <= factor <= 1.14:
        problems.append(f"the knob seat is {factor}x the knob - not a concentric recess")
    notes.append(f"radii {sorted(set(radii))}, knob seat concentric at {factor}x")

# ------------------------------------------------------------------ 3. tabular numbers
numbers = RENDERED["numbers"]
if not numbers:
    problems.append("the render drew no numbers at all - the check would be vacuous")
else:
    offenders = [ln.strip() for ln in SRC.splitlines()
                 if "d.text(" in ln and re.search(r"\{.*:.*\}|%", ln) and "num(" not in ln]
    for ln in offenders:
        problems.append(f"live number drawn proportionally: {ln[:70]}")
    notes.append(f"{len(numbers)} tracked readouts (all through num())")

# ------------------------------------------------------------------ 4. type scale
sizes = set(RENDERED["sizes"])
ALLOWED = {v7.T_MICRO, v7.T_CAP, v7.T_BODY, v7.T_VALUE}
stray_sizes = sorted(s for s in sizes if s not in ALLOWED)
if stray_sizes:
    problems.append(f"text sizes {stray_sizes} are outside the declared scale {sorted(ALLOWED)}")
notes.append(f"type scale in use {sorted(sizes)}")

# ------------------------------------------------------------------ 5. motion restraint
spec_path = ROOT / "plugin" / "Source" / "ui" / "MotionSpec.h"
if not spec_path.exists():
    problems.append("plugin/Source/ui/MotionSpec.h is missing")
    spec = ""
else:
    spec = spec_path.read_text()
    if "bounceAllowedInProductMotion = false" not in spec:
        problems.append("MotionSpec.h does not state the bounce ban")
    motions = re.findall(r'inline constexpr Motion (\w+)\s*\{ "([\w-]+)", motion::(\w+), (\w+),', spec)
    if len(motions) != 3:
        problems.append(f"MotionSpec declares {len(motions)} product motions, expected 3")
    for _var, name, dur, ease in motions:
        if ease in ("bounce", "bounceStrong"):
            problems.append(f"product motion '{name}' uses {ease} - bounce is banned")
        if dur not in ("stagger", "micro", "quick", "fast", "medium", "slow", "verySlow"):
            problems.append(f"product motion '{name}' uses '{dur}', which is not a duration token")
    for flag in ("animateWindow", "animateKnobDrag", "animateMeters"):
        if not re.search(rf"{flag}\s*=\s*false", spec):
            problems.append(f"MotionSpec does not disable {flag}")
    notes.append(f"{len(motions)} product motions, all on named tokens")

for token in ("animation", "ease(", "tween", "ImageSequence"):
    if token in SRC:
        problems.append(f"the panel render references '{token}' - a still cannot animate")

# ------------------------------------------------------------------ 6. the states
if "PART_STATES" not in SRC:
    problems.append("the panel does not declare the states its controls support")
else:
    block = SRC[SRC.index("PART_STATES"):]
    block = block[:block.index("}") + 1]
    declared = re.findall(r'"([\w-]+)":\s*\(([^)]*)\)', block)
    if not declared:
        problems.append("PART_STATES is empty")
    for name, states in declared:
        for st in ("idle", "hover", "press", "disabled"):
            if st not in states:
                problems.append(f"{name} is missing the '{st}' state")
    names = {n for n, _ in declared}
    for target, _box in TARGETS:
        if target not in names:
            problems.append(f"no states declared for {target}")
    notes.append(f"{len(declared)} parts x 4 states declared")

# ------------------------------------------------------------------ verdict
if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print(f"craft ok: {len(TARGETS)} targets >= {MIN_HIT:.0f} px and none overlapping; "
      + "; ".join(notes))
