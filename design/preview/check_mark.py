#!/usr/bin/env python3
"""
check_mark.py - his logo is drawn on the window, and it is drawn bright.

The first version of `Source/ui/Logo.h` left the tint to whoever called it and drew the
picture with `fillAlphaChannelWithCurrentBrush = true`.  That flag takes the colour from the
brush that is current on the Graphics, so the mark came out in whatever dim colour the header
had used last: 119 of its pixels above 0.5 luminance, none above 0.85, and a calligraphic
logo at 30% opacity is a smudge.  It compiled, it rendered, and nothing said a word.

So this measures the picture.  In CI's screenshot of the real editor the mark sits to the
left of the wordmark, between the panel's left edge and the accent word.  The ink there must
be as bright as the type next to it: at least 150 px above 0.85 luminance at 1240x800 (the
mark is ~266x200 and its strokes are thin, so a fifth of its area at full brightness is the
honest bar), scaled by the shot's area.

    python3 design/preview/check_mark.py               # newest shot in dist/ui
    python3 design/preview/check_mark.py --shot x.png
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
REF_W, REF_H = 1240, 800
MIN_BRIGHT = 150          # px above BRIGHT at 1240x800, scaled by area
BRIGHT = 0.85


def newest_shot():
    shots = []
    for pat in ("ui-*-window*.png", "ui-*x*.png"):
        shots += glob.glob(os.path.join(ROOT, "dist", "ui", pat))
    shots = [s for s in shots if "probe" not in s and "curve" not in s and "report" not in s]
    return max(shots, key=os.path.getmtime) if shots else None


def mark_region(path):
    """The mark: left of the first saturated (accent) ink in the header."""
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32) / 255.0
    h, w, _ = a.shape
    top = max(24, int(h * 0.12))
    mx = a.max(2)[:top]
    mn = a.min(2)[:top]
    sat = np.where(mx > 1e-6, (mx - mn) / np.maximum(mx, 1e-6), 0.0)
    accent = (sat > 0.22) & (mx > 0.70)
    cols = np.nonzero(accent.any(0))[0]
    if cols.size == 0:
        return None, None
    x1 = int(cols.min()) - 4                     # a small gap before the words
    region = a[:top, 4:max(x1, 8)]
    lum = 0.2126 * region[..., 0] + 0.7152 * region[..., 1] + 0.0722 * region[..., 2]
    return lum, w * h / float(REF_W * REF_H)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    args = ap.parse_args()

    shot = args.shot or newest_shot()
    if not shot or not os.path.exists(shot):
        print("UNAVAILABLE no window screenshot: python3 scripts/fetch_ci_ui.py")
        return 2

    lum, scale = mark_region(shot)
    if lum is None:
        print(f"FAIL {os.path.basename(shot)}: no accent word found, so the mark's box is "
              f"unknown - the header did not draw its wordmark")
        return 1

    bright = int((lum > BRIGHT).sum()) / max(scale, 1e-6)
    need = MIN_BRIGHT
    rel = os.path.relpath(shot, ROOT)

    if bright < need:
        print(f"FAIL {rel}: the mark has {bright:.0f} px above {BRIGHT} luminance "
              f"(need {need} at {REF_W}x{REF_H}); peak {lum.max():.2f}.  A logo drawn in a "
              f"dim brush is a smudge - `drawImage(..., fillAlphaChannelWithCurrentBrush)` "
              f"takes the colour from the Graphics, so it has to be set there.")
        return 1

    print(f"mark ok: {rel} his logo is on the window - {bright:.0f} px above {BRIGHT} "
          f"luminance (need {need}), peak {lum.max():.2f}, across "
          f"{lum.shape[1]}x{lum.shape[0]} px left of the wordmark")
    return 0


if __name__ == "__main__":
    sys.exit(main())
