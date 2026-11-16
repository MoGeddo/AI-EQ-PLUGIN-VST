#!/usr/bin/env python3
"""
check_wordmark.py - the wordmark the window draws is the wordmark the render approved.

The header prints the product name as two words in two colours: "GEDDO" in the accent,
"AEQ" in the light ink, drawn as two `drawText` calls because they are two colours.  The
second call needs the width of the first *as drawn*, and a word drawn with 3.4 px of extra
kerning is wider than the same word measured without it: five glyphs, four gaps, 13.6 px
the measurement did not know about.  Against an 8 px gap the second word started 5.6 px
inside the first, and 1.2.37 shipped a window that read `GEDDOAEQ`.

Nothing caught it.  `check_match` compares blocks and edges and a wordmark is neither; the
name gate reads source, and the source was self-consistent - it named the wrong width in
exactly one place, so a source gate would have agreed with the bug.  The arithmetic now has
its own function and its own test (G24, in `build/aeq_tests`); this file is the picture.

How it is measured
------------------
Not by colour.  The two words are pink and lavender over a purple plate, and the lavender
sits at 0.2-0.3 saturation because it is white at 72% over that plate, so a saturation cut
does not separate them - it was tried, and it reported both words as saturated in both
images.  What is measured instead is the one number the broken code got wrong: the *width
of the wordmark's ink*.

The bright rows at the top of the panel are split into runs; the first run is the wordmark,
the second is the tagline.  Inside the first run, the ink's left and right edges give the
width.  The window is 1240x800 and the render is 1180x700, and the panel is laid out from
its height, so the render's width is scaled by `window_h / 700` before comparing.  Two
programs, two type stacks: the tolerance is 6 px on a span of ~140, and the defect is 10 px
out, so the gate has to sit between them rather than around them.

    python3 design/preview/check_wordmark.py                      # newest dist/ui shot
    python3 design/preview/check_wordmark.py --shot x.png
    python3 design/preview/check_wordmark.py --shot docs/ui-1.2.37-wordmark-overlap.png
                                                                  # must FAIL: 1.2.37's window
    python3 design/preview/check_wordmark.py --shot design/preview/ui_v9_neon_1180x700.png
                                                                  # the render against itself
"""
from __future__ import annotations

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
RENDER = os.path.join(HERE, "ui_v9_neon_1180x700.png")
DESIGN_H = 700
TOL = 6            # px of wordmark width agreement, scaled to the shot's height
BRIGHT = 0.70      # both words are the brightest ink on the plate
LEFT = 0.30        # the wordmark is the left block; the right column holds the seal pill


def newest_shot():
    shots = []
    for pat in ("ui-*-window*.png", "ui-*x*.png"):
        shots += glob.glob(os.path.join(ROOT, "dist", "ui", pat))
    shots = [s for s in shots if "probe" not in s and "curve" not in s and "report" not in s]
    return max(shots, key=os.path.getmtime) if shots else None


def wordmark(path):
    """(width, rows, x0, x1) of the wordmark's ink, found from the picture."""
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32) / 255.0
    h, w, _ = a.shape
    left = max(8, int(w * LEFT))
    top = max(24, int(h * 0.12))
    bright = a.max(2)[:top, :left] > BRIGHT

    runs, start = [], None
    for y in range(top):
        on = bright[y].sum() >= 3
        if on and start is None:
            start = y
        elif not on and start is not None:
            if y - start >= 4:
                runs.append((start, y))
            start = None
    if start is not None and top - start >= 4:
        runs.append((start, top))
    if not runs:
        return None

    y0, y1 = runs[0]
    cols = np.nonzero(bright[y0:y1].any(0))[0]
    if cols.size == 0:
        return None
    return (int(cols.max() - cols.min() + 1), (y0, y1), int(cols.min()), int(cols.max()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None, help="the screenshot (default: newest in dist/ui)")
    ap.add_argument("--render", default=RENDER)
    args = ap.parse_args()

    shot = args.shot or newest_shot()
    if not shot or not os.path.exists(shot):
        print("UNAVAILABLE no window screenshot: python3 scripts/fetch_ci_ui.py")
        return 2

    got = wordmark(shot)
    want = wordmark(args.render)
    if got is None or want is None:
        print(f"FAIL no wordmark found (shot {bool(got)}, render {bool(want)})")
        return 1

    sw, srows, sx0, sx1 = got
    rw, rrows, rx0, rx1 = want
    shot_h = Image.open(shot).size[1]
    scale = shot_h / float(DESIGN_H)
    expected = rw * scale
    diff = sw - expected
    rel = os.path.relpath(shot, ROOT)

    if abs(diff) > TOL:
        print(f"FAIL {rel}: wordmark ink is {sw} px wide, the render's {rw} px at "
              f"{DESIGN_H} is {expected:.1f} px at {shot_h} ({diff:+.1f} px apart, "
              f"tolerance {TOL}).  Rows {srows[0]}-{srows[1]}, x{sx0}-{sx1}.  A wordmark "
              f"that is short by the tracking is two words drawn as one.")
        return 1

    print(f"wordmark ok: {rel} {sw} px vs render {rw} px at {DESIGN_H} -> "
          f"{expected:.1f} px at {shot_h} ({diff:+.1f} px, tolerance {TOL}); "
          f"rows {srows[0]}-{srows[1]} vs {rrows[0]}-{rrows[1]}, "
          f"left edge x{sx0} vs {rx0:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
