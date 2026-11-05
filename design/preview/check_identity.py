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

import ui_v7 as v7  # noqa: E402

problems: list[str] = []
notes: list[str] = []


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


def plate_base() -> tuple[int, int, int]:
    """The panel's own background, taken from the shipped plate (not from a guess)."""
    im = Image.open(v7.ASSETS / "panel_plate.png").convert("RGB")
    band = im.crop((40, 186, 1140, 206))  # above the well: bare plate
    px = list(band.get_flattened_data())
    px.sort(key=lambda p: p[0] * 0.2126 + p[1] * 0.7152 + p[2] * 0.0722)
    return px[len(px) // 2]


PANEL = v7.OUT
if not PANEL.exists():
    print(f"FAIL {PANEL.name} missing - run design/preview/ui_v7.py")
    sys.exit(1)
img = Image.open(PANEL).convert("RGB")
if img.size != (v7.W, v7.H):
    problems.append(f"render is {img.size}, expected {(v7.W, v7.H)}")

BASE = plate_base()

# ------------------------------------------------------------------ 1. the language
cmap = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-Regular.ttf")).getBestCmap()
cmap_b = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-SemiBold.ttf")).getBestCmap()
strings = list(v7.ARABIC_STRINGS)
missing = sorted({ch for s in strings for ch in s
                  if ord(ch) not in cmap and ord(ch) not in cmap_b})
if missing:
    problems.append("characters missing from the bundled font: " +
                    ", ".join(f"U+{ord(c):04X} {c!r}" for c in missing))

# ------------------------------------------------------------------ 2. the contrast
# body text >= 4.5:1; micro-caps and units are decorative and must clear 3:1.
BODY = {"TEXT": v7.TEXT, "DIM": (184, 174, 159), "MODESTATIC": v7.DIM,
        "PATINA": v7.PATINA, "BRASS_HOT": v7.BRASS_HOT}
DECOR = {"FAINT": v7.FAINT, "UNIT": (150, 140, 126), "BRASS": v7.BRASS}
for name, col in BODY.items():
    r = contrast(col, BASE)
    if r < 4.5:
        problems.append(f"{name} on the plate is {r:.2f}:1, under the 4.5:1 body floor")
for name, col in DECOR.items():
    r = contrast(col, BASE)
    if r < 3.0:
        problems.append(f"{name} on the plate is {r:.2f}:1, under the 3:1 decorative floor")

# ------------------------------------------------------------------ 3. the grid
blocks = {"header": v7.HEADER, "take": v7.TAKE, "well": v7.WELL,
          "ledger": v7.LEDGER, "footer": v7.FOOTER}


def hit(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


names = list(blocks)
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        if hit(blocks[names[i]], blocks[names[j]]):
            problems.append(f"{names[i]} overlaps {names[j]}")
for name, box in blocks.items():
    if box[0] < 0 or box[1] < 0 or box[2] > v7.W or box[3] > v7.H:
        problems.append(f"{name} leaves the panel: {box}")

# an equal card grid is exactly what "no two blocks share a height" rules out
heights = {n: b[3] - b[1] for n, b in blocks.items()}
for i, a in enumerate(names):
    for b in names[i + 1:]:
        if abs(heights[a] - heights[b]) <= 2:
            problems.append(f"{a} and {b} are both {heights[a]} px tall - that is a card grid")

# ------------------------------------------------------------------ 4. the story
# a take bar with a real seal somewhere in the middle, and a clamp sitting on it
if not 0.05 < v7.SEAL_T < 0.95:
    problems.append(f"the take is sealed at {v7.SEAL_T} - nothing was stopped, or nothing was heard")
bx0, by0, bx1, by1 = v7.TAKE_BAR
seal_x = int(bx1 - (bx1 - bx0) * v7.SEAL_T)
band = img.crop((seal_x - 26, by0 - 8, seal_x + 26, by1 + 8))
brass_px = sum(1 for p in band.get_flattened_data() if p[0] > 110 and p[0] - p[2] > 34 and p[1] > 70)
if brass_px < 300:
    problems.append(f"only {brass_px} brass px at the seal ({seal_x}) - the clamp is not on the bar")
# the take mass is the measurement, so it must read patina, not brass
mass = img.crop((seal_x + 70, by0 + 8, seal_x + 300, by1 - 8))
mpx = list(mass.get_flattened_data())
green = sum(1 for p in mpx if p[1] > p[0] + 8 and p[1] > 55)
if green < 0.5 * len(mpx):
    problems.append(f"the learned take is {100 * green / len(mpx):.0f}% patina - "
                    "the measurement and the stop are coloured the same")
else:
    notes.append(f"take mass {100 * green / len(mpx):.0f}% patina")

# the correction curve is the decision, so it must read brass
wx0, wy0, wx1, wy1 = v7.WELL
well = img.crop((wx0, wy0, wx1, wy1))
brass_curve = sum(1 for p in well.get_flattened_data()
                  if p[0] > 190 and p[0] - p[2] > 55 and p[1] > 140)
if brass_curve < 400:
    problems.append(f"{brass_curve} brass px in the well - the curve is not the brass layer")
else:
    notes.append(f"curve {brass_curve} brass px over the spectrum")

# the ledger has to hold all three after-commit behaviours
modes = {b[3] for b in v7.BANDS}
if modes != {0, 1, 2, 3}:
    problems.append(f"band modes present: {sorted(modes)} - static/dynamic/tracked must all exist")
n_static = sum(1 for b in v7.BANDS if b[3] in (0, 1))
notes.append(f"ledger {len(v7.BANDS)} bands ({n_static} static, 1 dynamic, 1 tracked)")

# ------------------------------------------------------------------ 5. not the store
# (a) the centred pill: nothing may be drawn in the middle of the header band
hx0, hy0, hx1, hy1 = v7.HEADER
mid = (v7.W // 2)
head = img.crop((mid - 60, hy0 + 4, mid + 60, hy1 - 4))
plate_ref = img.crop((mid - 60, hy0 + 4, mid + 60, hy1 - 4))
base_l = lum(BASE)
bright = sum(1 for p in head.get_flattened_data() if lum(p) > base_l + 0.08)
if bright > 60:
    problems.append(f"{bright} px drawn in the middle of the header - that is the reference's "
                    "centred pill")
else:
    notes.append(f"header centre empty ({bright} px over the plate)")

# (b) exactly two accents: cluster the saturated pixels by hue
sat = []
for p in img.resize((v7.W // 2, v7.H // 2)).get_flattened_data():
    mx, mn = max(p), min(p)
    if mx > 60 and (mx - mn) / mx > 0.22:
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
    want = sorted([40, 160])
    got = sorted(accents)
    if abs(got[0] - want[0]) > 22 or abs(got[1] - want[1]) > 26:
        problems.append(f"accents at {got} deg, expected brass ~40 and patina ~160")
    notes.append(f"two accents at {got[0]} and {got[1]} deg")

# (c) the watermark is a declaration, not a measurement: a 104 px watermark at alpha 10
# leaves no reliable signature, so this asserts the module never asks for one, and says so.
if v7.WATERMARK is not None:
    problems.append("a latin watermark is still being drawn behind the curve")
if v7.CENTRED_PILL or v7.EQUAL_CARD_GRID:
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
print(f"identity ok: {len(strings)} Arabic strings covered, contrast floor holds "
      f"(body {min(contrast(c, BASE) for c in BODY.values()):.1f}:1, "
      f"decor {min(contrast(c, BASE) for c in DECOR.values()):.1f}:1), "
      f"{len(blocks)} blocks on the grid with no shared height, "
      + "; ".join(notes))
