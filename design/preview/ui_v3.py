#!/usr/bin/env python3
"""ui_v3.py - the panel, drawn with the geometry the plugin will use.

What this replaces: a dark window split into rounded "cards", seven knobs with
glowing rings, pill-shaped switches and a column of explanatory boxes.  The user
deleted it in one sentence.  A plugin is a piece of gear: one continuous panel,
engraved lines, printed labels, and a graph that owns the window.

Rules this file follows, so the JUCE code can follow them too:

  * ONE accent (amber, the colour of a VU lamp) for the correction curve.  The
    analyser is ivory at low opacity.  Nothing else is coloured.
  * the graph is ~55% of the window; controls sit in one row underneath it, in
    the order a mixer reaches for them.
  * knob = metal skirt + ticks + dark cap + a single indicator.  No glow, no
    ring, no drop shadow.
  * every number is printed once.  Arabic labels, Latin numerals.
  * the correction is drawn from the same biquad response the DSP uses (RBJ
    peaking + the 24 dB/oct Butterworth low cut), not from a decorative spline.

    /tmp/venv/bin/python design/preview/ui_v3.py
"""
from __future__ import annotations

import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFont
import arabic_reshaper
from bidi.algorithm import get_display

ROOT = pathlib.Path(__file__).resolve().parents[2]
FONT_R = ROOT / "plugin" / "Source" / "assets" / "Cairo-Regular.ttf"
FONT_B = ROOT / "plugin" / "Source" / "assets" / "Cairo-SemiBold.ttf"

W, H = 1000, 620
OUT = ROOT / "design" / "preview" / "ui_v3_1000x620.png"

INK_LO, INK_HI = (13, 12, 15), (27, 25, 29)
ENGRAVE, SHADOW = (44, 41, 47), (7, 7, 8)
TEXT, TEXT_DIM, TEXT_FAINT = (233, 228, 219), (150, 144, 136), (104, 99, 92)
AMBER, AMBER_DIM = (228, 170, 76), (146, 110, 50)
IVORY = (214, 210, 200)
GRID, GRID_STRONG = (36, 33, 39), (62, 56, 49)

GRAPH = (24, 76, W - 24, 392)
F_MIN, F_MAX = 30.0, 18000.0
DB_MIN, DB_MAX = -18.0, 18.0
SR = 48000.0


# --------------------------------------------------------------------- helpers
ARABIC_FALLBACK = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
_FONT_CACHE = {}


def _font(path, size):
    key = (path, size)
    if key not in _FONT_CACHE:
        _FONT_CACHE[key] = ImageFont.truetype(path, size)
    return _FONT_CACHE[key]


def _cmap(path):
    key = ("cmap", path)
    if key not in _FONT_CACHE:
        from fontTools.ttLib import TTFont
        _FONT_CACHE[key] = TTFont(path).getBestCmap()
    return _FONT_CACHE[key]


def ar(text: str) -> str:
    # base_dir='R' matters: python-bidi's auto-detection reads a short Arabic string
    # with digits or a bullet as LTR and hands back the glyphs in logical order, which
    # when drawn left-to-right is the mirror image of the word.
    return get_display(arabic_reshaper.reshape(text), base_dir="R")


def ar_text(d, xy, s, size=12, fill=TEXT, bold=False, anchor="la"):
    """Shaped Arabic with a per-character font fallback.

    The Cairo files in the repo have no glyph for 55 of the Arabic Presentation
    Forms (including the isolated alef, U+FE8D), which without a shaping engine is
    a row of empty boxes.  Windows shapes with DirectWrite from the base letters and
    never asks for those codepoints, so this is a *preview* problem - but the
    preview has to be readable, so any character Cairo cannot draw is drawn from
    DejaVu, character by character, in visual order."""
    shaped = ar(s)
    main_font, fallback = _font(str(FONT_B if bold else FONT_R), size), _font(ARABIC_FALLBACK, size)
    cmap = _cmap(str(FONT_B if bold else FONT_R))
    widths = []
    for ch in shaped:
        f = main_font if ord(ch) in cmap else fallback
        widths.append(d.textlength(ch, font=f))
    total = sum(widths)
    x, y = xy
    if anchor[0] == "m":
        x -= total / 2
    elif anchor[0] == "r":
        x -= total
    for ch, w in zip(shaped, widths):
        f = main_font if ord(ch) in cmap else fallback
        d.text((x, y), ch, font=f, fill=fill)
        x += w


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONT_B if bold else FONT_R), size)


