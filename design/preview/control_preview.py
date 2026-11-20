#!/usr/bin/env python3
"""control_preview.py - the switch shapes, drawn locally from the shipped numbers.

Same reason as knob_preview.py: the only place the real panel can be photographed is Windows
CI, and 1.2.64's rocker shipped as a left-pointing triangle because the quad was filled with a
gradient, then 1.2.65's paddle shipped as a small "≡" because its padding was derived from a
34 px box.  Both were visible at 1:1 in two minutes here.

Geometry comes from `Source/ui/Controls.h` (parsed, so the preview cannot drift), colours from
`Source/ui/Theme.h`.  What it cannot show is JUCE's own anti-aliasing; that is the trade, and
it is a good one for "is this part too small / in the wrong place".

    .venv/bin/python design/preview/control_preview.py [--out docs/preview-controls.png]
"""
from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "Source" / "ui"
SS = 4


def theme(name: str, fallback):
    src = (SRC / "Theme.h").read_text(encoding="utf-8")
    m = re.search(rf"{name}\(\)\s*\{{ return juce::Colour::fromRGB \(0x([0-9a-f]{{2}}), "
                  rf"0x([0-9a-f]{{2}}), 0x([0-9a-f]{{2}})\); \}}", src)
    return tuple(int(g, 16) for g in m.groups()) if m else fallback


def spec_numbers(filename: str, _struct: str) -> dict[str, float]:
    """Every `m.x = box * 0.5f` in the file, plus the integer fields.  The three constructors in
    Controls.h use distinct names (plate/pad/tilt/lampD/lampGap, padX/headerH/..., insetX/...),
    so one pass over the file is unambiguous where a block scan kept missing `return m;`."""
    src = (SRC / filename).read_text(encoding="utf-8")
    out: dict[str, float] = {}
    for name, num in re.findall(r"[mc]\.(\w+)\s*=\s*box \* ([0-9.]+)f", src):
        out[name] = float(num)
    for name, num in re.findall(r"[mc]\.(\w+)\s*=\s*(\d+);", src):
        out[name] = float(num)
    return out


