#!/usr/bin/env python3
"""knob_preview.py - draw the dial's geometry before spending a CI run on it.

The real dial is C++ inside `drawKnob()`, and the only place it can be photographed is
Windows CI (~7 minutes a shot).  Re-deciding knurl contrast or ring weight at that price is
how a design pass turns into four releases of guesswork - which is exactly what 1.2.56 to
1.2.61 was.

So this draws the same part, at 4x, from the *same numbers*: every radius and stroke is read
out of `plugin/Source/ui/KnobSpec.h`, so the preview cannot drift from the shipped geometry.
It is a preview of the geometry, not of the paint: the gradients and the font are PIL's.
What it is good for is exactly the questions a 7-minute loop cannot answer quickly - is the
knurling visible at 54 px, is the ring a value or a smudge, is the pointer a pointer.

    .venv/bin/python design/preview/knob_preview.py [--box 54] [--value 0.62]
"""
from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "plugin" / "Source" / "ui" / "KnobSpec.h"

# the palette, read out of Theme.h so the preview is not a second palette
THEME = ROOT / "plugin" / "Source" / "ui" / "Theme.h"
SS = 4  # supersampling


def spec_numbers() -> dict[str, float]:
    """`m.body = box * 0.312f;` -> {"body": 0.312}, plus the non-scaled integers."""
    src = SPEC.read_text(encoding="utf-8")
    out: dict[str, float] = {}
    for name, num in re.findall(r"m\.(\w+)\s*=\s*box \* ([0-9.]+)f", src):
        out[name] = float(num)
    # `m.knurlIn = m.body * 1.00f;` - a radius expressed against another radius
    for name, ref, mult in re.findall(r"m\.(\w+)\s*=\s*m\.(\w+) \* ([0-9.]+)f", src):
        out.setdefault(name, (ref, float(mult)))
    for name, num in re.findall(r"m\.(\w+)\s*=\s*(\d+);", src):
        out[name] = float(num)
    m = re.search(r"kKnobTicksMinBox\s*=\s*([0-9.]+)f", src)
    out["ticksMinBox"] = float(m.group(1))
    # resolve the chained radii
    for name, v in list(out.items()):
        if isinstance(v, tuple):
            ref, mult = v
            base = out[ref]
            out[name] = (base if isinstance(base, float) else 0.0) * mult
    return out


