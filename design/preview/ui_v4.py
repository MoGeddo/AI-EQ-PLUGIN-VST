#!/usr/bin/env python3
"""ui_v4.py - the panel, second attempt: modern, not primitive.

The first rebuild was a flat engraved metal plate.  The user's verdict on that
family of looks: "شايف الاشكال العصرية مش البدائية … مش تحسها كئيبة كدة".  So this
follows what the reference store actually sells (In The Mix Bassmaker / Bandwidth):

  * a deep charcoal panel with a rim highlight and a real vignette, not a flat fill;
  * GIANT embossed typography behind the graph - the single strongest "modern
    product" cue there is, and it costs nothing;
  * every control sits in its own soft-shadowed circle with a gloss arc, and its
    value lives in a small dark LCD chip, not in grey text on the panel;
  * the graph is a recessed glass well: inner shadow, faint radial glow, grid you
    can barely see, an analyser with a gradient fill and a glow under the curve;
  * one accent (amber) plus one cold counterpoint (a very dim blue in the glow) so
    the window does not read as monochrome;
  * letter-spaced micro-caps for Latin, Arabic for everything that is a sentence.

Curve maths is the DSP's own: RBJ peaking + the 24 dB/oct Butterworth low cut.

    /tmp/venv/bin/python design/preview/ui_v4.py
"""
from __future__ import annotations

import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont
import arabic_reshaper
from bidi.algorithm import get_display

ROOT = pathlib.Path(__file__).resolve().parents[2]
FONT_R = ROOT / "plugin" / "Source" / "assets" / "Cairo-Regular.ttf"
FONT_B = ROOT / "plugin" / "Source" / "assets" / "Cairo-SemiBold.ttf"
ARABIC_FALLBACK = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

W, H = 1000, 620
OUT = ROOT / "design" / "preview" / "ui_v4_1000x620.png"

# palette
BASE = (20, 20, 24)
TOP = (30, 30, 36)
WELL = (12, 12, 15)
TEXT = (240, 237, 231)
DIM = (152, 148, 143)     # >= 4.5:1 on the panel
FAINT = (118, 113, 110)   # >= 3:1 on the panel; decorative text only (see check_contrast.py)
AMBER = (255, 178, 66)
AMBER_HOT = (255, 214, 140)
COLD = (86, 108, 190)
IVORY = (226, 225, 222)

GRAPH = (28, 78, W - 28, 386)
F_MIN, F_MAX = 30.0, 18000.0
DB_MIN, DB_MAX = -18.0, 18.0
SR = 48000.0

_cache = {}


# ------------------------------------------------------------------ text helpers
def font(path, size):
    key = (str(path), size)
    if key not in _cache:
        _cache[key] = ImageFont.truetype(str(path), size)
    return _cache[key]


def cmap_of(path):
    if path not in _cache:
        from fontTools.ttLib import TTFont
        _cache[path] = TTFont(str(path)).getBestCmap()
    return _cache[path]


def shape(text):
    return get_display(arabic_reshaper.reshape(text), base_dir="R")


def ar_text(d, xy, s, size=12, fill=DIM, bold=False, anchor="la"):
    shaped = shape(s)
    main_path = FONT_B if bold else FONT_R
    main, fb, cm = font(main_path, size), font(ARABIC_FALLBACK, size), cmap_of(main_path)
    widths = [d.textlength(ch, font=(main if ord(ch) in cm else fb)) for ch in shaped]
    total = sum(widths)
    x, y = xy
    if anchor[0] == "m":
        x -= total / 2
    elif anchor[0] == "r":
        x -= total
    for ch, w in zip(shaped, widths):
        f = main if ord(ch) in cm else fb
        d.text((x, y), ch, font=f, fill=fill)
        x += w


def caps(d, xy, s, size=10, fill=DIM, tracking=2.6, anchor="la", bold=False):
    f = font(FONT_B if bold else FONT_R, size)
    widths = [d.textlength(ch, font=f) + tracking for ch in s]
    total = sum(widths) - tracking
    x, y = xy
    if anchor[0] == "m":
        x -= total / 2
    elif anchor[0] == "r":
        x -= total
    for ch, w in zip(s, widths):
        d.text((x, y), ch, font=f, fill=fill)
        x += w