def fx(f: float) -> float:
    x0, _, x1, _ = GRAPH
    return x0 + (math.log10(f) - math.log10(F_MIN)) / (math.log10(F_MAX) - math.log10(F_MIN)) * (x1 - x0)


def dy(db: float) -> float:
    _, y0, _, y1 = GRAPH
    return y1 - (db - DB_MIN) / (DB_MAX - DB_MIN) * (y1 - y0)


# ----------------------------------------------------------------- the response
def peak_db(f: float, f0: float, q: float, gain_db: float) -> float:
    """RBJ peaking magnitude, in dB - the same filter the DSP runs."""
    a = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * f0 / SR
    alpha = math.sin(w0) / (2 * q)
    b0, b1, b2 = 1 + alpha * a, -2 * math.cos(w0), 1 - alpha * a
    a0, a1, a2 = 1 + alpha / a, -2 * math.cos(w0), 1 - alpha / a
    w = 2 * math.pi * f / SR
    z1, z2 = complex(math.cos(-w), math.sin(-w)), complex(math.cos(-2 * w), math.sin(-2 * w))
    num = b0 + b1 * z1 + b2 * z2
    den = a0 + a1 * z1 + a2 * z2
    return 20 * math.log10(max(1e-9, abs(num) / abs(den)))


def hpf_db(f: float, corner: float) -> float:
    r = f / corner
    return -20.0 * math.log10(1.0 + r ** -4)      # 24 dB/oct


def correction_db(f, bands, hpf):
    db = hpf_db(f, hpf) if hpf > 0 else 0.0
    for (bf, bq, bg) in bands:
        db += peak_db(f, bf, bq, bg)
    return db


# --------------------------------------------------------------------- drawing
def paint_panel(img):
    d = ImageDraw.Draw(img)
    for x in range(W):
        t = x / W
        k = 0.5 + 0.5 * min(t, 1 - t) * 2.2
        d.line([(x, 0), (x, H)], fill=tuple(int(INK_LO[i] + (INK_HI[i] - INK_LO[i]) * k) for i in range(3)))
    for y in range(H):
        a = 16 if y < 90 else 0
        if a:
            d.line([(0, y), (W, y)], fill=(30, 28, 32))
    return d


def hairline(d, x0, x1, y):
    d.line([(x0, y), (x1, y)], fill=ENGRAVE, width=1)
    d.line([(x0, y + 1), (x1, y + 1)], fill=SHADOW, width=1)


def draw_graph(d):
    x0, y0, x1, y1 = GRAPH
    d.rectangle([x0, y0, x1, y1], fill=(14, 13, 16))
    d.rectangle([x0, y0, x1, y1], outline=(30, 28, 32))
    f = F_MIN
    while f <= F_MAX:
        x = fx(f)
        decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
        d.line([(x, y0), (x, y1)], fill=GRID_STRONG if decade else GRID, width=1)
        f *= 2 ** (1 / 3)
    for db in (-12, -6, 0, 6, 12):
        y = dy(db)
        d.line([(x0, y), (x1, y)], fill=(58, 52, 45) if db == 0 else GRID, width=1)
        if db:
            d.text((x1 - 6, y - 8), f"{db:+d}", font=font(10), fill=TEXT_FAINT, anchor="ra")
    for f, lab in ((100, "100"), (1000, "1k"), (10000, "10k")):
        x = fx(f)
        d.line([(x, y1 - 7), (x, y1)], fill=(90, 85, 78), width=1)
        d.text((x + 5, y1 + 5), lab, font=font(11), fill=TEXT_FAINT)