def rounded(d, box, r, fill=None, outline=None, width=1):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def rocker(box: float, on: bool) -> Image.Image:
    k = spec_numbers("Controls.h", "constexpr RockerMetrics rockerMetrics")
    n = int(box * SS)
    img = Image.new("RGB", (n, n), theme("bg", (15, 18, 24)))
    d = ImageDraw.Draw(img, "RGBA")
    c = n / 2
    plate = k["plate"] * n
    rounded(d, [c - plate / 2, c - plate / 2, c + plate / 2, c + plate / 2], 0.26 * n,
            fill=theme("surface", (23, 27, 34)) + (255,), outline=(0, 0, 0, 120))
    pad = k["pad"] * n
    ph = k["paddleH"] * n
    pw = ph * 1.84
    travel = k["tilt"] * n * (1 if on else -1)
    stack = ph + k["lampGap"] * n + k["lampD"] * n
    top = c - stack / 2
    y0 = top + travel
    rounded(d, [c - pw / 2, y0, c + pw / 2, y0 + ph], 0.09 * n,
            fill=(255, 255, 255, 6) if not on else (255, 255, 255, 40),
            outline=(0, 0, 0, 150))
    for i in range(1, 4):
        y = y0 + ph * i / 4
        d.line([c - pw / 2 + 0.06 * n, y, c + pw / 2 - 0.06 * n, y], fill=(0, 0, 0, 90))
        d.line([c - pw / 2 + 0.06 * n, y + 1, c + pw / 2 - 0.06 * n, y + 1],
               fill=(255, 255, 255, 18))
    lit_y = (y0 + ph - 1) if not on else (y0 + 1)
    d.line([c - pw / 2 + 0.06 * n, lit_y, c + pw / 2 - 0.06 * n, lit_y],
           fill=(0, 0, 0, 110) if not on else (255, 255, 255, 50))
    ld = k["lampD"] * n
    ly = top + ph + k["lampGap"] * n + ld / 2
    lamp = theme("warn", (255, 194, 74))
    d.ellipse([c - ld / 2, ly - ld / 2, c + ld / 2, ly + ld / 2],
              fill=tuple(lamp) if on else tuple(theme("hairline", (57, 64, 77))))
    return img.resize((n // SS, n // SS), Image.LANCZOS)


def button(box: float) -> Image.Image:
    n = int(box * SS)
    img = Image.new("RGB", (n, n), theme("bg", (15, 18, 24)))
    d = ImageDraw.Draw(img, "RGBA")
    c = n / 2
    plate = 0.5 * n
    rounded(d, [c - plate / 2, c - plate / 2, c + plate / 2, c + plate / 2], 0.26 * n,
            fill=theme("surface", (23, 27, 34)) + (255,), outline=(0, 0, 0, 120))
    ring = 0.30 * n
    d.ellipse([c - ring, c - ring, c + ring, c + ring], fill=(0, 0, 0, 150),
              outline=(255, 255, 255, 36), width=2)
    face = ring * 0.80
    d.ellipse([c - face, c - face, c + face, c + face],
              fill=tuple(int(v * 1.25) if v < 200 else v
                         for v in theme("surfaceHi", (31, 36, 45))) + (255,))
    # the reset arrow: an arc with a tangential head
    rr = ring * 0.55
    a0 = 54
    d.arc([c - rr, c - rr, c + rr, c + rr], a0, a0 + 292, fill=(255, 255, 255, 220), width=4)
    # the tangential arrowhead on the arc's end, as Icon::Reset draws it
    ax = c + math.sin(math.radians(a0)) * rr
    ay = c - math.cos(math.radians(a0)) * rr
    tx, ty = math.cos(math.radians(a0)), math.sin(math.radians(a0))
    nh = 0.155 * n
    d.polygon([(ax + tx * nh * 1.25, ay + ty * nh * 1.25),
               (ax - tx * nh * 0.30 - ty * nh, ay - ty * nh * 0.30 + tx * nh),
               (ax - tx * nh * 0.30 + ty * nh, ay - ty * nh * 0.30 - tx * nh)],
              fill=(255, 255, 255, 235))
    return img.resize((n // SS, n // SS), Image.LANCZOS)


def card(w: float, h: float) -> Image.Image:
    n_w, n_h = int(w * SS), int(h * SS)
    img = Image.new("RGB", (n_w, n_h), theme("bg", (15, 18, 24)))
    d = ImageDraw.Draw(img, "RGBA")
    rounded(d, [0, 0, n_w - 1, n_h - 1], 0.18 * n_h,
            fill=tuple(max(0, v - 2) for v in theme("surface", (23, 27, 34))) + (255,),
            outline=(255, 255, 255, 26))
    cm = spec_numbers("Controls.h", "constexpr CardMetrics cardMetrics")
    y = cm["headerH"] * n_w
    inset = cm["ruleInset"] * n_w
    d.line([inset, y, n_w - inset, y], fill=(255, 255, 255, 18))
    d.line([inset, y + 1, n_w - inset, y + 1], fill=(0, 0, 0, 90))
    img = img.resize((n_w // SS, n_h // SS), Image.LANCZOS)
    dd = ImageDraw.Draw(img)
    dd.text((10, 6), "01", fill=(150, 155, 165))
    dd.text((w - 60, 6), "STATIC", fill=(200, 205, 212))
    return img


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "docs" / "preview-controls.png"))
    a = ap.parse_args()

    box = 34
    off, on = rocker(box, False), rocker(box, True)
    btn = button(box)
    chip = card(300, 78)

    z = 4
    tiles = [(off, "bypass OFF"), (on, "bypass ON"), (btn, "reset")]
    zoom = [(t.resize((t.width * z, t.height * z), Image.NEAREST), lab + " 4x")
            for t, lab in tiles]
    pad, gap = 14, 12
    w = pad * 2 + sum(t.width for t, _ in zoom) + gap * 2 + chip.width
    h = pad * 2 + max(max(t.height for t, _ in zoom), chip.height) + 20
    sheet = Image.new("RGB", (w, h), (10, 11, 14))
    x = pad
    for t, lab in zoom:
        sheet.paste(t, (x, pad))
        ImageDraw.Draw(sheet).text((x, h - 16), lab, fill=(150, 155, 165))
        x += t.width + gap
    sheet.paste(chip, (x, pad))
    ImageDraw.Draw(sheet).text((x, h - 16), "band card", fill=(150, 155, 165))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(a.out)
    print(a.out, sheet.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