# ------------------------------------------------------------------ surfaces
def rounded_mask(size, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius=radius, fill=255)
    return m


def vertical_gradient(size, top, bottom):
    w, h = size
    img = Image.new("RGB", (1, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        px[0, y] = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
    return img.resize(size)


def drop_shadow(canvas, mask, offset=(0, 8), blur=18, alpha=170, grow=0):
    """A soft shadow under an arbitrary shape, composited onto an RGBA canvas."""
    m = mask
    if grow:
        m = m.filter(ImageFilter.MaxFilter(grow * 2 + 1))
    sh = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    black = Image.new("RGBA", canvas.size, (0, 0, 0, alpha))
    sh.paste(black, offset, m)
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    return Image.alpha_composite(canvas, sh)


def inner_shadow(size, radius, depth=10, alpha=150):
    """dark band inside the top edge of a rounded rect - the 'recessed well' cue"""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for i in range(depth):
        a = int(alpha * (1 - i / depth) ** 2)
        d.rounded_rectangle([i, i, size[0] - 1 - i, size[1] - 1 - i], radius=max(1, radius - i),
                            outline=(0, 0, 0, a), width=1)
    return layer


def glow(canvas, draw_fn, blur=9, alpha=150):
    """Draw on a transparent layer, blur it, composite it - in place.

    Image.alpha_composite(a, b) *returns* a new image; a function that shadows the
    caller's variable with it silently throws the drawing away.  Those were the
    missing curve and the missing analyser in the first render of this file."""
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(layer))
    layer = layer.filter(ImageFilter.GaussianBlur(blur))
    if alpha < 255:
        layer.putalpha(layer.getchannel("A").point(lambda v: int(v * alpha / 255)))
    canvas.alpha_composite(layer)
    return canvas


def over(canvas, draw_fn):
    """A translucent stroke must be composited, not written: ImageDraw on an RGBA
    canvas *replaces* the pixel (including its alpha), so `fill=(255,255,255,12)`
    punches a nearly transparent hole instead of drawing a faint line."""
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(layer))
    canvas.alpha_composite(layer)
    return canvas


# ------------------------------------------------------------------ curve maths
def peak_db(f, f0, q, gain_db):
    a = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * f0 / SR
    alpha = math.sin(w0) / (2 * q)
    b0, b1, b2 = 1 + alpha * a, -2 * math.cos(w0), 1 - alpha * a
    a0, a1, a2 = 1 + alpha / a, -2 * math.cos(w0), 1 - alpha / a
    w = 2 * math.pi * f / SR
    z1, z2 = complex(math.cos(-w), math.sin(-w)), complex(math.cos(-2 * w), math.sin(-2 * w))
    return 20 * math.log10(max(1e-9, abs(b0 + b1 * z1 + b2 * z2) / abs(a0 + a1 * z1 + a2 * z2)))


def hpf_db(f, corner):
    return -20.0 * math.log10(1.0 + (f / corner) ** -4)


def correction_db(f, bands, hpf):
    return (hpf_db(f, hpf) if hpf > 0 else 0.0) + sum(peak_db(f, *b) for b in bands)


def fx(f):
    x0, _, x1, _ = GRAPH
    return x0 + (math.log10(f) - math.log10(F_MIN)) / (math.log10(F_MAX) - math.log10(F_MIN)) * (x1 - x0)


def dy(db):
    _, y0, _, y1 = GRAPH
    return y1 - (db - DB_MIN) / (DB_MAX - DB_MIN) * (y1 - y0)