def colour(name: str, fallback: tuple[int, int, int]) -> tuple[int, int, int]:
    src = THEME.read_text(encoding="utf-8")
    m = re.search(rf"{name}\(\)\s*\{{ return juce::Colour::fromRGB \(0x([0-9a-f]{{2}}), "
                  rf"0x([0-9a-f]{{2}}), 0x([0-9a-f]{{2}})\); \}}", src)
    if not m:
        return fallback
    return tuple(int(g, 16) for g in m.groups())  # type: ignore[return-value]


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def draw(box: float, value: float, out: Path) -> None:
    k = spec_numbers()
    n = int(round(box * SS))
    size = n + int(0.10 * n)          # a little air so the seat's edge is visible
    img = Image.new("RGB", (size, size), colour("bg", (15, 18, 24)))
    d = ImageDraw.Draw(img, "RGBA")
    c = size / 2.0
    accent = colour("accent", (236, 79, 2))
    hot = colour("accentHi", (255, 138, 77))
    surface = colour("surface", (23, 27, 34))
    surface_hi = colour("surfaceHi", (31, 36, 45))
    hair = colour("hairline", (57, 64, 77))
    muted = colour("muted", (191, 195, 203))

    def circle(r, fill=None, outline=None, width=1):
        d.ellipse([c - r, c - r, c + r, c + r], fill=fill, outline=outline, width=width)

    R = lambda name: k[name] * n  # noqa: E731

    # seat + a screw, exactly where the C++ puts them
    circle(R("seat"), fill=(0, 0, 0, 140))
    circle(R("seat"), outline=tuple(hair) + (115,), width=max(1, int(0.004 * n)))

    # knurled skirt: black valleys, lit ridges
    count = int(k["knurlCount"])
    band = R("knurlOut") - R("knurlIn")
    for i in range(count):
        a = 2 * math.pi * i / count
        dx, dy = math.sin(a), -math.cos(a)
        p0 = (c + dx * R("knurlIn"), c + dy * R("knurlIn"))
        p1 = (c + dx * R("knurlOut"), c + dy * R("knurlOut"))
        d.line([p0, p1], fill=(0, 0, 0, 115), width=max(1, int(R("knurlStroke") * 1.15)))
        lit = 0.5 + 0.5 * (-dy)
        q0 = (p0[0] + dx * R("knurlStroke") * 0.5, p0[1] + dy * R("knurlStroke") * 0.5)
        q1 = (p1[0] + dx * R("knurlStroke") * 0.5, p1[1] + dy * R("knurlStroke") * 0.5)
        d.line([q0, q1], fill=(255, 255, 255, int(255 * (0.06 + 0.24 * lit * lit))),
               width=max(1, int(R("knurlStroke") * 0.55)))
    assert band > 0

    # cap: a vertical gradient, darker at the rim
    r = R("body")
    for i in range(int(r * 2)):
        y = c - r + i
        t = i / max(1.0, r * 2)
        col = mix(mix(surface_hi, (255, 255, 255), 0.06), mix(colour("bg", (15, 18, 24)), (0, 0, 0), 0.30),
                  1.0 - t if t < 0.6 else (t - 0.4) * 1.6)
        half = math.sqrt(max(0.0, r * r - (y - c) ** 2))
        d.line([(c - half, y), (c + half, y)], fill=col)
    circle(r, outline=(255, 255, 255, 40), width=1)
    d.arc([c - r * 0.82, c - r * 0.82, c + r * 0.82, c + r * 0.82], 200, 340,
          fill=tuple(hot) + (36,), width=max(1, int(0.026 * n)))

    # the ring: travel in a groove, value in the bright end of the accent
    ring = R("groove")
    gs = R("grooveStroke")
    d.arc([c - ring, c - ring, c + ring, c + ring], 135, 405,
          fill=(0, 0, 0, 153), width=max(2, int(gs + SS)))
    d.arc([c - ring, c - ring, c + ring, c + ring], 135, 405,
          fill=tuple(muted) + (72,), width=max(1, int(gs * 0.45)))
    ang = -135.0 + 270.0 * value
    if value > 0.02:
        d.arc([c - ring, c - ring, c + ring, c + ring], 135, 135 + 270 * value,
              fill=tuple(mix(accent, hot, 0.55)) + (255,), width=max(2, int(gs)))

    # the scale, only above the threshold
    if box >= k["ticksMinBox"]:
        for i in range(11):
            a = math.radians(-135 + 27 * i)
            major = i % 5 == 0
            inner = R("majorIn") if major else R("tickIn")
            p0 = (c + math.sin(a) * inner, c - math.cos(a) * inner)
            p1 = (c + math.sin(a) * R("tickOut"), c - math.cos(a) * R("tickOut"))
            col = colour("text", (243, 242, 239)) if major else muted
            d.line([p0, p1], fill=tuple(col) + (230 if major else 115,),
                   width=max(1, int(R("majorStroke") if major else R("minorStroke"))))

    # pointer: tail off-centre, small head
    a = math.radians(ang)
    tail = (c + math.sin(a) * r * 0.18, c - math.cos(a) * r * 0.18)
    tip = (c + math.sin(a) * r * 0.70, c - math.cos(a) * r * 0.70)
    d.line([tail, tip], fill=(0, 0, 0, 140), width=max(1, int(0.055 * n)))
    d.line([tail, tip], fill=tuple(hot) + (255,), width=max(1, int(0.044 * n)))
    hr = 0.040 * n
    d.ellipse([tip[0] - hr, tip[1] - hr, tip[0] + hr, tip[1] + hr], fill=tuple(hot) + (255,))

    img = img.resize((size // SS, size // SS), Image.LANCZOS)

    # a strip: 54 px at 1:1, then 1:1 with a 2x zoom, then the ticked size
    small = img
    zoom = small.resize((small.width * 2, small.height * 2), Image.NEAREST)
    big = draw_one(90.0, value)
    pad = 16
    W = pad * 3 + small.width + zoom.width + big.width
    H = pad * 2 + max(small.height, zoom.height, big.height) + 22
    sheet = Image.new("RGB", (W, H), (12, 13, 16))
    sheet.paste(small, (pad, pad))
    sheet.paste(zoom, (pad * 2 + small.width, pad))
    sheet.paste(big, (pad * 3 + small.width + zoom.width, pad))
    dd = ImageDraw.Draw(sheet)
    dd.text((pad, H - 18), f"{box:g} px (as shipped)", fill=(150, 155, 165))
    dd.text((pad * 2 + small.width, H - 18), "2x", fill=(150, 155, 165))
    dd.text((pad * 3 + small.width + zoom.width, H - 18), "90 px (ticks appear)",
            fill=(150, 155, 165))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"{out}  {sheet.size}")


def draw_one(box: float, value: float) -> Image.Image:
    """One dial on its own, for the strip."""
    k = spec_numbers()
    n = int(round(box * SS))
    size = n + int(0.10 * n)
    img = Image.new("RGB", (size, size), colour("bg", (15, 18, 24)))
    d = ImageDraw.Draw(img, "RGBA")
    c = size / 2.0
    accent = colour("accent", (236, 79, 2))
    hot = colour("accentHi", (255, 138, 77))
    surface_hi = colour("surfaceHi", (31, 36, 45))
    hair = colour("hairline", (57, 64, 77))
    muted = colour("muted", (191, 195, 203))
    R = lambda name: k[name] * n  # noqa: E731

    def circle(r, fill=None, outline=None, width=1):
        d.ellipse([c - r, c - r, c + r, c + r], fill=fill, outline=outline, width=width)

    circle(R("seat"), fill=(0, 0, 0, 140))
    circle(R("seat"), outline=tuple(hair) + (115,), width=max(1, int(0.004 * n)))
    count = int(k["knurlCount"])
    for i in range(count):
        a = 2 * math.pi * i / count
        dx, dy = math.sin(a), -math.cos(a)
        p0 = (c + dx * R("knurlIn"), c + dy * R("knurlIn"))
        p1 = (c + dx * R("knurlOut"), c + dy * R("knurlOut"))
        d.line([p0, p1], fill=(0, 0, 0, 115), width=max(1, int(R("knurlStroke") * 1.15)))
        lit = 0.5 + 0.5 * (-dy)
        q0 = (p0[0] + dx * R("knurlStroke") * 0.5, p0[1] + dy * R("knurlStroke") * 0.5)
        q1 = (p1[0] + dx * R("knurlStroke") * 0.5, p1[1] + dy * R("knurlStroke") * 0.5)
        d.line([q0, q1], fill=(255, 255, 255, int(255 * (0.06 + 0.24 * lit * lit))),
               width=max(1, int(R("knurlStroke") * 0.55)))
    r = R("body")
    circle(r, fill=mix(surface_hi, (0, 0, 0), 0.25))
    circle(r, outline=(255, 255, 255, 40), width=1)
    ring, gs = R("groove"), R("grooveStroke")
    d.arc([c - ring, c - ring, c + ring, c + ring], 135, 405, fill=(0, 0, 0, 153),
          width=max(2, int(gs + SS)))
    d.arc([c - ring, c - ring, c + ring, c + ring], 135, 405, fill=tuple(muted) + (72,),
          width=max(1, int(gs * 0.45)))
    ang = -135.0 + 270.0 * value
    if value > 0.02:
        d.arc([c - ring, c - ring, c + ring, c + ring], 135, 135 + 270 * value,
              fill=tuple(mix(accent, hot, 0.55)) + (255,), width=max(2, int(gs)))
    if box >= k["ticksMinBox"]:
        for i in range(11):
            a = math.radians(-135 + 27 * i)
            major = i % 5 == 0
            inner = R("majorIn") if major else R("tickIn")
            p0 = (c + math.sin(a) * inner, c - math.cos(a) * inner)
            p1 = (c + math.sin(a) * R("tickOut"), c - math.cos(a) * R("tickOut"))
            col = colour("text", (243, 242, 239)) if major else muted
            d.line([p0, p1], fill=tuple(col) + (230 if major else 115,),
                   width=max(1, int(R("majorStroke") if major else R("minorStroke"))))
    a = math.radians(ang)
    tail = (c + math.sin(a) * r * 0.18, c - math.cos(a) * r * 0.18)
    tip = (c + math.sin(a) * r * 0.70, c - math.cos(a) * r * 0.70)
    d.line([tail, tip], fill=(0, 0, 0, 140), width=max(1, int(0.055 * n)))
    d.line([tail, tip], fill=tuple(hot) + (255,), width=max(1, int(0.044 * n)))
    hr = 0.040 * n
    d.ellipse([tip[0] - hr, tip[1] - hr, tip[0] + hr, tip[1] + hr], fill=tuple(hot) + (255,))
    return img.resize((size // SS, size // SS), Image.LANCZOS)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--box", type=float, default=54.0)
    ap.add_argument("--value", type=float, default=0.62)
    ap.add_argument("--out", default=str(ROOT / "docs" / "preview-knob.png"))
    a = ap.parse_args()
    draw(a.box, a.value, Path(a.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
