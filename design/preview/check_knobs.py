#!/usr/bin/env python3
"""
check_knobs.py - the dials say what the numbers say.

Two bugs lived in the same twelve lines of the editor and neither one was visible in a
screenshot you glance at:

  1. drawRotarySlider was handed `s.getValue()` - the value in *parameter* units - where
     an angle in turns was expected.  "strength" runs 0..100 %, so 100 % became
     -135 + 270 * 100 = 26 865 degrees: the needle span like a slot machine and the
     value arc wrapped the ring several times.  The knob still ended up pointing at the
     right place, which is why it survived a look-at-it review.
  2. the value ring is drawn at 1.19 x the body radius, and a child component is clipped
     to its own bounds.  The box was 42 px - the body - so the ends of every ring were
     cut off at the top of the knob.

This gate measures the thing a person would notice on the *shipped* screenshot: how much
of each value ring is lit, and whether the lit part reaches the ends of the travel.

It is a pixel gate on purpose.  The shot is produced by the real editor through the real
paint code, so it cannot be made to agree by editing a constant - it agrees when the
drawing code is right.

    /tmp/venv/bin/python design/preview/check_knobs.py
    /tmp/venv/bin/python design/preview/check_knobs.py --shot docs/ui-1.2.12-window.png
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

# The design grid, read from the module that draws the approved panel.  Only numbers -
# importing it is safe without a display.
sys.path.insert(0, HERE)
import ui_v9 as ui  # noqa: E402

# What the screenshot test sets: the two parameters are left at their defaults, so the
# left dial is "strength" at 100 % and the right one is "auto-stop" at OFF (0 of 0..300).
EXPECTED = {"strength": (1.00, (ui.W - 158, (ui.FOOTER[1] + ui.FOOTER[3]) / 2)),
            "auto-stop": (0.00, (ui.W - 30, (ui.FOOTER[1] + ui.FOOTER[3]) / 2))}

BOX = 54.0          # the child's bounds, in design px (body 42 + ring + seat)
RING = 0.39 * BOX * 1.19   # where the value arc is drawn, in design px

TOLERANCE = 0.15    # of the ring's travel
SAT_FLOOR = 0.25    # the arc is the accent; the metal around it is not
BUCKETS = 72        # 5 degrees per bucket


def hue_deg(px) -> float:
    r, g, b = [c / 255.0 for c in px[:3]]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    return h * 360.0, s, v


def arc_coverage(im: Image.Image, centre, want_hue: float, radius: float) -> tuple[float, list[int]]:
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
        # the window's height (box = 54 * sy) and this gate must not be a scale test
        for dr in (-4, -3, -2, -1, 0, 1, 2, 3, 4):
            x = int(round(cx + math.sin(a) * (r + dr)))
            y = int(round(cy - math.cos(a) * (r + dr)))
            if 0 <= x < w and 0 <= y < h:
                hue, s, v = hue_deg(px[x, y])
                if s >= SAT_FLOOR and v >= 0.25 and abs((hue - want_hue + 180) % 360 - 180) <= 30:
                    lit[i] = 1
                    break
    # the arc starts at the beginning of the travel (-135) and runs to the value, so the
    # count from the start is what a value of 0..1 means
    run = 0
    for i in range(BUCKETS - 1, -1, -1):
        if lit[i]:
            break
        run += 1
    return (BUCKETS - run) / BUCKETS, lit


def selftest() -> list[str]:
    """Can the gate read a ring it drew itself?

    A measurement that cannot be checked against a known answer is not a measurement.  The
    arc below is the one the editor draws now - JUCE measures from 12 o'clock, clockwise,
    with the travel at -135..+135 and the lit part from -135 to the value.  PIL measures
    from 3 o'clock, so the dial's -135 (bottom left) is PIL's 135, and the travel runs
    135..405.  Three values, three known answers - and the 25 % one is the check that
    matters: at 0 % and 100 % any offset in the mapping still gives the right answer.
    """
    problems: list[str] = []
    hue = hue_deg(ui.BRASS)[0]
    cy = (ui.FOOTER[1] + ui.FOOTER[3]) / 2.0
    for value in (0.0, 0.25, 1.0):
        im = Image.new("RGB", (ui.W, ui.H), (20, 14, 32))
        if value > 0.0:
            cx, c = ui.W - 158, cy
            ImageDraw.Draw(im).arc([cx - RING, c - RING, cx + RING, c + RING],
                                   start=135, end=135 + 270 * value,
                                   fill=tuple(ui.BRASS), width=3)
        got, _ = arc_coverage(im, (ui.W - 158, cy), hue, RING)
        if abs(got - value) > 0.06:
            problems.append(f"self-test: a ring drawn at {value:.0%} reads {got:.0%} - the "
                            "gate cannot see its own drawing, so it cannot judge a screenshot")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=os.path.join(ROOT, "docs", "ui-1.2.13-window.png"))
    args = ap.parse_args()

    if not os.path.exists(args.shot):
        print(f"FAIL no screenshot at {args.shot}")
        return 1

    problems = selftest()
    if problems:
        for p in problems:
            print("FAIL " + p)
        print(f"{len(problems)} problem(s)")
        return 1

    im = Image.open(args.shot).convert("RGB")
    want = hue_deg(ui.BRASS)[0]
    notes: list[str] = []

    for name, (expect, centre) in EXPECTED.items():
        cov, lit = arc_coverage(im, centre, want, RING)
        notes.append(f"{name} {cov:.0%} of the ring (expected {expect:.0%})")
        if abs(cov - expect) > TOLERANCE:
            problems.append(f"{name}: the ring is lit for {cov:.0%} of its travel, the "
                            f"parameter says {expect:.0%} - the dial is not drawing the value")
        if expect > 0.5:
            # a full ring has to be lit at *both* ends of the travel: this is the check
            # that the child's bounds hold the ring instead of clipping it
            if not (lit[0] and lit[-1]):
                problems.append(f"{name}: the ring is missing its {'start' if not lit[0] else 'end'}"
                                " - the value arc is being clipped by the knob's bounds")

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