# ------------------------------------------------------------------ the graph
def draw_graph(canvas, bands, hpf):
    x0, y0, x1, y1 = GRAPH
    size = (x1 - x0, y1 - y0)
    well = vertical_gradient(size, (26, 26, 32), (11, 11, 14)).convert("RGBA")
    # cold radial hint, top-left; warm one bottom-right
    for (cx, cy, r, col, a) in ((0.22, 0.10, 0.60, COLD, 26), (0.80, 0.88, 0.55, AMBER, 16)):
        g = Image.new("RGBA", size, (0, 0, 0, 0))
        gd = ImageDraw.Draw(g)
        rr = int(max(size) * r)
        gd.ellipse([cx * size[0] - rr, cy * size[1] - rr, cx * size[0] + rr, cy * size[1] + rr],
                   fill=col + (a,))
        g = g.filter(ImageFilter.GaussianBlur(rr / 3))
        well = Image.alpha_composite(well, g)
    well.putalpha(rounded_mask(size, 14))
    canvas.alpha_composite(well, (x0, y0))

    def grid(d):
        f = F_MIN
        while f <= F_MAX:
            x = fx(f)
            decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
            d.line([(x, y0), (x, y1)], fill=(255, 255, 255, 26 if decade else 12))
            f *= 2 ** (1 / 3)
        for db in (-12, -6, 0, 6, 12):
            y = dy(db)
            d.line([(x0, y), (x1, y)], fill=(255, 255, 255, 34 if db == 0 else 14))
            if db:
                d.text((x1 - 8, y - 9), f"{db:+d}", font=font(FONT_R, 9),
                       fill=(255, 255, 255, 90), anchor="ra")
        for f, lab in ((100, "100"), (1000, "1k"), (10000, "10k")):
            d.text((fx(f) + 6, y1 - 16), lab, font=font(FONT_R, 10), fill=(255, 255, 255, 110))
    over(canvas, grid)

    # giant wordmark inside the well: the strongest "modern product" cue, and it
    # costs nothing - 16/255 of white over the dark glass
    def mark(d):
        f = font(FONT_B, 86)
        txt = "V O I C E"
        w = d.textlength(txt, font=f)
        d.text(((x0 + x1) / 2 - w / 2, (y0 + y1) / 2 - 62), txt, font=f, fill=(255, 255, 255, 16))
    over(canvas, mark)

    draw_analyser(canvas)
    draw_curve(canvas, bands, hpf)

    # recessed edge + rim light
    def edge(d):
        d.rounded_rectangle([x0, y0, x1, y1], radius=14, outline=(0, 0, 0, 200), width=1)
        d.line([(x0 + 16, y0 + 1), (x1 - 16, y0 + 1)], fill=(255, 255, 255, 40))
    over(canvas, edge)
    canvas.alpha_composite(inner_shadow(size, 14, 12, 170), (x0, y0))

    draw_handles(canvas, bands, hpf)


def analyser_points(seed=5):
    rnd = random.Random(seed)

    def level(f):
        db = -1.4 * math.log2(max(f, 40.0) / 600.0)
        for fc, bw, amp in ((700, 200, 13.0), (1900, 400, 9.0), (3000, 700, 5.0), (900, 380, -5.0)):
            db += amp / (1.0 + ((f - fc) / bw) ** 2)
        db -= 26.0 / (1.0 + (7000.0 / max(f, 60.0)) ** 2)
        db -= 7.0 / (1.0 + (f / 70.0) ** 2)
        return db

    raw = []
    for i in range(300):
        f = F_MIN * (F_MAX / F_MIN) ** (i / 299)
        raw.append((f, level(f) + 0.35 * math.sin(i / 4.1) + 0.2 * math.sin(i / 11.0)
                    + rnd.uniform(-0.1, 0.1)))
    lo, hi = min(v for _, v in raw), max(v for _, v in raw)
    top, bottom = 11.0, -14.0
    return [(fx(f), dy(bottom + (v - lo) / max(1e-6, hi - lo) * (top - bottom))) for f, v in raw]


def draw_analyser(canvas):
    pts = analyser_points()
    poly = [(GRAPH[0], GRAPH[3])] + pts + [(GRAPH[2], GRAPH[3])]

    # fill: 2.5% white, faded to nothing at the bottom of the well
    h = int(GRAPH[3] - GRAPH[1])
    graded = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(graded).polygon(poly, fill=(230, 230, 228, 255))
    a = graded.getchannel("A")
    ramp = Image.new("L", (1, h))
    rp = ramp.load()
    for y in range(h):
        rp[0, y] = int(46 * (1 - y / h) ** 1.3)          # <= 18% at the top, 0 at the floor
    full = Image.new("L", canvas.size, 0)
    full.paste(ramp.resize((int(GRAPH[2] - GRAPH[0]), h)), (int(GRAPH[0]), int(GRAPH[1])))
    graded.putalpha(Image.composite(a, Image.new("L", canvas.size, 0), full))   # ramp IS the mask
    canvas.alpha_composite(graded)

    glow(canvas, lambda d: d.line(pts, fill=IVORY + (120,), width=4, joint="curve"), blur=12, alpha=110)
    over(canvas, lambda d: d.line(pts, fill=(235, 234, 232, 235), width=2, joint="curve"))


