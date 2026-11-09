#!/usr/bin/env python3
"""
check_knobs.py - the dials say what the numbers say.

Three bugs lived in the same twenty lines of the editor, and none of them is visible in a
screenshot you glance at:

  1. drawRotarySlider was handed `s.getValue()` - the value in *parameter* units - where a
     turn in 0..1 was expected.  "strength" runs 0..100 %, so 100 % became
     -135 + 270 * 100 = 26 865 degrees: the needle span like a slot machine and the value
     arc wrapped the ring several times.  The knob still ended up pointing at the right
     place, which is why it survived a look-at-it review.  It takes
     `valueToProportionOfLength` now.
  2. the value arc was drawn with its two angles the wrong way round - from the value to
     12 o'clock instead of from the start of the travel to the value - so the lit part sat
     on the wrong side of the knob and its length had nothing to do with the number.
  3. the ring is drawn at 1.19 x the body radius, and a child component is clipped to its
     own bounds.  The box was 42 px - the body - so the ends of every ring were cut off.

This gate measures the thing a person would notice on the *shipped* screenshot: how much
of each dial's travel is lit, and whether the lit part reaches the ends of the travel.

It is a pixel gate on purpose.  The shot is produced by the real editor through the real
paint code, so it cannot be made to agree by editing a constant - it agrees when the
drawing code is right.

It reads the two dial centres from the numbers the editor lays them out with
(`resized()`: the footer's right edge minus 158 and minus 30 design px, the footer's centre
row), so a dial that moves has to move in both places at once.

Before it judges anything it reads rings it drew itself at 0 %, 25 % and 100 %.

    /tmp/venv/bin/python design/preview/check_knobs.py
    /tmp/venv/bin/python design/preview/check_knobs.py --shot docs/ui-1.2.34-window.png
"""

from __future__ import annotations

import argparse
import colorsys
import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import ui_v9 as ui  # noqa: E402

# The two dials, in design px.  These are the editor's numbers, not the render's: the
# footer runs kMargin..kDesignW-kMargin, the dials sit 158 and 30 px inside its right
# edge, and they share its centre row.
# Imported from the module that draws the panel.  This file used to keep its own copy of
# the layout, and the copy drifted 22 px from the editor's on the very first change.
CY = (ui.FOOTER[1] + ui.FOOTER[3]) / 2.0
BOX = 54.0                                  # the child's bounds: body 42 + ring + seat
RING = 0.39 * BOX * 1.19                    # where the value arc is drawn
LEFT = (float(ui.KNOB_X[0]), CY)            # strength
RIGHT = (float(ui.KNOB_X[1]), CY)           # auto-stop
PITCH = float(ui.KNOB_PITCH)

# The screenshot test leaves both parameters at their defaults.
EXPECTED = (("strength", LEFT, 1.00), ("auto-stop", RIGHT, 0.00))

TOLERANCE = 0.15    # of the ring's travel
SELFTEST_TOLERANCE = 0.06
SAT_FLOOR = 0.30    # the arc is the accent; the metal around it is not
VAL_FLOOR = 0.45
HUE_WINDOW = 30.0
BUCKETS = 72        # 270 / 72 = 3.75 degrees per bucket


def hue_deg(px) -> tuple[float, float, float]:
    r, g, b = [c / 255.0 for c in px[:3]]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    return h * 360.0, s, v


def is_accent(px, want_hue: float) -> bool:
    h, s, v = hue_deg(px)
    return s >= SAT_FLOOR and v >= VAL_FLOOR and abs((h - want_hue + 180) % 360 - 180) <= HUE_WINDOW


def coverage(im: Image.Image, centre, want_hue: float, radius: float = RING):
    """How much of the -135..+135 travel is lit, and which buckets are lit."""
    w, h = im.size
    sx, sy = w / ui.W, h / ui.H
    cx, cy = centre[0] * sx, centre[1] * sy
    # the editor sizes a knob from the *vertical* scale (box = roundToInt (54 * sy)), so
    # the radius scales with sy even when the window is wider than the design
    r = radius * sy
    lit = [0] * BUCKETS
    px = im.load()
    for i in range(BUCKETS):
        a = math.radians(-135.0 + (270.0 / BUCKETS) * (i + 0.5))
        # a radial scan rather than a single radius: the ring's exact radius depends on
        # the window's height, and this gate must not be a scale test
        for dr in (-4, -3, -2, -1, 0, 1, 2, 3, 4):
            x = int(round(cx + math.sin(a) * (r + dr)))
            y = int(round(cy - math.cos(a) * (r + dr)))
            if 0 <= x < w and 0 <= y < h and is_accent(px[x, y], want_hue):
                lit[i] = 1
                break
    # the arc starts at the beginning of the travel (-135) and runs to the value, so the
    # count from the start is what a value of 0..1 means
    run = BUCKETS
    while run and not lit[run - 1]:
        run -= 1
    return run / BUCKETS, lit