def draw_trace(d, img):
    """A vocal spectrum with its own vertical fit (a take spans ~60 dB, the graph
    is +-18).  Smooth on purpose: an analyser that looks like noise looks broken."""
    rnd = random.Random(11)

    def level(f):
        # a sung "a": F1 near 700, a valley at 1.2k, F2 near 1.9k, F3 near 3k, then
        # the natural roll-off of a voice plus the mic's top
        db = -1.6 * math.log2(max(f, 40.0) / 600.0)
        for fc, bw, amp in ((700, 210, 13.0), (1900, 420, 9.0), (3000, 700, 5.0),
                            (900, 400, -5.0)):
            db += amp / (1.0 + ((f - fc) / bw) ** 2)
        db -= 26.0 / (1.0 + (7000.0 / max(f, 60.0)) ** 2)     # rolls off above ~7 k
        db -= 7.0 / (1.0 + (f / 70.0) ** 2)
        return db

    raw = []
    i = 0
    while i < 300:
        f = F_MIN * (F_MAX / F_MIN) ** (i / 299)
        raw.append((f, level(f) + 0.35 * math.sin(i / 4.1) + 0.2 * math.sin(i / 11.0)
                    + rnd.uniform(-0.12, 0.12)))
        i += 1
    lo, hi = min(v for _, v in raw), max(v for _, v in raw)
    top, bottom = 8.0, -12.0
    pts = [(fx(f), dy(bottom + (v - lo) / max(1e-6, hi - lo) * (top - bottom))) for f, v in raw]

    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    od.polygon([(GRAPH[0], GRAPH[3])] + pts + [(GRAPH[2], GRAPH[3])], fill=IVORY + (14,))
    img.alpha_composite(ov)
    d.line(pts, fill=IVORY + (120,), width=1)
    d.line(pts, fill=IVORY + (45,), width=3, joint="curve")


def draw_correction(d, img, bands, hpf):
    pts = []
    i = 0
    while i <= 480:
        f = F_MIN * (F_MAX / F_MIN) ** (i / 480)
        pts.append((fx(f), dy(max(DB_MIN, min(DB_MAX, correction_db(f, bands, hpf))))))
        i += 1
    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    od.polygon([(GRAPH[0], dy(0))] + pts + [(GRAPH[2], dy(0))], fill=AMBER + (14,))
    img.alpha_composite(ov)
    d.line(pts, fill=AMBER + (255,), width=2, joint="curve")


def handle(d, f, db, label):
    x, y = fx(f), dy(max(DB_MIN, min(DB_MAX, db)))
    d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(15, 14, 17), outline=AMBER, width=2)
    d.line([(x, y + 7), (x, y + 12)], fill=AMBER_DIM, width=1)
    d.text((x, y + 14), label, font=font(10, True), fill=(206, 178, 128), anchor="ma")


def knob(d, cx, cy, r, value, label, readout, accent=False):
    for i, shade in enumerate(((72, 67, 74), (60, 56, 62), (50, 47, 52), (36, 34, 38))):
        rr = r - i
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=shade)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(19, 18, 21), width=1)
    for i in range(11):
        aa = math.radians(-130 + 26 * i)
        heavy = i % 5 == 0
        r0, r1 = r - (7 if heavy else 5), r - 1
        d.line([(cx + r0 * math.sin(aa), cy - r0 * math.cos(aa)),
                (cx + r1 * math.sin(aa), cy - r1 * math.cos(aa))],
               fill=(132, 122, 106) if heavy else (74, 70, 72), width=1)
    br = r - 10
    d.ellipse([cx - br, cy - br, cx + br, cy + br], fill=(42, 39, 44), outline=(17, 16, 19))
    d.arc([cx - br + 1, cy - br + 1, cx + br - 1, cy + br - 1], 165, 305, fill=(70, 65, 68), width=1)
    ang = math.radians(-130 + 260 * max(0.0, min(1.0, value)))
    col = AMBER if accent else (240, 235, 226)
    d.line([(cx + 4 * math.sin(ang), cy - 4 * math.cos(ang)),
            (cx + (br - 2) * math.sin(ang), cy - (br - 2) * math.cos(ang))], fill=col, width=3)
    ar_text(d, (cx, cy + r + 9), label, 13, TEXT_DIM, anchor="ma")
    d.text((cx, cy + r + 29), readout, font=font(13, True), fill=TEXT, anchor="ma")


