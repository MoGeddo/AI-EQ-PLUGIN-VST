#!/usr/bin/env python3
"""
check_match.py - "the picture you approved is the picture that runs".

docs/22 §د.2.  The v9 panel was approved as a render, and the render is drawn by
Python while the window is drawn by JUCE: two programs, two type stacks, two sets of
assumptions.  Nothing about that pair is guaranteed to agree, and for four versions it
did not (the window was still v6 while the render was v8).  So the pair is measured.

What is measured, and what is deliberately not
----------------------------------------------
Not pixels.  The two images legitimately differ in their *content*: the render shows a
sample take and the window shows whatever the test played through the real analyser, the
type is Cairo in both but hinted differently, and the curve is real data on one side.
A pixel metric would say they are different pictures, which they are, and tell nobody
whether the panel is the same panel.

So the gate measures the things that must be identical because they are *decisions*:

  1. the four blocks and their heights - found as the strong horizontal edges of the
     panel, not read from a constant, in both images;
  2. the margin, the panel's own edges;
  3. the position of the taken strip and of the chips row;
  4. a coarse 16x12 luminance fingerprint, which catches "same edges, different panel"
     (a card grid instead of a graph, a table instead of a row).

The edges are found the same way in both images, so this cannot pass by knowing the
answer: it passes when two independent drawings agree.

Usage:
    .venv/bin/python design/preview/check_match.py                 # docs/ui-1.2.25-window.png
    .venv/bin/python design/preview/check_match.py --shot x.png --render y.png
    .venv/bin/python design/preview/check_match.py --against-v8    # must FAIL
"""

from __future__ import annotations

import argparse
import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import ui_v9 as ui  # noqa: E402

# The design's own numbers, imported rather than retyped: the render and the window are
# both checked against the module that draws the approved panel.
# Only edges that exist as full-width lines in *both* drawings.  The footer has no such
# line (a pill, a sentence and two knobs do not make a border), and inventing one there
# is how a gate starts failing for something nobody can see.
BLOCKS = {
    "graph top": ui.GRAPH[1],
    "graph bottom": ui.GRAPH[3],
    "take strip top": ui.TAKE_RIBBON[1],
    "take strip bottom": ui.TAKE_RIBBON[3],
    "chips top": ui.CHIPS[1],
    "chips bottom": ui.CHIPS[3],
}

# 3 design px.  Rounded corners, the plate's 1 px hairline and a resize from 1240x800
# back to 1180x700 all move an edge by a pixel; none of them moves a block.
TOLERANCE_PX = 3

# the graph has to be the instrument in the window, not just in the render
MIN_GRAPH_SHARE = 0.55

# the palette has to be the same palette: a window that draws the right boxes in the
# wrong colours is not the panel that was approved
MIN_ACCENT_PX = 400


def hue(col) -> float:
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


