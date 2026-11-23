#!/usr/bin/env python3
"""check_readouts.py - the four readouts are on the glass, in the order the words are in.

Why this exists
---------------
The 1.2.46 window shipped with the scan strip drawn through `aeq::ui::draw`, which puts a
RIGHT-TO-LEFT MARK in front of every string that starts with a letter or a digit
(Source/ui/ArabicText.cpp).  In an RTL paragraph "SCAN 0%  ·  0 s" is two LTR runs and the
paragraph, not the string, decides the order: the empty window reads "s SCAN 0%  ·  0".

Nothing caught it.  `ui_shot` asserts the *boxes* do not overlap, the ledger asserts the
*sentences* are right, and both were telling the truth - the words were correct and the
drawing of them was not.  So this gate checks two things that catch exactly that class:

  A. the source: each of the four readouts is drawn through `trackedText` (the non-bidi
     funnel) and never through `aeq::ui::draw` / `arabic`, which prepend the RTL mark.
     This half runs anywhere, including a machine with no screenshot.
  B. the picture, when CI's shot is here: the caption row carries the strip's cyan, the
     header carries the badge, the sealed window carries the layer line in accent on the
     right of the stop row, and no ink sits left of the panel's 22 px margin.

    .venv/bin/python design/preview/check_readouts.py
"""
from __future__ import annotations

import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
EDITOR = ROOT / "plugin" / "Source" / "PluginEditor.cpp"
SHOTS = [ROOT / "dist" / "ui" / "ui-1240x800.png",
         ROOT / "dist" / "ui" / "ui-empty-1240x800.png"]

MARGIN = 22.0          # the design's left margin, in design px
DESIGN_W, DESIGN_H = 1180.0, 700.0
READOUTS = ("scanBadgeText()", "scanStripText()", "layerReadoutText()", "autoReadoutText()")


def source_checks() -> list[str]:
    src = EDITOR.read_text(encoding="utf-8", errors="replace")
    out = []
    for name in READOUTS:
        if f"trackedText (g, {name}" not in src:
            out.append(f"{name} is not drawn through trackedText - the readouts take the "
                       f"non-bidi funnel or their words come out in the wrong order")
        for bad in (f"aeq::ui::draw (g, {name}", f"arabic (g, {name}"):
            if bad in src:
                out.append(f"{name} is drawn through {bad.split('(')[0].strip()} - that "
                           f"prepends a RIGHT-TO-LEFT MARK and reorders the line")
    return out


def lum(px) -> float:
    r, g, b = px[0] / 255.0, px[1] / 255.0, px[2] / 255.0
    return 0.299 * r + 0.587 * g + 0.114 * b


def cyan(px) -> bool:
    r, g, b = px[0], px[1], px[2]
    return b > 120 and g > 110 and b >= r + 25 and g >= r + 10


def accent(px) -> bool:
    r, g, b = px[0], px[1], px[2]
    return r > 150 and b > 90 and r >= g + 60 and b >= g + 20


def ink_in(img, box, test) -> int:
    x0, y0, x1, y1 = [int(v) for v in box]
    return sum(1 for y in range(max(0, y0), min(img.height, y1))
               for x in range(max(0, x0), min(img.width, x1))
               if test(img.getpixel((x, y))))


def picture_checks() -> tuple[list[str], list[str]]:
    from PIL import Image

    problems, lines = [], []
    for shot in SHOTS:
        img = Image.open(shot).convert("RGB")
        sx, sy = img.width / DESIGN_W, img.height / DESIGN_H
        sealed = "empty" not in shot.name

        # A. nothing drawn off the left edge of the take caption's row (design 514..532)
        y0, y1 = int(506 * sy), int(524 * sy)
        margin_x = int((MARGIN - 2) * sx)
        stray = sum(1 for y in range(y0, y1) for x in range(0, margin_x)
                    if lum(img.getpixel((x, y))) > 0.55)
        if stray:
            problems.append(f"{shot.name}: {stray} px of ink left of the {MARGIN:.0f} px "
                            f"margin in the caption row - the line is drawn off the edge")

        # B. the strip is drawn, in cyan, inside the caption row
        strip_px = ink_in(img, (margin_x, y0, img.width * 0.45, y1), cyan)
        if strip_px < 60:
            problems.append(f"{shot.name}: {strip_px} px of the scan strip's cyan in the "
                            f"caption row (need 60) - the strip is not drawn")

        # C. the badge is on the header, between the wordmark and the reference pill
        badge = ink_in(img, (img.width * 0.26, int(14 * sy), img.width * 0.56, int(44 * sy)),
                       cyan)
        if badge < 120:
            problems.append(f"{shot.name}: {badge} px of cyan where the scan badge goes "
                            f"(need 120) - the badge is not on the header")

        # D. the layer line is above the take, on the right, once something is committed
        layer = 0
        if sealed:
            layer = ink_in(img, (img.width * 0.62, int(458 * sy), img.width, int(478 * sy)),
                           accent)
            if layer < 60:
                problems.append(f"{shot.name}: {layer} px of accent where the layer line goes "
                                f"(need 60) - static/dynamic is not written above the take")
        lines.append(f"{shot.name}: strip {strip_px} px cyan, badge {badge} px cyan"
                     + (f", layer line {layer} px accent" if sealed else ""))
    return problems, lines


def main() -> int:
    problems = source_checks()
    lines: list[str] = []
    missing = [p.name for p in SHOTS if not p.exists()]
    if missing:
        lines.append("no shot here (" + ", ".join(missing) + ") - source half only")
    else:
        shot_problems, lines = picture_checks()
        problems += shot_problems

    if problems:
        for p in problems:
            print("FAIL: " + p)
        return 1
    print("readouts ok: four readouts on the non-bidi funnel; " + "; ".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
