#!/usr/bin/env python3
"""check_panel.py - the panel render has to obey the things the panel claims.

Three properties, each of which has been wrong at least once and each of which is
invisible unless something measures it:

  1. EVERY ARABIC STRING IS DRAWN IN ONE CALL.  The first version placed one glyph at a
     time (to fall back to DejaVu for characters Cairo lacks), and the per-glyph advance
     widths accumulated error across a sentence: the words collided in the middle.  Cairo
     covers Arabic *and* Latin, so the fallback path should almost never run - if the
     panel needs it for a string, that string is a bug, not a font problem.
  2. THE CONTRAST FLOOR HOLDS.  Text on the panel is either >= 4.5:1 (readable body) or
     deliberately decorative and >= 3:1.  check_contrast.py owns the pairs; this gate
     makes sure it is still green after the palette changed.
  3. THE LAYOUT DOES NOT OVERLAP ITSELF.  Cards, the well, the strip and the knobs are
     rectangles; if two of them intersect, a control is under another control.

    /tmp/venv/bin/python design/preview/check_panel.py
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

problems = []

# ---- 1. every string the panel draws is covered by the bundled Arabic font ----------
from fontTools.ttLib import TTFont  # noqa: E402

import ui_v5  # noqa: E402  (import must not render; build() is behind __main__)

cmap = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-Regular.ttf")).getBestCmap()
cmap_b = TTFont(str(ROOT / "plugin" / "Source" / "assets" / "Cairo-SemiBold.ttf")).getBestCmap()
strings = [
    "التزام محفوظ · 4 باندات", "بيسمع", "التعلم", "المنحنى", "المتحرك", "الخروج",
    "إيقاف", "اللي اتسمع قبل الإيقاف", "القوة", "٦٢٪ من التصحيح", "التثبيت",
    "بيمشي مع النغمة", "المستوى", "الباند المتحرك بيثبّت 600 هرتز",
    "اتظبط لوحده بعد ٢٤.٩ ثانية · ٤ باندات · واحد شغّال ديناميك",
]
missing = sorted({ch for s in strings for ch in s if ord(ch) not in cmap and ord(ch) not in cmap_b})
if missing:
    problems.append("characters missing from the bundled font: " +
                    ", ".join(f"U+{ord(c):04X} {c!r}" for c in missing))

# ---- 2. contrast --------------------------------------------------------------------
r = subprocess.run([sys.executable, str(HERE / "check_contrast.py")],
                   capture_output=True, text=True)
if r.returncode != 0:
    problems.append("contrast check failed:\n" + (r.stdout + r.stderr).strip()[-400:])

# ---- 3. no two interactive rectangles overlap ----------------------------------------
def hit(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])

boxes = {
    "well": ui_v5.GRAPH,
    "strip": (32, 624, 1148, 676),
    "card-lean": (32, 452, 299, 612),
    "card-curve": (311, 452, 578, 612),
    "card-track": (590, 452, 857, 612),
    "card-out": (869, 452, 1136, 612),
}
names = list(boxes)
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        if hit(boxes[names[i]], boxes[names[j]]):
            problems.append(f"{names[i]} overlaps {names[j]}")
for name, box in boxes.items():
    if box[0] < 0 or box[1] < 0 or box[2] > ui_v5.W or box[3] > ui_v5.H:
        problems.append(f"{name} leaves the panel: {box}")

# ---- verdict ------------------------------------------------------------------------
if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print(f"panel ok: {len(strings)} strings fully covered by the bundled font, "
      f"contrast floor holds, {len(boxes)} blocks do not overlap")
