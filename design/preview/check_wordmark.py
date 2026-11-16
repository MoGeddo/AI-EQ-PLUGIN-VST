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

BRIGHT = 0.70      # both words are the brightest ink on the plate
WARM = 0.05        # accent word: red over blue (brass/pink)
COOL = -0.01       # ink word: white at 72% over the purple plate, red under blue
MIN_GAP = 4        # px at 1240x800: a space, not the sidebearing of a glyph
MIN_RATIO = 0.45   # ink word / accent word width.  "AEQ" against "GEDDO" is 0.54 in this
                   # face; if the ink word's first glyph is buried inside the accent word -
                   # the 1.2.37 defect - the visible part measures 0.32
LEFT = 0.30        # the wordmark is the left block; the right column holds the seal pill


def newest_shot():
    shots = []
    for pat in ("ui-*-window*.png", "ui-*x*.png"):
        shots += glob.glob(os.path.join(ROOT, "dist", "ui", pat))
    shots = [s for s in shots if "probe" not in s and "curve" not in s and "report" not in s]
    return max(shots, key=os.path.getmtime) if shots else None


def wordmark(path):
    """(warm span, cool span, rows) of the header wordmark, found from the picture.

    The two words are not separable by saturation - the ink word is white at 72% over a
    purple plate, so it lands at 0.2-0.3, the same band as the accent.  They ARE separable
    by the sign of red-minus-blue: +0.09..+0.14 for the accent word, -0.02..-0.04 for the
    ink word, on both renderers.  That is the only thing the two words disagree about.
    """
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32) / 255.0
    h, w, _ = a.shape
    left = max(8, int(w * LEFT))
    top = max(24, int(h * 0.12))
    mx = a.max(2)[:top, :left]
    rb = (a[..., 0] - a[..., 2])[:top, :left]
    bright = mx > BRIGHT

    # The accent word's own rows are the band, found from the accent word itself: since
    # 1.2.42 the header also carries his logo to the left of the words and a tagline under
    # them, and a band taken from "the first bright run" would swallow both.  The accent
    # word is the only warm ink in that corner; the logo is white and the tagline is dim.
    warm_all = bright & (rb > WARM)
    rows = np.nonzero (warm_all.sum(1) >= 3)[0]
    if rows.size == 0:
        return None
    y0, y1 = int(rows.min()), int(rows.max()) + 1

    warm = (bright & (rb > WARM))[y0:y1]
    cool = (bright & (rb < COOL))[y0:y1]
    wc = np.nonzero(warm.any(0))[0]
    cc = np.nonzero(cool.any(0))[0]
    if wc.size == 0 or cc.size == 0:
        return None
    after = cc[cc > wc.max()]                 # the ink word comes after the accent word
    if after.size == 0:
        return None
    return (int(wc.min()), int(wc.max())), (int(after.min()), int(after.max())), (y0, y1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None, help="screenshot (default: newest in dist/ui)")
    ap.add_argument("--render", default=None,
                    help="also measure this picture (the approved render) and print it")
    args = ap.parse_args()

    shot = args.shot or newest_shot()
    if not shot or not os.path.exists(shot):
        print("UNAVAILABLE no window screenshot: python3 scripts/fetch_ci_ui.py")
        return 2

    if args.render:
        r = wordmark(args.render)
        if r:
            (a0, a1), (n0, n1), _ = r
            print(f"render {os.path.relpath(args.render, ROOT)}: gap {n0 - a1 - 1} px")

    found = wordmark(shot)
    if found is None:
        print(f"FAIL {os.path.basename(shot)}: no wordmark found in the header band")
        return 1
    (a0, a1), (n0, n1), (y0, y1) = found
    gap = n0 - a1 - 1
    rel = os.path.relpath(shot, ROOT)

    accent_w, ink_w = a1 - a0 + 1, n1 - n0 + 1
    ratio = ink_w / float(accent_w)

    if gap < MIN_GAP:
        print(f"FAIL {rel}: the two words are {gap} px apart (need {MIN_GAP}) - "
              f"accent x{a0}-{a1}, ink x{n0}-{n1}, rows {y0}-{y1}.  "
              f"The second word was placed from a width measured without its tracking, "
              f"so the tracking ate the space between them.")
        return 1
    if ratio < MIN_RATIO:
        print(f"FAIL {rel}: the ink word measures {ink_w} px against the accent word's "
              f"{accent_w} (ratio {ratio:.2f}, need {MIN_RATIO}) - the first glyph is not "
              f"outside the accent word, it is drawn inside it.  Rows {y0}-{y1}.")
        return 1

    print(f"wordmark ok: {rel} two words, {gap} px apart (min {MIN_GAP}), ink/accent "
          f"width {ratio:.2f} (min {MIN_RATIO}) - accent x{a0}-{a1} ({accent_w} px), "
          f"ink x{n0}-{n1} ({ink_w} px), rows {y0}-{y1}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