def draw_curve(canvas, bands, hpf):
    pts = []
    for i in range(481):
        f = F_MIN * (F_MAX / F_MIN) ** (i / 480)
        pts.append((fx(f), dy(max(DB_MIN, min(DB_MAX, correction_db(f, bands, hpf))))))

    glow(canvas, lambda d: d.line(pts, fill=AMBER + (200,), width=5, joint="curve"), blur=14, alpha=200)
    glow(canvas, lambda d: d.line(pts, fill=AMBER_HOT + (150,), width=2, joint="curve"), blur=3, alpha=170)
    over(canvas, lambda d: d.line(pts, fill=(255, 226, 168, 255), width=2, joint="curve"))


def draw_handles(canvas, bands, hpf):
    # the low cut: a bracket on the curve, not a dot
    hx = fx(hpf)
    hy = dy(correction_db(hpf, bands, hpf))
    over(canvas, lambda d: d.line([(hx, hy + 4), (hx, GRAPH[3] - 6)], fill=AMBER + (120,), width=1))
    glow(canvas, lambda dd: dd.ellipse([hx - 4, hy - 4, hx + 4, hy + 4], fill=AMBER), blur=6, alpha=255)
    d = ImageDraw.Draw(canvas)
    d.ellipse([hx - 3.5, hy - 3.5, hx + 3.5, hy + 3.5], fill=AMBER_HOT)
    d.rounded_rectangle([hx - 20, hy - 26, hx + 20, hy - 12], radius=6, fill=(0, 0, 0, 150))
    caps(d, (hx, hy - 24), f"{hpf:.0f}", 9, AMBER_HOT, 0.6, anchor="ma", bold=True)

    for (bf, bq, bg) in bands:
        x, y = fx(bf), dy(correction_db(bf, bands, hpf))
        glow(canvas, lambda dd, x=x, y=y: dd.ellipse([x - 6, y - 6, x + 6, y + 6],
                                                     fill=AMBER), blur=7, alpha=230)
        d = ImageDraw.Draw(canvas)
        d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(14, 14, 17), outline=AMBER, width=2)
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=AMBER_HOT)
        lab = f"{bf/1000:.1f}k" if bf >= 1000 else f"{bf:.0f}"
        w = 9 + 5.2 * len(lab)
        d.rounded_rectangle([x - w, y + 12, x + w, y + 28], radius=7, fill=(0, 0, 0, 150))
        caps(d, (x, y + 14), lab, 9, AMBER_HOT, 0.5, anchor="ma", bold=True)
        # the gain, as a number, on the other side
        caps(d, (x, y - 30), f"{correction_db(bf, bands, hpf):+.1f}", 9, DIM, 0.4, anchor="ma", bold=True)


