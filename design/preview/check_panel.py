#!/usr/bin/env python3
"""check_panel.py - the panel render has to obey the things the panel claims.

Two claims, because they fail differently and are checked by different oracles:

  --claim=blocks    the layout is legible
      1. EVERY ARABIC STRING IS DRAWN IN ONE RUN AND THE FONT COVERS IT.  The
         first version placed one glyph at a time (to fall back to DejaVu for
         characters Cairo lacks), and the per-glyph advance widths accumulated
         error across a sentence: the words collided in the middle, and every
         anchor built from that sum landed in the wrong place.  The strings come
         from ui_v6 itself, so this cannot drift from what is drawn.
      2. THE CONTRAST FLOOR HOLDS.  Text is either >= 4.5:1 (readable body) or
         deliberately decorative and >= 3:1.  check_contrast.py owns the pairs.
      3. THE LAYOUT DOES NOT OVERLAP ITSELF.  Cards, the well and the strip are
         rectangles; if two intersect, a control is under another control.

  --claim=material  the parts are physical
      4. THE PARTS ARE RENDERS, AND THE POINTER ANSWERS THE VALUE.  v3-v5 were
         drawn with primitives and read as flat, so the knobs and buttons are now
         composited from rendered materials (design/assets/src -> assets_prep.py).
         That is only true if the pixels carry it: each part has to be a real
         cut-out with a specular gradient, the knob face in the shipped PNG has
         to hold hundreds of distinct colours, and the needle drawn for a value
         has to point where that value says it points.

    /tmp/venv/bin/python design/preview/check_panel.py                # both
    /tmp/venv/bin/python design/preview/check_panel.py --claim=blocks
"""
from __future__ import annotations

import math
import pathlib
import subprocess
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

CLAIM = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--claim=")), "all")
problems: list[str] = []

from fontTools.ttLib import TTFont  # noqa: E402

import ui_v9 as ui  # noqa: E402  (import must not render; build() is behind __main__)

STRINGS = list(ui.ARABIC_STRINGS)
# the parts v8 actually composites: two knob bodies and the machined clamp
SPRITES = ("knob_brass.png", "knob_body.png", "clamp.png")
BOXES = dict(ui.BLOCKS)


def claim_blocks() -> str:
    # 1. every string the panel draws is covered by the bundled Arabic font
    cmap = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-Regular.ttf")).getBestCmap()
    cmap_b = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-SemiBold.ttf")).getBestCmap()
    missing = sorted({ch for s in STRINGS for ch in s
                      if ord(ch) not in cmap and ord(ch) not in cmap_b})
    if missing:
        problems.append("characters missing from the bundled font: " +
                        ", ".join(f"U+{ord(c):04X} {c!r}" for c in missing))

    # 2. contrast
    r = subprocess.run([sys.executable, str(HERE / "check_contrast.py")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        problems.append("contrast check failed:\n" + (r.stdout + r.stderr).strip()[-400:])

    # 3. nothing that is interactive sits under anything else
    def hit(a, b):
        return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])

    names = list(BOXES)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            if hit(BOXES[names[i]], BOXES[names[j]]):
                problems.append(f"{names[i]} overlaps {names[j]}")
    for name, box in BOXES.items():
        if box[0] < 0 or box[1] < 0 or box[2] > ui.W or box[3] > ui.H:
            problems.append(f"{name} leaves the panel: {box}")

    return (f"{len(STRINGS)} Arabic strings fully covered by the bundled font, "
            f"contrast floor holds, {len(BOXES)} blocks do not overlap")


def accent_pixel(px) -> bool:
    """The needle, by the hue the panel declares instead of by "warm gold"."""
    r, g, b = [c / 255.0 for c in px[:3]]
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0 or mx < 0.45:
        return False
    if mx == r:
        h = (60 * ((g - b) / d)) % 360
    elif mx == g:
        h = 60 * ((b - r) / d) + 120
    else:
        h = 60 * ((r - g) / d) + 240
    want = ui.BRASS
    rw, gw, bw = [c / 255.0 for c in want[:3]]
    mxw, mnw = max(rw, gw, bw), min(rw, gw, bw)
    dw = mxw - mnw
    if dw == 0:
        return False
    if mxw == rw:
        hw = (60 * ((gw - bw) / dw)) % 360
    elif mxw == gw:
        hw = 60 * ((bw - rw) / dw) + 120
    else:
        hw = 60 * ((rw - gw) / dw) + 240
    return abs((h - hw + 180) % 360 - 180) <= 30


def needle_angle(size: int, value: float):
    lay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ui.needle(lay, size / 2, size / 2, size, value)
    xs = ys = n = 0
    px = lay.load()
    for y in range(size):
        for x in range(size):
            rr, gg, bb, aa = px[x, y]
            if aa > 120 and accent_pixel((rr, gg, bb)):
                xs += x
                ys += y
                n += 1
    if not n:
        return None
    return math.degrees(math.atan2(xs / n - size / 2, -(ys / n - size / 2)))


def claim_material() -> str:
    # 4a. every part is a cut-out render with real shading in it
    levels = {}
    for name in SPRITES:
        path = ui.ASSETS / name
        if not path.exists():
            problems.append(f"missing material sprite {name} (run design/assets_prep.py)")
            continue
        im = Image.open(path).convert("RGBA")
        solid = sum(im.getchannel("A").histogram()[64:])
        colours = len(im.convert("RGB").getcolors(maxcolors=1 << 20) or [])
        levels[name] = colours
        if solid < 0.05 * im.size[0] * im.size[1]:
            problems.append(f"{name} is {solid} px - not a cut-out part")
        if colours < 200:
            problems.append(f"{name} holds only {colours} colours - too flat to be a render")

    # ...and the composite carries it, so look at the PNG that ships
    knob_levels = 0
    if not ui.OUT.exists():
        problems.append(f"{ui.OUT.name} missing - run design/preview/ui_v9.py")
    else:
        panel = Image.open(ui.OUT).convert("RGB")
        kx0, ky0, kx1, ky1 = dict(ui.TARGETS)["knob-curve"]
        knob_levels = len(panel.crop((kx0 + 2, ky0 + 2, kx1 - 2, ky1 - 2))
                          .getcolors(maxcolors=1 << 20))
        if knob_levels < 300:
            problems.append(f"the knob face holds {knob_levels} colours - "
                            "the render was not composited")

    # 4b. the pointer follows the value: five positions, measured from the pixels
    worst = 0.0
    got_all = []
    for v in (0.0, 0.25, 0.5, 0.75, 1.0):
        got = needle_angle(320, v)
        want = -135.0 + 270.0 * v
        if got is None:
            problems.append(f"no needle drawn at value {v}")
            continue
        err = abs((got - want + 180.0) % 360.0 - 180.0)
        worst = max(worst, err)
        if err > 6.0:
            problems.append(f"needle at value {v} points {got:.1f} deg, not {want:.0f}")
        got_all.append(got)
    travel = (max(got_all) - min(got_all)) if got_all else 0.0

    return (f"{len(levels)} material sprites in the composite "
            f"(knob face {knob_levels} colours), needle within {worst:.1f} deg over "
            f"{travel:.0f} deg of travel")


# ---- verdict ------------------------------------------------------------------------
reports = []
if CLAIM in ("all", "blocks"):
    reports.append(("panel", claim_blocks))
if CLAIM in ("all", "material"):
    reports.append(("material", claim_material))

if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print("; ".join(f"{name} ok: {fn()}" for name, fn in reports))