def locate(im: Image.Image, near, want_hue: float):
    """Where the ring actually is, near where we expected it.  Only used to say something
    true in a failure message - a gate that reports "not drawing the value" when the dial
    has moved is guessing."""
    best = (0.0, near)
    for dx in range(-16, 17, 4):
        for dy in range(-16, 17, 4):
            cov, _ = coverage(im, (near[0] + dx, near[1] + dy), want_hue)
            if cov > best[0]:
                best = (cov, (near[0] + dx, near[1] + dy))
    return best


def selftest() -> list[str]:
    """Read rings the gate drew itself, and find them where it put them.

    The rings below are drawn with the editor's convention - JUCE measures from 12
    o'clock, clockwise, travel -135..+135, the lit part from -135 to the value.  PIL
    measures from 3 o'clock with the y axis down, so the dial's angle a is PIL's a - 90:
    -135 (lower left) is 135, and the travel runs 135..405.

    The 25 % case is the one that matters.  At 0 % and 100 % any 90 degree error in the
    mapping still produces the right answer, which is exactly how an earlier version of
    this gate passed a screenshot with the bug still in the build.
    """
    problems: list[str] = []
    hue = hue_deg(ui.BRASS)[0]
    for value in (0.0, 0.25, 1.0):
        im = Image.new("RGB", (ui.W, ui.H), (20, 14, 32))
        cx, cy = LEFT
        if value > 0.0:
            ImageDraw.Draw(im).arc([cx - RING, cy - RING, cx + RING, cy + RING],
                                   start=135, end=135 + 270 * value,
                                   fill=tuple(ui.BRASS), width=3)
        got, lit = coverage(im, LEFT, hue)
        if abs(got - value) > SELFTEST_TOLERANCE:
            problems.append(f"self-test: a ring drawn at {value:.0%} reads {got:.0%} - the "
                            "gate cannot read its own drawing, so it cannot judge a shot")
            continue
        if value > 0.5 and not (lit[0] and lit[-1]):
            problems.append("self-test: a full ring was drawn and the gate does not see "
                            "both ends of the travel lit")
    # and the empty plate: a dial at zero has no arc to find, so the gate must not invent one
    blank = Image.new("RGB", (ui.W, ui.H), (20, 14, 32))
    got, _ = coverage(blank, LEFT, hue)
    if got > SELFTEST_TOLERANCE:
        problems.append(f"self-test: an empty plate reads {got:.0%} - the gate is counting "
                        "something that is not the value ring")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=os.path.join(ROOT, "docs", "ui-1.2.34-window.png"))
    args = ap.parse_args()

    problems = selftest()
    if problems:
        for p in problems:
            print("FAIL " + p)
        print(f"{len(problems)} problem(s)")
        return 1

    if not os.path.exists(args.shot):
        print(f"FAIL no screenshot at {args.shot}")
        return 1

    im = Image.open(args.shot).convert("RGB")
    want = hue_deg(ui.BRASS)[0]
    notes: list[str] = []

    for name, centre, expect in EXPECTED:
        got, lit = coverage(im, centre, want)
        notes.append(f"{name} {got:.0%} of the ring (expected {expect:.0%})")
        if abs(got - expect) > TOLERANCE:
            where = ""
            if expect > 0.5:
                cov, found = locate(im, centre, want)
                if cov > got + 0.25:
                    where = (f" - a ring reading {cov:.0%} is at ({found[0]:.0f}, "
                             f"{found[1]:.0f}) design px, not at ({centre[0]:.0f}, "
                             f"{centre[1]:.0f}) where the layout puts the dial")
            problems.append(f"{name}: the ring is lit for {got:.0%} of its travel, the "
                            f"parameter says {expect:.0%} - the dial is not drawing the "
                            f"value it is attached to{where}")
        if expect > 0.5 and not (lit[0] and lit[-1]):
            problems.append(f"{name}: the ring is missing its "
                            f"{'start' if not lit[0] else 'end'} - the value arc is being "
                            "clipped by the knob's bounds")

    if problems:
        for p in problems:
            print("FAIL " + p)
        print(f"{len(problems)} problem(s)")
        return 1

    print("knobs ok: " + "; ".join(notes)
          + f"; the ring reaches both ends of the travel; hue {want:.0f} deg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