# ------------------------------------------------------------------ controls
def knob(canvas, cx, cy, r, value, label_ar, readout, accent=False):
    mask = Image.new("L", (int(r * 2 + 2), int(r * 2 + 2)), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, mask.size[0] - 1, mask.size[1] - 1], fill=255)
    sh = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", mask.size, (0, 0, 0, 150)), (int(cx - r), int(cy - r + 7)), mask)
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))

    # body: dark metal with a top gloss
    body = Image.new("RGBA", (int(r * 2), int(r * 2)), (0, 0, 0, 0))
    bd = ImageDraw.Draw(body)
    for i in range(int(r)):
        t = i / r
        c = int(74 - 32 * t)
        bd.ellipse([i, i, body.size[0] - 1 - i, body.size[1] - 1 - i], fill=(c, c + 1, c + 4, 255))
    gloss = Image.new("RGBA", body.size, (0, 0, 0, 0))
    ImageDraw.Draw(gloss).ellipse([int(r * 0.22), int(r * 0.10), int(r * 1.78), int(r * 1.05)],
                                  fill=(255, 255, 255, 26))
    body = Image.alpha_composite(body, gloss.filter(ImageFilter.GaussianBlur(r * 0.18)))
    body.putalpha(rounded_mask(body.size, int(r)))
    canvas.alpha_composite(body, (int(cx - r), int(cy - r)))

    def ticks(dd):
        dd.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(0, 0, 0, 210), width=1)
        dd.arc([cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1], 186, 340, fill=(255, 255, 255, 46))
        for i in range(21):
            aa = math.radians(-135 + 13.5 * i)
            r0, r1 = r + 4, r + (9 if i % 5 == 0 else 7)
            dd.line([(cx + r0 * math.sin(aa), cy - r0 * math.cos(aa)),
                     (cx + r1 * math.sin(aa), cy - r1 * math.cos(aa))],
                    fill=(255, 255, 255, 44) if i % 5 else (255, 255, 255, 80), width=1)
    if accent:
        glow(canvas, lambda dd: dd.arc([cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1], 186, 340,
                                       fill=AMBER + (150,), width=2), blur=4, alpha=170)
    over(canvas, ticks)
    ang = math.radians(-135 + 270 * max(0.0, min(1.0, value)))
    col = AMBER if accent else (245, 242, 236)
    tipx, tipy = cx + (r - 6) * math.sin(ang), cy - (r - 6) * math.cos(ang)
    glow(canvas, lambda dd: dd.line([(cx, cy), (tipx, tipy)], fill=col, width=3), blur=5, alpha=200)
    d = ImageDraw.Draw(canvas)
    d.line([(cx, cy), (tipx, tipy)], fill=col, width=3)
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(20, 20, 24), outline=(255, 255, 255, 30))

    # LCD chip
    chip_w = max(58, 10 + 7.0 * len(readout))
    cy2 = cy + r + 14
    over(canvas, lambda dd: (dd.rounded_rectangle([cx - chip_w, cy2, cx + chip_w, cy2 + 22],
                                                  radius=6, fill=(0, 0, 0, 150)),
                             dd.rounded_rectangle([cx - chip_w, cy2, cx + chip_w, cy2 + 22],
                                                  radius=6, outline=(255, 255, 255, 26))))
    caps(d, (cx, cy2 + 6), readout, 10, AMBER_HOT if accent else TEXT, 1.0, anchor="ma", bold=True)
    ar_text(d, (cx, cy2 + 26), label_ar, 11, DIM, anchor="ma")
    return canvas


def led(canvas, x, y, r=4, col=(120, 235, 150)):
    glow(canvas, lambda d: d.ellipse([x - r, y - r, x + r, y + r], fill=col), blur=6, alpha=200)
    d = ImageDraw.Draw(canvas)
    d.ellipse([x - r, y - r, x + r, y + r], fill=col)
    d.ellipse([x - r + 1, y - r + 1, x - 1, y - 1], fill=(255, 255, 255, 170))
    return canvas


def pill(canvas, box, text, on, size=10):
    x0, y0, x1, y1 = box

    def paint(dd):
        dd.rounded_rectangle(box, radius=(y1 - y0) // 2,
                             fill=(255, 190, 90, 30) if on else (0, 0, 0, 120))
        dd.rounded_rectangle(box, radius=(y1 - y0) // 2,
                             outline=(255, 190, 90, 190) if on else (255, 255, 255, 34))
    over(canvas, paint)
    ar_text(ImageDraw.Draw(canvas), ((x0 + x1) / 2, y0 + (y1 - y0) / 2 - size * 0.78), text, size,
            AMBER_HOT if on else DIM, anchor="ma")


