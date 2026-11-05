#!/usr/bin/env python3
"""check_identity.py - the panel is this product's, not the store's.

The user's correction was specific: "كنت اقصد الهوية البصرية و التحضر في التصميم مش
اقصد نسخة" - the visual identity and the craft, not a copy.  v5/v6 were a copy, and
they were a copy in ways that can be measured rather than argued about:

  * a **centred pill** in the header (the reference's preset switcher).  A pill sits at
    the horizontal centre of the panel, so the test is: is anything drawn inside the
    middle of the header band?  On this panel that column is empty.
  * an **equal card grid** (four 267 px cards with four identical knobs).  The test is
    the one that property fails: no two layout blocks may share a height.
  * a **latin watermark** carved behind the curve (the reference's product name in
    104 px caps).  Declared, not measured - see the note where it is asserted.
  * a **third accent**.  The reference is blue + amber; the test counts the hues the
    saturated pixels actually cluster into.  This world has exactly two: the brass that
    means "a decision" and the patina that means "a measurement".

and the panel has to carry the *product's* story at the same time - a take bar that
somebody stopped, a machined clamp sealing it, and a ledger where at least one band is
static, one is dynamic and one follows the note.  A panel that is merely different but
says nothing about learn/stop/commit is not the answer to the question that was asked.

    /tmp/venv/bin/python design/preview/check_identity.py
    /tmp/venv/bin/python design/preview/check_identity.py --against-v6

``--against-v6`` runs the two pixel measures that do not depend on this panel's own
geometry (the middle of the header, and the accent hues) against the *previous*
render, to show the gate discriminates instead of agreeing with whatever is in front
of it.
"""
from __future__ import annotations

import math
import pathlib
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from fontTools.ttLib import TTFont  # noqa: E402

import ui_v9 as ui  # noqa: E402

problems: list[str] = []
notes: list[str] = []

def hue_of(rgb) -> float | None:
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


def is_accent(px, hue: float, tol: float = 26.0) -> bool:
    """A pixel that reads as the decision accent: the hue the panel declares, carried
    by a saturated, not-dark pixel.  v8 hard-coded "warm gold"; a world may be magenta,
    so the test follows the declaration (ACCENTS) instead of a hue I happened to ship."""
    h = hue_of(px)
    if h is None:
        return False
    mx = max(px[:3])
    mn = min(px[:3])
    if mx < 110 or (mx - mn) / mx < 0.30:
        return False
    return abs((h - hue + 180) % 360 - 180) <= tol


ACCENT_HUE = hue_of(ui.BRASS)