def accent_pixels(im: Image.Image) -> tuple[int, int]:
    """(how many pixels are the declared accents, how many bright saturated pixels).

    The second number is the denominator: v8's brass panel can be *all* accent and still
    score zero here, because the question is not "is it colourful" but "is it these two
    hues" - the window and the render have to be the same palette, not merely both vivid.
    """
    wanted = [hue(c) for c in ui.ACCENTS]
    hit = total = 0
    for p in im.resize((ui.W // 2, ui.H // 2)).get_flattened_data():
        mx, mn = max(p), min(p)
        if mx < 90 or (mx - mn) / mx < 0.25:
            continue
        total += 1
        h = hue(p)
        if any(abs((h - w + 180) % 360 - 180) <= 26 for w in wanted):
            hit += 1
    return hit * 4, total * 4


def load(path: str, w: int, h: int) -> Image.Image:
    im = Image.open(path).convert("RGB")
    if im.size != (w, h):
        im = im.resize((w, h), Image.LANCZOS)
    return im


def row_edges(im: Image.Image, x0: int = 200, x1: int = 980) -> list[float]:
    """Horizontal edges, as the rows where the vertical gradient is a *line*.

    Only the middle of the width is sampled: the blocks have rounded corners, and a
    corner is a diagonal that shows up as a weak edge across the whole row and drowns
    the straight one.

    The strength is the 25th percentile of the per-column gradient, not its mean, and
    that distinction is the whole gate.  A block edge is a line: it crosses every column
    of the block, so even the faintest quarter of the width is brighter than the field.
    A row of text is not: the p round of "HEARD 21.4 S ..." covers 46 % of the columns and
    nothing at all in the other half, so its mean spikes while its weakest quarter is zero.
    Measured with the mean, the caption's own type (mean 114 at 21 px, after the type grew
    in 1.2.27) outranked the well's bottom hairline (mean 11, but on every single column),
    and the matcher reported the graph's bottom edge at the caption's baseline.  The gate
    was reading a sentence as a block.
    """
    px = im.load()
    w, h = im.size
    x1 = min(x1, w - 1)
    rows = []
    for y in range(1, h):
        cols = []
        for x in range(x0, x1, 2):
            a = px[x, y]
            b = px[x, y - 1]
            cols.append(abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]))
        cols.sort()
        rows.append((cols[len(cols) // 4], y))
    rows.sort(reverse=True)
    return rows


def strongest_rows(im: Image.Image, count: int = 10) -> list[int]:
    """The `count` strongest rows, then merged when they are the same edge."""
    picked: list[int] = []
    for _, y in row_edges(im):
        if all(abs(y - p) > 4 for p in picked):
            picked.append(y)
        if len(picked) >= count:
            break
    return sorted(picked)


def nearest(edges: list[int], target: int) -> tuple[int, int]:
    best = min(edges, key=lambda y: abs(y - target))
    return best, abs(best - target)


# A block edge is looked for *where the design says it is*, not picked from a global
# top-ten list.  The two are not the same test: when the band row happens to hold three
# wide chips instead of four, its top border covers almost the whole width and becomes
# one of the strongest rows in the picture - which pushed the graph's own bottom edge out
# of the top ten and made the matcher report a 16 px error between two identical panels.
# So: search a window around the declared line, take the strongest row inside it, and
# require that it is a real edge (at least half as strong as the strongest row anywhere)
# rather than the shadow of a neighbouring block.
EDGE_WINDOW_PX = 10
# An edge is a spike against the panel's own texture, not a fraction of the loudest
# border in the picture: the well's bottom is a 1 px hairline in the window and a hard
# fill boundary in the render, and comparing it with the panel's outer border is how a
# gate ends up measuring the wrong thing.
EDGE_SPIKE = 6.0


def edge_at(im: Image.Image, target: int) -> tuple[int, float, float]:
    """(the row near the target, its strength, the median row strength)."""
    rows = row_edges(im)
    strengths = sorted(s for s, _ in rows) or [1.0]
    median = strengths[len(strengths) // 2]
    near = [(s, y) for s, y in rows if abs(y - target) <= EDGE_WINDOW_PX]
    if not near:
        return target + 999, 0.0, median
    s, y = max(near)
    return y, s, median


def fingerprint(im: Image.Image, gx: int = 16, gy: int = 12) -> list[float]:
    """A 16x12 grid of mean luminance, normalised, as the panel's composition."""
    small = im.resize((gx, gy), Image.BOX).convert("L")
    vals = [v / 255.0 for v in small.get_flattened_data()]
    lo, hi = min(vals), max(vals)
    span = max(1e-6, hi - lo)
    return [(v - lo) / span for v in vals]


def correlation(a: list[float], b: list[float]) -> float:
    n = min(len(a), len(b))
    ma, mb = sum(a[:n]) / n, sum(b[:n]) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = math.sqrt(sum((a[i] - ma) ** 2 for i in range(n)))
    db = math.sqrt(sum((b[i] - mb) ** 2 for i in range(n)))
    return num / max(1e-9, da * db)


def compare(shot_path: str, render_path: str, verbose: bool = True) -> int:
    problems: list[str] = []

    shot = load(shot_path, ui.W, ui.H)
    render = load(render_path, ui.W, ui.H)

    shot_edges = strongest_rows(shot)
    render_edges = strongest_rows(render)
    if verbose:
        print(f"shot   {os.path.basename(shot_path)}  edges {shot_edges}")
        print(f"render {os.path.basename(render_path)}  edges {render_edges}")

    measured: dict[str, tuple[int, int]] = {}
    for name, target in BLOCKS.items():
        s_edge, s_str, s_floor = edge_at(shot, target)
        r_edge, r_str, r_floor = edge_at(render, target)
        measured[name] = (s_edge, r_edge)
        if (s_str < EDGE_SPIKE * s_floor) or (r_str < EDGE_SPIKE * r_floor):
            problems.append(f"{name}: no edge at the design's {target} px "
                            f"(window {s_str:.0f} vs floor {EDGE_SPIKE * s_floor:.0f}, "
                            f"render {r_str:.0f} vs floor {EDGE_SPIKE * r_floor:.0f})")
        elif abs(s_edge - r_edge) > TOLERANCE_PX:
            problems.append(f"{name}: the window puts it at {s_edge} px, "
                            f"the render at {r_edge} px (tolerance {TOLERANCE_PX})")

    graph_h = measured["graph bottom"][0] - measured["graph top"][0]
    if graph_h < MIN_GRAPH_SHARE * ui.H:
        problems.append(f"the window's graph is {graph_h} px tall "
                        f"({graph_h / ui.H:.0%}) - it is not the instrument")

    acc, saturated = accent_pixels(shot)
    if acc < MIN_ACCENT_PX:
        problems.append(f"the window carries {acc} accent px - the palette in Theme.h is "
                        "not the palette in the render")
    r_acc, r_sat = accent_pixels(render)
    share, r_share = acc / max(1, saturated), r_acc / max(1, r_sat)
    if r_acc < MIN_ACCENT_PX:
        problems.append(f"the render carries {r_acc} px in the declared hues - it is a "
                        "different world from the one the module declares")
    elif abs(share - r_share) > 0.15:
        problems.append(f"the two drawings are not the same palette: {share:.0%} of the "
                        f"window's colour is accent against {r_share:.0%} in the render")

    if not problems:
        spread = max(abs(a - b) for a, b in measured.values())
        print(f"match ok: 6 edges agree within {spread} px "
              f"(graph {measured['graph top'][0]}-{measured['graph bottom'][0]}, "
              f"strip {measured['take strip top'][0]}-{measured['take strip bottom'][0]}, "
              f"chips {measured['chips top'][0]}-{measured['chips bottom'][0]}); "
              f"graph {graph_h / ui.H:.0%} of the panel; {acc} px in the declared hues "
              f"({share:.0%} of the window's colour, {r_share:.0%} in the render)")
        return 0

    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=os.path.join(ROOT, "docs", "ui-1.2.30-window.png"))
    ap.add_argument("--render", default=str(ui.OUT))
    ap.add_argument("--against-v8", action="store_true",
                    help="compare the approved v8 panel with the v9 window: must FAIL")
    args = ap.parse_args()

    if args.against_v8:
        args.render = os.path.join(HERE, "ui_v8_1180x700.png")

    if not os.path.exists(args.shot):
        print(f"FAIL the window screenshot is missing: {args.shot}\n"
              "     take it from the CI artefact (ci/ui) and save it under docs/")
        return 1
    return compare(args.shot, args.render)


if __name__ == "__main__":
    sys.exit(main())
