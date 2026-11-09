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

import ui_v9 as ui  # noqa: E402  (import must not render; build() is behind __main__)

problems: list[str] = []
notes: list[str] = []

# the bare margins of the panel: no text, no control, nothing but the field
# The four bare strips of the field, chosen where the design leaves them bare:
#   * left/right: the 20 px margin, from under the header to the chips row;
#   * top: the empty middle of the header, between the tagline and the seal pill.  It used to
#     be the 8 px strip above the well, which is where the panel's own top-lit vignette is -
#     the same mistake as the bottom band, one edge up.
#   * bottom: the 8 px between the chips row and the footer - NOT the last few rows of the
#     window, which are the panel's own vignette (a smooth gradient eight grey levels deep)
#     and, at the right-hand end, the two dials' seats.  Measuring there measured the fade,
#     not the field, and it failed the moment the footer moved 6 px down.
MARGINS = {"left": (4, 120, 20, 600), "right": (1160, 120, 1176, 600),
           "top": (470, 26, 1000, 44), "bottom": (300, 633, 900, 639)}
# The real measure of texture is high-frequency energy: v9's margins sit at 0.06-0.36
# and v7's fabric plate at 1.9-3.2, a 6x gap.  Colour count is a sanity bound only -
# a saturated gradient legitimately holds more distinct colours than a warm grey one,
# and refusing colour would refuse the thing the panel was asked to become.
# 1.2.9: the field is brighter, so its margins hold more distinct colours
# (615-709 against 42-109 in the near-black world).  Colour count was never the
# measure of texture - that is hf, and it still sits at 0.32-0.68 against v7's
# 1.9-3.2 - so the bound moves with the world and the texture ceiling does not.
MAX_MARGIN_COLOURS = 900
MAX_MARGIN_HF = 0.8


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
    print(f"FAIL {PANEL.name} missing - run design/preview/ui_v9.py")
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
src = (HERE / "ui_v9.py").read_text()
code = "\n".join(ln.split("#", 1)[0] for ln in src.splitlines())
plate = re.search(r'(?:sprite|Image\.open)\([^)\n]*(panel_plate|fabric|carbon)', code)
if plate:
    problems.append(f"the panel composites a textured ground again ('{plate.group(1)}') - "
                    "depth has to come from light and blur")

# ------------------------------------------------------------------ 2b. the metal is the world's metal
# Anodising is the one place where a *material* could smuggle in a third hue: the knob
# sprites were brass, and brass in a magenta panel is the accent nobody asked for.  So
# the knob face is measured, and it has to sit within 30 deg of the declared accent.
def hue_of(rgb):
    r, g, b = [c / 255.0 for c in rgb[:3]]
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        return None
    if mx == r:
        return (60 * ((g - b) / d)) % 360
    if mx == g:
        return 60 * ((b - r) / d) + 120
    return 60 * ((r - g) / d) + 240


knob_box = dict(ui.TARGETS)["knob-strength"]
face = img.crop((knob_box[0] + 14, knob_box[1] + 14, knob_box[2] - 14, knob_box[3] - 14))
sample = [p for p in list(face.get_flattened_data())
          if max(p) - min(p) > 26 and max(p) > 90]
acc_h = hue_of(ui.BRASS)
face_h = None
if sample:
    # circular mean of the saturated part of the knob face
    sx = sum(__import__("math").sin(hue_of(p) * 3.14159265 / 180) for p in sample
             if hue_of(p) is not None)
    cx = sum(__import__("math").cos(hue_of(p) * 3.14159265 / 180) for p in sample
             if hue_of(p) is not None)
    face_h = (__import__("math").degrees(__import__("math").atan2(sx, cx))) % 360
if face_h is None or acc_h is None:
    problems.append("no saturated knob face to measure - the metal was not composited")
else:
    diff = abs((face_h - acc_h + 180) % 360 - 180)
    if diff > 30:
        problems.append(f"the knob is {face_h:.0f} deg while the accent is {acc_h:.0f} - the "
                        "hardware is smuggling in a third hue")
    else:
        notes.append(f"knob anodised to {face_h:.0f} deg (accent {acc_h:.0f})")

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
if head[3] - head[1] > 60:
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