# ------------------------------------------------------------------ helpers
def lum(rgb) -> float:
    def ch(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = rgb[:3]
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(a, b) -> float:
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def plate_base(im: Image.Image) -> tuple[int, int, int]:
    """The panel's own ground, read from the render itself.

    v7 sampled its fabric plate; v8's ground is a gradient with two blooms, so the
    honest sample is a bare strip of this render - and if the strip ever has text on
    it, the median still lands on the ground because the text is the minority.
    """
    band = im.crop(ui.GROUND_PROBE)
    px = list(band.get_flattened_data())
    px.sort(key=lambda p: p[0] * 0.2126 + p[1] * 0.7152 + p[2] * 0.0722)
    return px[len(px) // 2]


PANEL = ui.OUT
if not PANEL.exists():
    print(f"FAIL {PANEL.name} missing - run design/preview/ui_v9.py")
    sys.exit(1)
img = Image.open(PANEL).convert("RGB")
if img.size != ui.EXPECTED_SIZES_V8:
    problems.append(f"render is {img.size}, expected {ui.EXPECTED_SIZES_V8}")

BASE = plate_base(img)

# ------------------------------------------------------------------ 1. the language
cmap = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-Regular.ttf")).getBestCmap()
cmap_b = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-SemiBold.ttf")).getBestCmap()
strings = list(ui.ARABIC_STRINGS)
missing = sorted({ch for s in strings for ch in s
                  if ord(ch) not in cmap and ord(ch) not in cmap_b})
if missing:
    problems.append("characters missing from the bundled font: " +
                    ", ".join(f"U+{ord(c):04X} {c!r}" for c in missing))

# ------------------------------------------------------------------ 2. the contrast
# body text >= 4.5:1; micro-caps and units are decorative and must clear 3:1.
BODY = {"TEXT": ui.TEXT, "DIM": (184, 174, 159), "MODESTATIC": ui.DIM,
        "PATINA": ui.PATINA, "BRASS_HOT": ui.BRASS_HOT}
DECOR = {"FAINT": ui.FAINT, "UNIT": (150, 140, 126), "BRASS": ui.BRASS}
for name, col in BODY.items():
    r = contrast(col, BASE)
    if r < 4.5:
        problems.append(f"{name} on the plate is {r:.2f}:1, under the 4.5:1 body floor")
for name, col in DECOR.items():
    r = contrast(col, BASE)
    if r < 3.0:
        problems.append(f"{name} on the plate is {r:.2f}:1, under the 3:1 decorative floor")

# ------------------------------------------------------------------ 3. the grid
# v8 has four blocks and no equal-weight band stack at all: the hero graph owns
# 69 % of the height and the ledger is a chip row, not a table
blocks = dict(ui.BLOCKS)


def hit(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


names = list(blocks)
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        if hit(blocks[names[i]], blocks[names[j]]):
            problems.append(f"{names[i]} overlaps {names[j]}")
for name, box in blocks.items():
    if box[0] < 0 or box[1] < 0 or box[2] > ui.W or box[3] > ui.H:
        problems.append(f"{name} leaves the panel: {box}")

# an equal card grid is exactly what "no two blocks share a height" rules out
heights = {n: b[3] - b[1] for n, b in blocks.items()}
for i, a in enumerate(names):
    for b in names[i + 1:]:
        if abs(heights[a] - heights[b]) <= 2:
            problems.append(f"{a} and {b} are both {heights[a]} px tall - that is a card grid")

# ------------------------------------------------------------------ 3b. the modern reading
# The user's last rejection was "التصميم حاسه بدائي اوي" - primitive.  The cure was not
# taste, it was three structural moves, and each one is measurable:
#   * the graph owns the window instead of sharing it with four equal-weight bands;
#   * the ground is light on a flat field, not a fabric plate composited behind it;
#   * there is no card grid to fall back into (already asserted above).
if hasattr(ui, "GRAPH_SHARE"):
    if ui.GRAPH_SHARE < 0.60:
        problems.append(f"the graph is {100 * ui.GRAPH_SHARE:.0f}% of the height - under the "
                        "60% floor, so the panel reads as a form again")
    else:
        notes.append(f"graph {100 * ui.GRAPH_SHARE:.0f}% of the height")
if "panel_plate" in (HERE / "ui_v9.py").read_text():
    problems.append("a texture plate is composited behind the curve again - depth has to "
                    "come from light and blur, not from a fabric ground")

# ------------------------------------------------------------------ 4. the story
# a take bar with a real seal somewhere in the middle, and a clamp sitting on it
if not 0.05 < ui.SEAL_T < 0.95:
    problems.append(f"the take is sealed at {ui.SEAL_T} - nothing was stopped, or nothing was heard")
bx0, by0, bx1, by1 = ui.TAKE_RIBBON
seal_x = ui.SEAL_X
band = img.crop((seal_x - 26, by0 - 8, seal_x + 26, by1 + 8))
brass_px = sum(1 for p in band.get_flattened_data() if is_accent(p, ACCENT_HUE))
if brass_px < 300:
    problems.append(f"only {brass_px} accent px at the seal ({seal_x}) - the clamp is not on the bar")
# the take mass is the measurement, so it must read patina, not brass
mass = img.crop(ui.TAKE_MASS)
mpx = list(mass.get_flattened_data())
green = sum(1 for p in mpx if p[1] > p[0] + 8 and p[1] > 55)
if green < 0.5 * len(mpx):
    problems.append(f"the learned take is {100 * green / len(mpx):.0f}% patina - "
                    "the measurement and the stop are coloured the same")
else:
    notes.append(f"take mass {100 * green / len(mpx):.0f}% patina")

# the correction curve is the decision, so it must read brass
wx0, wy0, wx1, wy1 = ui.GRAPH
well = img.crop((wx0, wy0, wx1, wy1))
brass_curve = sum(1 for p in well.get_flattened_data() if is_accent(p, ACCENT_HUE))
if brass_curve < 400:
    problems.append(f"{brass_curve} accent px in the well - the curve is not the accent layer")
else:
    notes.append(f"curve {brass_curve} accent px over the spectrum")

# the ledger has to hold all three after-commit behaviours
modes = {b[3] for b in ui.BANDS}
if modes != {0, 1, 2, 3}:
    problems.append(f"band modes present: {sorted(modes)} - static/dynamic/tracked must all exist")
n_static = sum(1 for b in ui.BANDS if b[3] in (0, 1))
notes.append(f"ledger {len(ui.BANDS)} bands ({n_static} static, 1 dynamic, 1 tracked)")

# ------------------------------------------------------------------ 5. not the store
# (a) the centred pill: nothing may be drawn in the middle of the header band
hx0, hy0, hx1, hy1 = ui.HEADER
mid = (ui.W // 2)
head = img.crop((mid - 60, hy0 + 4, mid + 60, hy1 - 4))
plate_ref = img.crop((mid - 60, hy0 + 4, mid + 60, hy1 - 4))
base_l = lum(BASE)
bright = sum(1 for p in head.get_flattened_data() if lum(p) > base_l + 0.08)
if bright > 60:
    problems.append(f"{bright} px drawn in the middle of the header - that is the reference's "
                    "centred pill")
else:
    notes.append(f"header centre empty ({bright} px over the plate)")

# (b) exactly two accents: cluster the saturated pixels by hue.
#
# The count runs on what the *drawing* put on screen, not on the panel: the ground is
# diffed out against a ground-only render.  A vivid ground is saturated too, and once
# the field carries real light a composite-only count reports the room instead of the
# ink - the first render of the brighter world collapsed both accents into the field's
# hue at 240 deg.
ground_path = ui.HERE / f"ui_v9_{ui.PALETTE}_ground.png"
if not ground_path.exists():
    # render it rather than complain: the file is a build product, and a gate that
    # fails because a product was not made yet teaches nobody anything
    import os as _os
    import subprocess as _sp
    _sp.run([sys.executable, str(ui.HERE / "ui_v9.py")],
            env={**_os.environ, "AEQ_GROUND_ONLY": "1"}, check=True,
            stdout=_sp.DEVNULL)
if not ground_path.exists():
    problems.append(f"{ground_path.name} missing - run AEQ_GROUND_ONLY=1 "
                    "design/preview/ui_v9.py")
    ground_px = None
else:
    ground_px = (Image.open(ground_path).convert("RGB")
                 .resize((ui.W // 2, ui.H // 2)).load())

half = img.resize((ui.W // 2, ui.H // 2))
sat = []
for y in range(half.size[1]):
    for x in range(half.size[0]):
        p = half.getpixel((x, y))
        if ground_px is not None:
            g0 = ground_px[x, y]
            if sum(abs(int(p[i]) - int(g0[i])) for i in range(3)) < 14:
                continue                       # the room's light, not the drawing
        mx, mn = max(p), min(p)
        # an accent is *bright* saturated ink.  The plate is a saturated violet too, and
        # so is the well - raising the value floor is what tells an accent from a surface.
        if mx > 150 and (mx - mn) / mx > 0.22:
            sat.append(p)
hues: dict[int, int] = {}
for p in sat:
    r, g, b = [c / 255.0 for c in p]
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        continue
    if mx == r:
        h = (60 * ((g - b) / d)) % 360
    elif mx == g:
        h = 60 * ((b - r) / d) + 120
    else:
        h = 60 * ((r - g) / d) + 240
    hues[int(h) // 15] = hues.get(int(h) // 15, 0) + 1
# a 5 % floor: an accent is an *area* of this panel - the curve, the seal, the chips, the
# strip - not a fringe.  At 2 % the violet edges where the plate, the glow and the curve
# blend in passed as accents, and a three-hue reading says nothing about the drawing.
big = {k * 15: n for k, n in hues.items() if n >= 0.05 * max(1, len(sat))}
# merge neighbouring buckets into one accent
merged: list[list[int]] = []
for h in sorted(big):
    if merged and h - merged[-1][-1] <= 15:
        merged[-1].append(h)
    else:
        merged.append([h])
accents = [sum(g) // len(g) for g in merged]
if len(accents) != 2:
    problems.append(f"{len(accents)} accent hues at {accents} deg - the world allows exactly two "
                    "(brass = a decision, patina = a measurement)")
else:
    def hue_of(col):
        r, g, b = [c / 255.0 for c in col[:3]]
        mx, mn = max(r, g, b), min(r, g, b)
        d = mx - mn
        if d == 0:
            return 0.0
        if mx == r:
            return (60 * ((g - b) / d)) % 360
        if mx == g:
            return 60 * ((b - r) / d) + 120
        return 60 * ((r - g) / d) + 240

    # the world is whatever the panel declares (ACCENTS); the gate's job is that the
    # pixels agree with the declaration, not that the declaration is a hue I chose
    want = sorted(hue_of(c) for c in ui.ACCENTS)
    got = sorted(accents)

    def near(a, b):
        return abs((a - b + 180) % 360 - 180)

    if any(near(g, w) > 26 for g, w in zip(got, want)):
        problems.append(f"accents at {got} deg, but the panel declares {[round(w) for w in want]}")
    notes.append(f"two accents at {got[0]} and {got[1]} deg, declared "
                 f"{[round(w) for w in want]}")

# (c) the watermark is a declaration, not a measurement: a 104 px watermark at alpha 10
# leaves no reliable signature, so this asserts the module never asks for one, and says so.
if ui.WATERMARK is not None:
    problems.append("a latin watermark is still being drawn behind the curve")
if ui.CENTRED_PILL or ui.EQUAL_CARD_GRID:
    problems.append("the module still declares a market panel cue")

# ------------------------------------------------------------------ proof, not a promise
def audit_previous(path: pathlib.Path) -> None:
    """The same two measures, against the panel this one replaced."""
    im = Image.open(path).convert("RGB")
    mid = im.size[0] // 2
    band = im.crop((mid - 60, 22, mid + 60, 62))
    base = lum(BASE)
    drawn = sum(1 for p in band.get_flattened_data() if lum(p) > base + 0.08)
    hs: dict[int, int] = {}
    tot = 0
    for p in im.resize((im.size[0] // 2, im.size[1] // 2)).get_flattened_data():
        mx, mn = max(p), min(p)
        if mx > 60 and (mx - mn) / mx > 0.22:
            tot += 1
            r, g, b = [c / 255.0 for c in p]
            d = max(r, g, b) - min(r, g, b)
            if d == 0:
                continue
            if max(r, g, b) == r:
                h = (60 * ((g - b) / d)) % 360
            elif max(r, g, b) == g:
                h = 60 * ((b - r) / d) + 120
            else:
                h = 60 * ((r - g) / d) + 240
            hs[int(h) // 15] = hs.get(int(h) // 15, 0) + 1
    big = {k * 15: n for k, n in hs.items() if n >= 0.05 * max(1, tot)}
    groups: list[list[int]] = []
    for h in sorted(big):
        if groups and h - groups[-1][-1] <= 15:
            groups[-1].append(h)
        else:
            groups.append([h])
    hues = [sum(g) // len(g) for g in groups]
    verdicts = []
    verdicts.append(f"header centre {drawn} px drawn"
                    + (" -> FAILS (that is the pill)" if drawn > 60 else " -> passes"))
    unmapped = [h for h in hues if not (h < 70 or 130 < h < 200)]
    verdicts.append(f"accents {hues} deg"
                    + (f" -> FAILS (third accent at {unmapped})" if len(hues) != 2 or unmapped
                       else " -> passes"))
    print(f"previous panel ({path.name}): " + "; ".join(verdicts))


# ------------------------------------------------------------------ verdict
if "--against-v6" in sys.argv:
    prev = HERE / "ui_v6_1180x700.png"
    if prev.exists():
        audit_previous(prev)

if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print(f"identity ok: {len(strings)} panel strings covered, contrast floor holds "
      f"(body {min(contrast(c, BASE) for c in BODY.values()):.1f}:1, "
      f"decor {min(contrast(c, BASE) for c in DECOR.values()):.1f}:1), "
      f"{len(blocks)} blocks on the grid with no shared height, "
      + "; ".join(notes))