def lamp(d, x, y, on=True):
    d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(96, 150, 96) if on else (60, 50, 48),
              outline=(24, 30, 24))
    d.ellipse([x - 2, y - 4, x + 1, y - 1], fill=(180, 220, 170) if on else (90, 84, 80))


def switch(d, x, y, w, h, on, label):
    d.rounded_rectangle([x, y, x + w, y + h], radius=4, fill=(16, 15, 18), outline=(46, 43, 49))
    cw = w // 2 - 4
    cx0 = x + 4 if on else x + w // 2
    d.rounded_rectangle([cx0, y + 4, cx0 + cw, y + h - 4], radius=3,
                        fill=(74, 64, 44) if on else (42, 40, 44),
                        outline=(158, 118, 54) if on else (64, 60, 66))
    ar_text(d, (x + w / 2, y + h + 8), label, 11, TEXT_DIM, anchor="ma")


def build():
    img = Image.new("RGBA", (W, H), INK_LO)
    d = paint_panel(img)

    # ---- header -------------------------------------------------------------
    x = 26
    for ch in "AEQ RESTORE":
        d.text((x, 17), ch, font=font(15, True), fill=TEXT)
        x += d.textlength(ch, font=font(15, True)) + 2.6
    ar_text(d, (26, 40), "يسمع المقطع، يقرّر، ويظبط قبل ما تلمسه", 12, TEXT_DIM)

    lamp(d, W - 232, 30)
    ar_text(d, (W - 220, 21), "بيسمع", 12, TEXT_DIM)
    switch(d, W - 96, 19, 58, 22, True, "تلقائي")
    hairline(d, 20, W - 20, 68)

    # ---- graph --------------------------------------------------------------
    draw_graph(d)
    draw_trace(d, img)
    bands = [(430.0, 2.2, -4.6), (3000.0, 1.0, 2.6), (12000.0, 0.7, 1.8)]
    hpf = 88.0
    draw_correction(d, img, bands, hpf)
    handle(d, 430, correction_db(430, bands, hpf), "430")
    handle(d, 3000, correction_db(3000, bands, hpf), "3k")
    handle(d, 12000, correction_db(12000, bands, hpf), "12k")
    hx, hy = fx(hpf), dy(-3.0)
    d.polygon([(hx, hy - 4), (hx + 10, hy + 7), (hx - 10, hy + 7)], outline=AMBER, fill=(15, 14, 17))
    ar_text(d, (hx, hy + 12), "قطع", 10, (206, 178, 128), anchor="ma")

    d.text((GRAPH[2] - 10, GRAPH[1] + 10), "4", font=font(12, True), fill=TEXT_DIM, anchor="ra")
    ar_text(d, (GRAPH[2] - 28, GRAPH[1] + 10), "حركات", 11, TEXT_DIM, anchor="ra")

    hairline(d, 20, W - 20, 400)

    # ---- controls -----------------------------------------------------------
    cy = 462
    knobs = [
        ("قطع الأول", "88 Hz", hpf / 220.0, True),
        ("الجسم", "-4.2 dB", 0.32, True),
        ("الحضور", "+2.4 dB", 0.63, False),
        ("الهواء", "+1.6 dB", 0.57, False),
        ("سيبيلانت", "تلقائي", 0.30, False),
        ("الخرج", "0.0 dB", 0.5, False),
    ]
    gap = (W - 2 * 86) / (len(knobs) - 1)
    for i, (lab, val, v, acc) in enumerate(knobs):
        knob(d, 86 + i * gap, cy, 38, v, lab, val, accent=acc)

    # ---- the decision line, once, in Arabic ---------------------------------
    hairline(d, 20, W - 20, 552)
    ar_text(d, (26, 566), "اتظبط تلقائي بعد ٨.٢ ثانية · ٤ حركات · الجسم زي ما هو", 13, TEXT)
    d.text((W - 26, 570), "v1.2.9", font=font(11), fill=TEXT_FAINT, anchor="ra")

    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], radius=10, fill=255)
    img.putalpha(mask)
    img.save(OUT)
    print(OUT)


if __name__ == "__main__":
    build()