# ------------------------------------------------------------------ build
def build():
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    mask = rounded_mask((W, H), 16)
    canvas = drop_shadow(canvas, mask, (0, 14), 26, 210)

    panel = vertical_gradient((W, H), TOP, BASE).convert("RGBA")
    # vignette
    vig = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    vd = ImageDraw.Draw(vig)
    for i in range(70):
        vd.rectangle([i, i, W - 1 - i, H - 1 - i], outline=(0, 0, 0, int(3 + i * 0.9)))
    panel = Image.alpha_composite(panel, vig.filter(ImageFilter.GaussianBlur(24)))
    panel.putalpha(mask)
    canvas.alpha_composite(panel)

    d = ImageDraw.Draw(canvas)
    over(canvas, lambda dd: dd.rounded_rectangle([0, 0, W - 1, H - 1], radius=16,
                                                 outline=(255, 255, 255, 34)))

    # ---- header -------------------------------------------------------------
    caps(d, (34, 24), "AEQ", 11, (255, 255, 255, 120), 4.0, bold=True)
    caps(d, (34 + 44, 24), "RESTORE", 11, (255, 255, 255, 60), 4.0)
    canvas = led(canvas, W - 176, 32, 4, (130, 240, 160))
    d = ImageDraw.Draw(canvas)
    ar_text(d, (W - 162, 22), "بيسمع", 12, DIM)
    pill(canvas, (W - 108, 17, W - 34, 47), "تلقائي", True)
    d = ImageDraw.Draw(canvas)
    caps(d, (W - 34, 52), "AUTO", 8, (255, 255, 255, 90), 1.6, anchor="ra")

    # ---- giant embossed watermark behind the graph -------------------------
    wm = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wm)
    caps(wd, (W // 2, 128), "V O I C E", 108, (255, 255, 255, 12), 6.0, anchor="ma", bold=True)
    wm = wm.filter(ImageFilter.GaussianBlur(0.6))
    canvas.alpha_composite(wm)
    wd2 = ImageDraw.Draw(canvas)
    caps(wd2, (W // 2, 127), "V O I C E", 108, (0, 0, 0, 90), 6.0, anchor="ma", bold=True)

    brands = [(430.0, 2.2, -4.6), (3000.0, 1.0, 2.6), (12000.0, 0.7, 1.8)]
    hpf = 88.0
    draw_graph(canvas, brands, hpf)

    # graph corner readout: what it did, as numbers
    d = ImageDraw.Draw(canvas)
    caps(d, (GRAPH[0] + 14, GRAPH[1] + 12), "LIVE", 8, AMBER_HOT, 2.2, bold=True)
    caps(d, (GRAPH[0] + 14, GRAPH[1] + 26), "4 MOVES", 8, (255, 255, 255, 90), 2.0)

    # ---- controls -----------------------------------------------------------
    cy = 452
    knobs = [
        ("قطع الأول", "88 Hz", hpf / 220.0, True),
        ("الجسم", "-4.6 dB", 0.28, True),
        ("الحضور", "+2.6 dB", 0.66, False),
        ("الهواء", "+1.8 dB", 0.59, False),
        ("سيبيلانت", "AUTO", 0.3, False),
        ("الخرج", "0.0 dB", 0.5, False),
    ]
    gap = (W - 2 * 96) / (len(knobs) - 1)
    for i, (lab, val, v, acc) in enumerate(knobs):
        canvas = knob(canvas, 96 + i * gap, cy, 40, v, lab, val, accent=acc)

    # ---- footer -------------------------------------------------------------
    d = ImageDraw.Draw(canvas)
    over(canvas, lambda dd: dd.line([(34, H - 44), (W - 34, H - 44)], fill=(255, 255, 255, 22)))
    ar_text(d, (34, H - 32), "اتظبط لوحده بعد ٨.٢ ثانية · ٤ حركات · الجسم زي ما هو", 13, TEXT)
    caps(d, (W - 34, H - 40), "V1.2.9", 9, FAINT, 1.8, anchor="ra")
    # a 12-step activity strip, like a mini meter
    for i in range(12):
        on = i < 9
        over(canvas, lambda dd, i=i, on=on: dd.rounded_rectangle(
            [W - 34 - (12 - i) * 7, H - 22, W - 34 - (12 - i) * 7 + 4, H - 17], radius=2,
            fill=AMBER + (200,) if on else (255, 255, 255, 30)))

    canvas.putalpha(rounded_mask((W, H), 16))
    canvas.save(OUT)
    print(OUT)


if __name__ == "__main__":
    build()
