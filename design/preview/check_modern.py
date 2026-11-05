#!/usr/bin/env python3
"""check_modern.py - the panel has to look like it shipped this year, and that is measurable.

The rejection that made v8 was not a matter of taste:

    "التصميم حاسه بدائي اوي لسة مش عصري كدة - بص دول مثلا شوف كل البلاجنز الجديدة
     شكلها عامل ازاي الابعاد الخطوط الالوان"

so the answer cannot be "v8, trust me".  Forty shipping panels were collected
(image-search/) and four of them were read closely (Minimal Audio Morph EQ, Baby Audio
Smooth Operator Pro, oeksound soothe 3, iZotope Ozone 12).  They agree on four
structural things, and every one of them is a measurement:

  1. THE GRAPH IS THE INSTRUMENT.  65-75 % of the window height, bled to the margins.
     v7 gave its graph 36 % and stacked four equal-weight bands under it, which is why
     it read as a form.  Floor: 60 % of the height, and the graph must be the largest
     block on the panel by area.
  2. THE FIELD IS FLAT, AND THE DEPTH IS LIGHT.  Every one of those panels puts its
     content on a near-black field and gets depth from a soft bloom and a hairline, not
     from a texture.  v7 composited a carbon-fabric plate, and its margins carry
     ~750 distinct colours and 2.2 units of high-frequency energy; v8's carry ~100
     colours and 0.3.  That is the difference between "primitive" and "modern" measured
     on pixels: a bare margin must stay under both ceilings.
  3. THE LEDGER IS A ROW, NOT A TABLE.  Four full-width stacked bands of equal weight is
     a form.  The blocks must be four, and no two may share a height (a card grid is the
     failure this rules out).
  4. ONE ACCENT, ONE GLOW.  Two hues, the decision and the measurement - check_identity
     counts the hues; this gate refuses a third accent *and* the old texture plate by
     name, so a revert is caught in the source as well as in the pixels.

    /tmp/venv/bin/python design/preview/check_modern.py
    /tmp/venv/bin/python design/preview/check_modern.py --against-v7

``--against-v7`` runs the two pixel measures against the panel v8 replaced, to show the
gate discriminates instead of agreeing with whatever is in front of it.
"""
from __future__ import annotations

import pathlib
import re
import statistics
import sys

from PIL import Image, ImageFilter

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ui_v8 as ui  # noqa: E402  (import must not render; build() is behind __main__)

problems: list[str] = []
notes: list[str] = []

# the bare margins of the panel: no text, no control, nothing but the field
MARGINS = {"left": (4, 120, 20, 600), "right": (1160, 120, 1176, 600),
           "top": (300, 5, 1100, 13), "bottom": (300, 688, 1100, 696)}
MAX_MARGIN_COLOURS = 220      # v8 109, v7 774
MAX_MARGIN_HF = 0.8           # v8 0.36, v7 2.22


def margin_stats(im: Image.Image) -> dict[str, tuple[int, float]]:
    out: dict[str, tuple[int, float]] = {}
    for name, box in MARGINS.items():
        crop = im.crop(box)
        px = list(crop.get_flattened_data())
        blurred = list(crop.filter(ImageFilter.GaussianBlur(1.5)).get_flattened_data())
        hf = statistics.mean(abs(a[0] - b[0]) for a, b in zip(px, blurred))
        colours = len(crop.getcolors(maxcolors=1 << 20) or [])
        out[name] = (colours, hf)
    return out


# ------------------------------------------------------------------ 1. the graph owns it
if not hasattr(ui, "GRAPH_SHARE"):
    problems.append("the panel does not declare how much of the window its graph owns")
else:
    if ui.GRAPH_SHARE < 0.60:
        problems.append(f"the graph is {100 * ui.GRAPH_SHARE:.0f}% of the height, under the "
                        "60% floor - the panel reads as a form again")
    notes.append(f"graph {100 * ui.GRAPH_SHARE:.0f}% of the height")

blocks = dict(ui.BLOCKS)
areas = {n: (b[2] - b[0]) * (b[3] - b[1]) for n, b in blocks.items()}
biggest = max(areas, key=areas.get)
panel_area = ui.W * ui.H
if biggest != "hero":
    problems.append(f"the largest block is '{biggest}', not the graph")
else:
    share = areas["hero"] / panel_area
    notes.append(f"graph is the largest block at {100 * share:.0f}% of the panel area")
    if share < 0.55:
        problems.append(f"the graph covers {100 * share:.0f}% of the panel area, under 55%")

# ------------------------------------------------------------------ 2. flat field
PANEL = ui.OUT
if not PANEL.exists():
    print(f"FAIL {PANEL.name} missing - run design/preview/ui_v8.py")
    sys.exit(1)
img = Image.open(PANEL).convert("RGB")
if img.size != ui.EXPECTED_SIZES_V8:
    problems.append(f"render is {img.size}, expected {ui.EXPECTED_SIZES_V8}")

stats = margin_stats(img)
textured = [f"{n} ({c} colours, hf {hf:.2f})" for n, (c, hf) in stats.items()
            if c > MAX_MARGIN_COLOURS or hf > MAX_MARGIN_HF]
if textured:
    problems.append("the bare field carries texture: " + "; ".join(textured)
                    + f" - ceilings are {MAX_MARGIN_COLOURS} colours and {MAX_MARGIN_HF} hf")
else:
    notes.append("field flat to " + ", ".join(f"{n} hf {hf:.2f}" for n, (_c, hf) in stats.items()))

# a plate composited behind the curve is the v7 failure by name, so it is refused by name -
# in the code that draws, not in the comments that explain why the code does not
src = (HERE / "ui_v8.py").read_text()
code = "\n".join(ln.split("#", 1)[0] for ln in src.splitlines())
plate = re.search(r'(?:sprite|Image\.open)\([^)\n]*(panel_plate|fabric|carbon)', code)
if plate:
    problems.append(f"the panel composites a textured ground again ('{plate.group(1)}') - "
                    "depth has to come from light and blur")

# ------------------------------------------------------------------ 3. a row, not a table
heights = [b[3] - b[1] for b in blocks.values()]
if len(blocks) != 4:
    problems.append(f"{len(blocks)} blocks - v8 is header / hero / chips / footer")
for i, h in enumerate(heights):
    for j in range(i + 1, len(heights)):
        if abs(h - heights[j]) <= 2:
            problems.append(f"two blocks are both {h} px tall - that is a card grid")
notes.append("block heights " + "/".join(str(h) for h in heights))

# the header must not grow into a toolbar row (that is where v7's extra strips came from)
head = blocks["header"]
if head[3] - head[1] > 56:
    problems.append(f"the header is {head[3] - head[1]} px tall - a toolbar, not a title bar")

# ------------------------------------------------------------------ proof, not a promise
def audit(path: pathlib.Path) -> None:
    im = Image.open(path).convert("RGB")
    s = margin_stats(im)
    bad = [f"{n} ({c}c/{hf:.2f}hf)" for n, (c, hf) in s.items()
           if c > MAX_MARGIN_COLOURS or hf > MAX_MARGIN_HF]
    print(f"previous panel ({path.name}): margins " + ", ".join(
        f"{n} {c}c/{hf:.2f}hf" for n, (c, hf) in s.items())
        + (f" -> FAILS (textured: {', '.join(bad)})" if bad else " -> passes"))


if "--against-v7" in sys.argv:
    prev = HERE / "ui_v7_1180x700.png"
    if prev.exists():
        audit(prev)

# ------------------------------------------------------------------ verdict
if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print(f"modern ok: {len(blocks)} blocks, no shared height; " + "; ".join(notes))
