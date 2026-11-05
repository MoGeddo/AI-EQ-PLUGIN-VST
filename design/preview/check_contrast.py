#!/usr/bin/env python3
"""check_contrast.py - the panel's palette has to be readable, and that is measurable.

From the craft floor of the design review (docs/18): body text >= 4.5:1 against its
own background, large text >= 3:1.  The first render of the v4 panel failed this:
the caption colour (150,144,136) is 3.4:1 on the panel and the footer note was
2.9:1, i.e. legible on a good monitor and mush on a laptop in a bright room.

This gate reads the palette out of design/preview/ui_v4.py so it cannot drift from
the picture, checks every pair we actually draw, and exits non-zero if any pair
fails.  It is deliberately dull: no screenshots, no taste, just arithmetic.

    python3 design/preview/check_contrast.py
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "design" / "preview" / "ui_v4.py"

# pairs that are actually drawn: (foreground name, background name, minimum ratio)
PAIRS = [
    ("TEXT", "BASE", 4.5),
    ("TEXT", "TOP", 4.5),
    ("DIM", "BASE", 4.5),
    ("DIM", "TOP", 4.5),
    ("FAINT", "BASE", 3.0),     # decorative only: axis rulers, version stamp
    ("AMBER", "BASE", 4.5),
    ("AMBER_HOT", "BASE", 4.5),
]


def parse_palette(text: str) -> dict[str, tuple[int, int, int]]:
    out = {}
    for m in re.finditer(r"^([A-Z_]+)\s*=\s*\((\d+),\s*(\d+),\s*(\d+)\)", text, re.M):
        out[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    return out


def luminance(rgb):
    def channel(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(v) for v in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(fg, bg):
    a, b = luminance(fg), luminance(bg)
    hi, lo = max(a, b), min(a, b)
    return (hi + 0.05) / (lo + 0.05)


def main() -> int:
    pal = parse_palette(SRC.read_text(encoding="utf-8"))
    missing = [n for pair in PAIRS for n in pair[:2] if n not in pal]
    if missing:
        print(f"palette names not found in {SRC.name}: {sorted(set(missing))}")
        return 2
    print(f"palette: {len(pal)} colours from {SRC.name}\n")
    worst, fails = 99.0, 0
    for fg, bg, need in PAIRS:
        ratio = contrast(pal[fg], pal[bg])
        worst = min(worst, ratio)
        ok = ratio >= need
        fails += 0 if ok else 1
        print(f"  {'ok  ' if ok else 'FAIL'}   {fg:9s} on {bg:5s}  {ratio:5.2f}:1  "
              f"(need {need:.1f})")
    print(f"\n===== worst pair {worst:.2f}:1, {fails} failing =====\n")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
