#!/usr/bin/env python3
"""ui_v8.py - the panel at the level the category actually ships at now.

The verdict on v7 was "بدائي اوي لسة مش عصري" - primitive, still not modern.  Looking
at what actually ships in 2025 (Minimal Audio Morph EQ, Baby Audio Smooth Operator,
oeksound soothe 3, Ozone 12) the difference is not decoration, it is *proportion and
finish*:

  * THE GRAPH IS THE WINDOW.  65-75 % of the height, one surface, bled to the edges.
    v7 spent 37 % of its height on a well and the rest on a take bar, a four-row
    ledger and a footer - five bands of equal weight, which is a form, not an
    instrument.
  * DEPTH IS LIGHT, NOT TEXTURE.  None of them brush a fabric into the background.
    They use a near-black flat field, one huge soft bloom, one-pixel separators, a
    top inner highlight and translucency.  The carbon plate is gone.
  * TYPE IS LIGHT.  Fine weights, small capitals with wide tracking, and numbers as
    the only bold thing on the panel.  v7 shouted in semibold everywhere.
  * ONE ACCENT WITH BLOOM.  A bright accent that actually glows, sitting on a
    gradient, plus translucent band fills - not a matte metal swatch.

The story does not change (design/IDENTITY_AR.md: brass = a decision, patina = a
measurement) and it does not move into more chrome: the take is now a ribbon along
the bottom edge *inside* the graph, sealed by the same machined clamp, so listen ->
stop -> commit happens in the hero rather than in four stacked bands.

    /tmp/venv/bin/python design/preview/ui_v8.py
"""
from __future__ import annotations

import math
import pathlib
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from ui_v4 import (  # noqa: E402
    FONT_B, FONT_R, ar_text, caps, font, glow, hpf_db, inner_shadow, over, peak_db,
    rounded_mask, shape,
)
from ui_v6 import knob_angle, mix, sprite  # noqa: E402

ROOT = HERE.parents[1]
ASSETS = ROOT / "design" / "assets"
W, H = 1180, 700
OUT = HERE / "ui_v8_1180x700.png"

# ---- the world (design/IDENTITY_AR.md) ----------------------------------------------
INK_TOP = (18, 16, 14)
INK_BOT = (8, 8, 9)
TEXT = (244, 240, 232)
DIM = (170, 162, 148)
FAINT = (126, 119, 108)
BRASS = (214, 172, 96)
BRASS_HOT = (255, 226, 176)
PATINA = (120, 186, 162)
PATINA_HOT = (176, 232, 210)

# ---- one type scale, four steps ----------------------------------------------------
T_MICRO, T_CAP, T_BODY, T_VALUE = 8, 10, 12, 15

# ---- geometry ----------------------------------------------------------------------
M = 22                                   # margin: tighter than v7, so the graph can breathe
X0, X1 = M, W - M
HEADER = (X0, 14, X1, 54)
GRAPH = (X0, 70, X1, 556)                # 496 px = 71 % of the window
TAKE_RIBBON = (X0 + 18, 494, X1 - 18, 520)
CHIPS = (X0, 572, X1, 636)
FOOTER = (X0, 646, X1, 682)

PANEL_R, GRAPH_R, CHIP_R, PILL_R, SMALL_R = 16, 14, 12, 11, 4
RADII = (PANEL_R, GRAPH_R, CHIP_R, PILL_R, SMALL_R, 2)
SEAL_T = 0.795
ROW_H = 64                                # a band chip is a target as well as a label

F_MIN, F_MAX = 30.0, 18000.0
DB_MIN, DB_MAX = -18.0, 18.0

# ---- copy, as data ------------------------------------------------------------------
TAGLINE = "بيسمع التيك كله، وإنت اللي بتوقفه"
SEAL_STATE = "مختوم"
TAKE_LABEL = "التيك"
TAKE_DONE = "اتسمع ٢٤٫٩ من ٣١٫٢ ث"
REMAINING = "متبقي ٦٫٣ ث"
STOP_MARK = "الوقفة · إنت وقفته"
MEASURED_KEY = "اللي اتقاس"
DECISION_KEY = "القرار"
BAND_MODES = ("ثابت", "ثابت", "ديناميك", "متابع النغمة")
MODE_SEGMENTS = ("ثابت", "ديناميك", "متابع")
FOOTER_SEAL = "مش بيتغيّر لوحده"
FOOTER_REPORT = "٤ باندات · واحد ديناميك · واحد متابع"
KNOB_LABELS = ("القوة", "الخروج")
CHIP_UNIT = "dB"
CHIP_HZ = "هرتز"
FOLLOWING = "متابع 600 هرتز"

ARABIC_STRINGS = (TAGLINE, SEAL_STATE, TAKE_LABEL, STOP_MARK, REMAINING, MEASURED_KEY,
                  DECISION_KEY, *BAND_MODES, *MODE_SEGMENTS, FOOTER_SEAL, FOOTER_REPORT,
                  *KNOB_LABELS, CHIP_UNIT, CHIP_HZ, FOLLOWING)

CENTRED_PILL = False
EQUAL_CARD_GRID = False
WATERMARK = None
ACCENTS = (BRASS, PATINA)

BANDS = (
    # hz, q, gain dB, mode(0 static 1 static 2 dynamic 3 tracked), live activity
    (118.0, 0.82, -3.1, 0, 0.0),
    (440.0, 1.10, -4.6, 1, 0.0),
    (3000.0, 1.00, 2.6, 2, 0.42),
    (600.0, 1.60, 3.4, 3, 0.78),
)
HPF = 88.0

# ---- the layout surface the gates read ---------------------------------------------
# A gate that reconstructs geometry from the module's constants is one edit away from
# checking something that is not drawn.  These are the boxes the panel itself draws.
EXPECTED_SIZES_V8 = (W, H)
GRAPH_SHARE = (GRAPH[3] - GRAPH[1]) / H
SEAL_FROM = "left"
SEAL_X = int(TAKE_RIBBON[0] + (TAKE_RIBBON[2] - TAKE_RIBBON[0]) * SEAL_T)
TAKE_MASS = (SEAL_X - 300, TAKE_RIBBON[1] + 8, SEAL_X - 60, TAKE_RIBBON[3] - 8)
GROUND_PROBE = (500, 14, 880, 46)
BLOCKS = {"header": HEADER, "hero": GRAPH, "chips": CHIPS, "footer": FOOTER}
KNOB_SEAT = 1.08
_KNOB_CY = (FOOTER[1] + FOOTER[3]) // 2
TARGETS = [
    ("knob-curve", (X1 - 60, _KNOB_CY - 26, X1, _KNOB_CY + 26)),
    ("knob-output", (X1 - 164, _KNOB_CY - 26, X1 - 104, _KNOB_CY + 26)),
    ("seal-clamp", (SEAL_X - 26, TAKE_RIBBON[1] - 10, SEAL_X + 26, TAKE_RIBBON[3] + 10)),
]
for _i in range(len(BANDS)):
    _gap = 10
    _cw = (CHIPS[2] - CHIPS[0] - _gap * (len(BANDS) - 1)) / len(BANDS)
    _cx0 = CHIPS[0] + _i * (_cw + _gap)
    TARGETS.append((f"band-row-{_i + 1}", (_cx0, CHIPS[1], _cx0 + _cw, CHIPS[3])))

PART_STATES = {
    "knob-curve": ("idle", "hover", "press", "disabled"),
    "knob-output": ("idle", "hover", "press", "disabled"),
    "seal-clamp": ("idle", "hover", "press", "disabled"),
    "band-row-1": ("idle", "hover", "press", "disabled"),
    "band-row-2": ("idle", "hover", "press", "disabled"),
    "band-row-3": ("idle", "hover", "press", "disabled"),
    "band-row-4": ("idle", "hover", "press", "disabled"),
}

# what got drawn (the gates ask the render instead of scraping this file)
DRAWN: dict[str, object] = {"sizes": set(), "arabic": set(), "numbers": [], "caps": set()}

_ar_text, _caps = ar_text, caps


def ar_text(d, xy, s, size=12, *a, **k):        # noqa: F811 - deliberate shadow
    DRAWN["arabic"].add(s)
    DRAWN["sizes"].add(size)
    return _ar_text(d, xy, s, size, *a, **k)


def caps(d, xy, s, size=10, *a, **k):           # noqa: F811 - deliberate shadow
    if s.strip():
        DRAWN["caps"].add(s)
        DRAWN["sizes"].add(size)
    return _caps(d, xy, s, size, *a, **k)


def report():
    return {k: (sorted(v) if isinstance(v, set) else list(v)) for k, v in DRAWN.items()}


# ------------------------------------------------------------------ measuring text
def ar_w(s: str, size: int, bold: bool = False) -> float:
    return font(FONT_B if bold else FONT_R, size).getlength(shape(s))


def caps_w(s: str, size: int, tracking: float, bold: bool = False) -> float:
    f = font(FONT_B if bold else FONT_R, size)
    return sum(f.getlength(c) + tracking for c in s) - tracking


def num(canvas, x, y, s, size=T_VALUE, col=None, anchor="ra", bold=True, tracking=1.1):
    """A readout: letterspaced so a column of numbers reads as a column."""
    DRAWN["sizes"].add(size)
    DRAWN["numbers"].append(s)
    d = ImageDraw.Draw(canvas)
    f = font(FONT_B if bold else FONT_R, size)
    w = sum(f.getlength(c) + tracking for c in s) - tracking
    if anchor[0] == "r":
        x -= w
    elif anchor[0] == "m":
        x -= w / 2
    for ch in s:
        d.text((x, y), ch, font=f, fill=col or TEXT)
        x += f.getlength(ch) + tracking
    return w


# ------------------------------------------------------------------ maths
def fx(f: float) -> float:
    return GRAPH[0] + (math.log10(f) - math.log10(F_MIN)) / (
        math.log10(F_MAX) - math.log10(F_MIN)) * (GRAPH[2] - GRAPH[0])


def dy(db: float) -> float:
    return GRAPH[3] - (db - DB_MIN) / (DB_MAX - DB_MIN) * (GRAPH[3] - GRAPH[1])


def correction_db(f: float, bands=BANDS, hpf: float = HPF) -> float:
    v = hpf_db(f, hpf) if hpf > 0 else 0.0
    for bf, q, g, _m, _a in bands:
        v += peak_db(f, bf, q, g)
    return v


# ------------------------------------------------------------------ the ground
_GRAIN: Image.Image | None = None


def grain(size) -> Image.Image:
    """A 3 % film grain.  Nothing in this category is a photograph of fabric; what
    keeps a flat field from banding on a cheap panel is noise, not texture."""
    global _GRAIN
    if _GRAIN is None or _GRAIN.size != size:
        rnd = random.Random(3)
        n = Image.new("L", (size[0], size[1]))
        px = n.load()
        for y in range(size[1]):
            for x in range(size[0]):
                px[x, y] = 128 + rnd.randint(-26, 26)
        _GRAIN = n
    a = Image.new("RGBA", size, (0, 0, 0, 0))
    a.putalpha(_GRAIN.point(lambda v: max(0, int((v - 128) * 0.34))))
    return a


def ground() -> Image.Image:
    canvas = Image.new("RGBA", (W, H), INK_BOT + (255,))
    grad = Image.new("RGBA", (1, H))
    for y in range(H):
        grad.putpixel((0, y), mix(INK_TOP, INK_BOT, (y / (H - 1)) ** 0.85) + (255,))
    canvas.alpha_composite(grad.resize((W, H), Image.BILINEAR))

    # two enormous, very soft blooms - the accent's light in the room
    for (cxr, cyr, r, col, a) in ((0.14, -0.16, 1.15, BRASS, 20), (0.94, 1.10, 1.05, PATINA, 15)):
        rr = int(max(W, H) * r)
        g = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([cxr * W - rr, cyr * H - rr, cxr * W + rr, cyr * H + rr],
                                  fill=col + (a,))
        canvas.alpha_composite(g.filter(ImageFilter.GaussianBlur(rr / 2.6)))
    canvas.alpha_composite(grain((W, H)))
    return canvas


# ------------------------------------------------------------------ parts
def knob_small(canvas, cx, cy, size, value, brass=True):
    body = sprite("knob_brass.png" if brass else "knob_body.png", size)
    seat = int(size * 1.08)
    boss = Image.new("RGBA", (seat, seat), (0, 0, 0, 0))
    ImageDraw.Draw(boss).ellipse([0, 0, seat - 1, seat - 1], fill=(3, 3, 4, 240))
    canvas.alpha_composite(boss.filter(ImageFilter.GaussianBlur(1.2)),
                           (int(cx - seat / 2), int(cy - seat / 2)))
    over(canvas, lambda d: (d.arc([cx - seat / 2, cy - seat / 2, cx + seat / 2, cy + seat / 2],
                                  200, 340, fill=(255, 236, 205, 30), width=1),
                            d.arc([cx - seat / 2, cy - seat / 2, cx + seat / 2, cy + seat / 2],
                                  20, 160, fill=(0, 0, 0, 150), width=2)))
    sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", (size, size), (0, 0, 0, 170)), (0, 0),
             body.getchannel("A").point(lambda v: int(v * 0.85)))
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(max(2, size // 20))),
                           (int(cx - size / 2), int(cy - size / 2 + size // 22)))
    canvas.alpha_composite(body, (int(cx - size / 2), int(cy - size / 2)))
    needle(canvas, cx, cy, size, value)


def needle(canvas, cx, cy, size, value):
    r = size / 2
    ss = 4
    n = int(size * ss)
    lay = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    c = n / 2
    a = knob_angle(value)
    dx, dy_ = math.sin(a), -math.cos(a)
    p0 = (c + dx * r * 0.04, c + dy_ * r * 0.04)
    p1 = (c + dx * r * 0.64, c + dy_ * r * 0.64)
    wd = r * 0.07 * ss

    def capsule(q0, q1, wid, fill):
        d.line([q0, q1], fill=fill, width=int(wid))
        for q in (q0, q1):
            d.ellipse([q[0] - wid / 2, q[1] - wid / 2, q[0] + wid / 2, q[1] + wid / 2], fill=fill)

    capsule(p0, p1, wd + 2.0 * ss, (16, 11, 5, 180))
    capsule(p0, p1, wd, BRASS_HOT + (255,))
    canvas.alpha_composite(lay.resize((int(size), int(size)), Image.LANCZOS),
                           (int(cx - r), int(cy - r)))


def pill(canvas, box, label, size=T_CAP, fill=(255, 255, 255, 14), line=(255, 255, 255, 30),
         text_col=DIM, pad=10, bold=False):
    x0, y0, x1, y1 = box
    over(canvas, lambda d: d.rounded_rectangle(box, radius=PILL_R, fill=fill))
    over(canvas, lambda d: (d.rounded_rectangle(box, radius=PILL_R, outline=line),
                            d.line([(x0 + PILL_R, y0 + 1), (x1 - PILL_R, y0 + 1)],
                                   fill=(255, 255, 255, 22))))
    d = ImageDraw.Draw(canvas)
    ar_text(d, ((x0 + x1) / 2, (y0 + y1) / 2 - size * 0.66), label, size, text_col,
            bold=bold, anchor="ma")
    return x1 - x0


def led(canvas, cx, cy, r=3, on=True):
    if on:
        glow(canvas, lambda d: d.ellipse([cx - r * 3.2, cy - r * 3.2, cx + r * 3.2, cy + r * 3.2],
                                         fill=BRASS), blur=7, alpha=190)
    over(canvas, lambda d: d.ellipse([cx - r, cy - r, cx + r, cy + r],
                                     fill=(BRASS_HOT if on else (74, 70, 64)) + (255,)))


# ------------------------------------------------------------------ header
def draw_header(canvas):
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = HEADER
    cy = (y0 + y1) / 2

    # wordmark: light weight, wide tracking, left.  No pill in the middle (G50).
    caps(d, (x0, cy - 7), "AEQ", T_BODY, BRASS_HOT, 3.4, bold=True)
    caps(d, (x0 + caps_w("AEQ", T_BODY, 3.4, True) + 8, cy - 7), "RESTORE", T_BODY,
         (255, 245, 230, 120), 3.4)

    # state: a pill at the right edge, and the seal LED
    label_w = ar_w(SEAL_STATE, T_CAP) + 40
    pill(canvas, (x1 - label_w, cy - 11, x1, cy + 11), SEAL_STATE, T_CAP,
         fill=(214, 172, 96, 26), line=(214, 172, 96, 90), text_col=BRASS_HOT)
    led(canvas, x1 - label_w + 12, cy, 3)

    # the seal never changes by itself: said once, at the seal
    ar_text(d, (x1, cy + 14), FOOTER_SEAL, T_MICRO, (146, 138, 126), anchor="ra")

    # tagline under the wordmark: the one line that says what this is
    ar_text(d, (x0, y1 - 9), TAGLINE, T_BODY, (158, 150, 137))


# ------------------------------------------------------------------ the graph
def analyser(seed=5):
    rnd = random.Random(seed)

    def env(f):
        db = -1.0 * math.log2(max(f, 40.0) / 700.0)
        for fc, bw, amp in ((700, 220, 11.0), (1900, 430, 7.4), (3100, 760, 4.0),
                            (150, 260, -2.4)):
            db += amp / (1.0 + ((f - fc) / bw) ** 2)
        db -= 26.0 / (1.0 + (6200.0 / max(f, 60.0)) ** 2)
        db -= 10.0 / (1.0 + (f / 62.0) ** 2)
        return db

    pts, f, i = [], F_MIN, 0
    while f < F_MAX:
        v = env(f) + (-2.6 if i % 3 == 0 else rnd.uniform(-0.45, 0.8)) + 0.2 * math.sin(i / 7.0)
        pts.append((f, v))
        f *= 2 ** (1 / 12)
        i += 1
    lo = min(v for _, v in pts)
    hi = max(v for _, v in pts)
    return [(f, dy(-7.0 + (v - lo) / max(1e-6, hi - lo) * 17.5)) for f, v in pts]


def pitch_trail(n=110):
    rnd = random.Random(11)
    out, f = [], 600.0
    for i in range(n):
        target = 600.0 * (1 + 0.28 * math.sin(i / 11.0) + 0.09 * math.sin(i / 3.4 + 1.2))
        f += (target - f) * 0.2
        out.append(f * (1 + rnd.uniform(-0.01, 0.01)))
    return out


def draw_graph(canvas):
    """One surface, bled to the margins: no frame, no title bar, no inner bezel.

    Everything inside is drawn on its own layer and clipped to the well.  An EQ
    curve at a 24 dB/oct high-pass genuinely leaves the axis below 60 Hz; if it is
    not clipped, the bass corner draws a diagonal across the whole panel and the
    graph reads as broken rather than as filtered.
    """
    x0, y0, x1, y1 = GRAPH
    size = (x1 - x0, y1 - y0)
    plate = Image.new("RGBA", size, (10, 10, 12, 214))
    plate.putalpha(rounded_mask(size, GRAPH_R))
    canvas.alpha_composite(plate, (x0, y0))
    over(canvas, lambda d: (d.rounded_rectangle([x0, y0, x1, y1], radius=GRAPH_R,
                                                outline=(255, 245, 230, 26)),
                            d.line([(x0 + GRAPH_R, y0 + 1), (x1 - GRAPH_R, y0 + 1)],
                                   fill=(255, 245, 230, 34))))

    top = y0 + 26
    bot = TAKE_RIBBON[1] - 26

    # the layer everything lives on, so one mask clips grid, spectrum, trail and curve
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw_graph_content(layer, top, bot)
    m = Image.new("L", (W, H), 0)
    # the plot ends above the take band: the well keeps a footer strip for the
    # ribbon and its caption, and nothing drawn as data may enter it
    plot = Image.new("L", size, 0)
    ImageDraw.Draw(plot).rectangle([0, 0, size[0], bot + 10 - y0], fill=255)
    m.paste(ImageChops.multiply(rounded_mask(size, GRAPH_R), plot), (x0, y0))
    layer.putalpha(Image.composite(layer.getchannel("A"), Image.new("L", (W, H), 0), m))
    canvas.alpha_composite(layer)

    # legends stay *outside* the clip: they are chrome, not data
    d = ImageDraw.Draw(canvas)
    rx = x1 - 16
    for label, col in ((DECISION_KEY, BRASS), (MEASURED_KEY, PATINA)):
        ky = TAKE_RIBBON[3] + 10
        over(canvas, lambda dd, rx=rx: dd.ellipse([rx - 4, ky + 4, rx + 4, ky + 12],
                                                  fill=col + (230,)))
        d = ImageDraw.Draw(canvas)
        ar_text(d, (rx - 12, ky), label, T_CAP, (150, 142, 130), anchor="ra")
        rx -= ar_w(label, T_CAP) + 36


def draw_graph_content(canvas, top, bot):
    x0, y0, x1, y1 = GRAPH
    size = (x1 - x0, y1 - y0)

    def grid(d):
        f = F_MIN
        while f <= F_MAX:
            x = fx(f)
            decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
            d.line([(x, top), (x, bot)], fill=(255, 245, 230, 16 if decade else 7))
            f *= 2 ** (1 / 3)
        for db in (-12, -6, 0, 6, 12):
            y = y0 + 26 + (y1 - y0 - 26) * (DB_MAX - db) / (DB_MAX - DB_MIN) * 0.86
            d.line([(x0 + 12, y), (x1 - 12, y)], fill=(255, 245, 230, 14 if db == 0 else 6))
            if db:
                caps(d, (x0 + 16, y - 9), f"{db:+d}", T_MICRO, (255, 245, 230, 84), 0.8)
        for f, lab in ((31.5, "31"), (63, "63"), (125, "125"), (250, "250"), (500, "500"),
                       (1000, "1k"), (2000, "2k"), (4000, "4k"), (8000, "8k"), (16000, "16k")):
            x = fx(f)
            caps(d, (x, y0 + 8), lab, T_MICRO, (255, 245, 230, 108), 1.1, anchor="ma")
    over(canvas, grid)

    # one soft bloom inside the well, tinted by whichever accent owns that side
    for (cxr, cyr, r, col, a) in ((0.28, 0.62, 0.75, PATINA, 16), (0.72, 0.36, 0.7, BRASS, 12)):
        rr = int(max(size) * r)
        g = Image.new("RGBA", size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([cxr * size[0] - rr, cyr * size[1] - rr,
                                   cxr * size[0] + rr, cyr * size[1] + rr], fill=col + (a,))
        canvas.alpha_composite(g.filter(ImageFilter.GaussianBlur(rr / 2.2)), (x0, y0))

    # ---- the measurement: patina, translucent, spiky, fading into the floor
    pts = analyser()
    mask = Image.new("L", size, 0)
    poly = [(fx(f) - x0, y - y0) for f, y in pts] + [(size[0], bot - y0), (0, bot - y0)]
    ImageDraw.Draw(mask).polygon(poly, fill=255)
    grad = Image.new("RGBA", size)
    gp = grad.load()
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        col = mix((150, 214, 190), (26, 54, 48), min(1.0, t * 1.05))
        a = int(120 * max(0.0, 1.0 - t) ** 1.3) + 22
        for x in range(size[0]):
            gp[x, y] = col + (a,)
    spec = Image.new("RGBA", size, (0, 0, 0, 0))
    spec.paste(grad, (0, 0), mask)
    canvas.alpha_composite(spec, (x0, y0))
    edge = [(fx(f), y) for f, y in pts]
    glow(canvas, lambda d: d.line(edge, fill=PATINA + (120,), width=2, joint="curve"), blur=7,
         alpha=110)
    over(canvas, lambda d: d.line(edge, fill=PATINA_HOT + (230,), width=1, joint="curve"))

    # ---- the note's trail: dotted, because it is a record
    trail = pitch_trail()
    ty0, ty1 = y0 + 60, bot - 18
    for i, f in enumerate(trail):
        tx = fx(f)
        ty = ty1 - (i / max(1, len(trail) - 1)) * (ty1 - ty0)
        r = 1.0 if i % 3 else 1.6
        over(canvas, lambda d, tx=tx, ty=ty, r=r, i=i:
             d.ellipse([tx - r, ty - r, tx + r, ty + r],
                       fill=PATINA + (30 + int(110 * i / len(trail)),)))

    # ---- the decision: brass, thin, glowing, on top of the measurement
    cpts = []
    for i in range(721):
        f = F_MIN * (F_MAX / F_MIN) ** (i / 720)
        cpts.append((fx(f), dy(max(DB_MIN, min(DB_MAX, correction_db(f))))))
    glow(canvas, lambda d: d.line(cpts, fill=BRASS + (190,), width=4, joint="curve"), blur=16,
         alpha=180)
    glow(canvas, lambda d: d.line(cpts, fill=BRASS_HOT + (140,), width=2, joint="curve"),
         blur=3, alpha=160)
    over(canvas, lambda d: d.line(cpts, fill=(255, 238, 205, 245), width=1, joint="curve"))

    # ---- bands: rings, and the tracked one as a lens with its own halo
    d = ImageDraw.Draw(canvas)
    for bf, q, g, mode, live in BANDS:
        x, y = fx(bf), dy(correction_db(bf))
        if mode == 3:
            hw = 9 + 7 * live
            glow(canvas, lambda dd, x=x, y=y, hw=hw:
                 dd.ellipse([x - hw, y - hw * 2.3, x + hw, y + hw * 2.3], fill=BRASS),
                 blur=14, alpha=170)
            d = ImageDraw.Draw(canvas)
            d.rounded_rectangle([x - hw, y - hw * 2.3, x + hw, y + hw * 2.3], radius=int(hw),
                                outline=BRASS_HOT, width=1)
            over(canvas, lambda dd, x=x: dd.line([(x, top), (x, bot)], fill=BRASS + (46,)))
            d = ImageDraw.Draw(canvas)
            d.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5], fill=BRASS_HOT)
            caps(d, (x + hw + 10, y - hw * 2.3 - 6), "TRACKED 600 Hz", T_MICRO,
                 (228, 176, 86), 1.3)
        else:
            glow(canvas, lambda dd, x=x, y=y: dd.ellipse([x - 9, y - 9, x + 9, y + 9],
                                                         fill=BRASS), blur=10, alpha=120)
            d = ImageDraw.Draw(canvas)
            d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(16, 14, 12), outline=BRASS, width=2)
            d.ellipse([x - 2.4, y - 2.4, x + 2.4, y + 2.4], fill=BRASS_HOT)
            if live > 0.05:
                over(canvas, lambda dd, x=x, y=y, live=live:
                     dd.ellipse([x - 4 - 5 * live, y - 3, x + 4 + 5 * live, y + 3],
                                fill=PATINA + (210,)))



# ------------------------------------------------------------------ the take ribbon
def draw_take(canvas):
    """The story, inside the hero: it fills from the right as it learns, the clamp
    lands where the person stopped it, and after that nothing moves."""
    x0, y0, x1, y1 = TAKE_RIBBON
    d = ImageDraw.Draw(canvas)

    over(canvas, lambda dd: dd.rounded_rectangle([x0, y0, x1, y1], radius=SMALL_R,
                                                 fill=(255, 255, 255, 10)))
    # the take reads left to right, so the learnt part is the left 79.5 % and the
    # clamp sits exactly where it was stopped. Nothing is invented past the clamp.
    seal_x = x0 + (x1 - x0) * SEAL_T

    mask = Image.new("L", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(mask).rectangle([0, 0, seal_x - x0, y1 - y0], fill=255)
    span = max(1.0, seal_x - x0)
    grad = Image.new("RGBA", (x1 - x0, y1 - y0))
    gp = grad.load()
    for x in range(x1 - x0):
        t = min(1.0, x / span) ** 0.72
        col = mix((34, 62, 56), (110, 172, 152), t)
        for y in range(y1 - y0):
            gp[x, y] = col + (int(84 + 128 * t),)
    band = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    band.paste(grad, (0, 0), mask)
    canvas.alpha_composite(band, (x0, y0))

    over(canvas, lambda dd: (dd.line([(seal_x, y0 - 2), (seal_x, y1 + 2)],
                                     fill=(255, 246, 232, 40), width=6),
                             dd.line([(seal_x, y0), (seal_x, y1)], fill=PATINA_HOT + (235,), width=2)))

    # the clamp: the real machined part, vertical, sitting on the seal
    cl = Image.open(ASSETS / "clamp.png").convert("RGBA").rotate(90, expand=True)
    ch = 44
    cw = max(1, int(cl.size[0] * ch / cl.size[1]))
    cl = cl.resize((cw, ch), Image.LANCZOS)
    cxx, cyy = int(seal_x - cw / 2), int((y0 + y1) / 2 - ch / 2)
    shadow = Image.new("RGBA", cl.size, (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", cl.size, (0, 0, 0, 190)), (0, 0), cl.getchannel("A"))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(6)), (cxx + 3, cyy + 4))
    canvas.alpha_composite(cl, (cxx, cyy))

    d = ImageDraw.Draw(canvas)
    # one strip under the ribbon: learned (left), the stop (centre), the keys (right).
    # Nothing is stacked, and nothing sits over the clamp itself.
    # under the band: what was learnt (left, Arabic) and the keys (right).
    caps(d, (x0, y1 + 10), "LEARNED 24.9 s · 96 BANDS · 15 SONGS · 48 kHz", T_MICRO,
         (150, 142, 130), 1.1)
    # the seal itself is labelled: above the band, where nothing else lives
    ar_text(d, (seal_x, y0 - 20), STOP_MARK, T_MICRO, (228, 176, 86), anchor="ma")


def draw_chips(canvas):
    """The ledger as four chips instead of a four-row table: same information, a third
    of the ink, and the tracked one is allowed to stand out."""
    x0, y0, x1, y1 = CHIPS
    gap = 10
    n = len(BANDS)
    cw = (x1 - x0 - gap * (n - 1)) / n
    for i, (bf, q, g, mode, live) in enumerate(BANDS):
        cx0 = x0 + i * (cw + gap)
        cx1 = cx0 + cw
        hot = mode == 3
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.rounded_rectangle(
            [cx0, y0, cx1, y1], radius=CHIP_R, fill=(22, 20, 17, 232)))
        if hot:
            glow(canvas, lambda dd, cx0=cx0, cx1=cx1, y0=y0, y1=y1: dd.rounded_rectangle(
                [cx0, y0, cx1, y1], radius=CHIP_R, fill=BRASS), blur=14, alpha=22)
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.rounded_rectangle(
            [cx0, y0, cx1, y1], radius=CHIP_R,
            outline=(BRASS + (150,) if hot else (255, 245, 230, 30))))
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.line(
            [(cx0 + 10, y0 + 1), (cx1 - 10, y0 + 1)], fill=(255, 245, 230, 26)))

        d = ImageDraw.Draw(canvas)
        caps(d, (cx0 + 12, y0 + 9), f"0{i + 1}", T_MICRO, BRASS_HOT if hot else FAINT, 1.3)
        ar_text(d, (cx1 - 12, y0 + 6), BAND_MODES[mode], T_CAP,
                BRASS_HOT if hot else (PATINA if mode == 2 else DIM), anchor="ra")
        w = num(canvas, cx0 + 12, y0 + 22, f"{g:+.1f}", T_VALUE, TEXT, anchor="la")
        d = ImageDraw.Draw(canvas)
        caps(d, (cx0 + 15 + w, y0 + 28), CHIP_UNIT, T_MICRO, (150, 142, 130), 0.3)
        caps(d, (cx0 + 12, y0 + 42), f"{bf:.0f} Hz · Q {q:.2f}", T_MICRO, FAINT, 0.9)
        if live > 0.02:
            over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.rounded_rectangle(
                [cx0 + 12, y1 - 9, cx0 + 12 + (cx1 - cx0 - 24) * live, y1 - 7], radius=2,
                fill=PATINA + (200,)))


# ------------------------------------------------------------------ footer
def draw_footer(canvas):
    x0, y0, x1, y1 = FOOTER
    d = ImageDraw.Draw(canvas)
    cy = (y0 + y1) / 2

    # segmented mode control: one pill, three segments - the modern replacement for
    # three checkboxes and, in this product, the honest state of the band layer
    seg_w, seg_h = 66, 24
    total = seg_w * 3 + 8
    sx = x0
    over(canvas, lambda dd: dd.rounded_rectangle([sx, cy - seg_h / 2, sx + total, cy + seg_h / 2],
                                                 radius=PILL_R, fill=(255, 255, 255, 10)))
    over(canvas, lambda dd: dd.rounded_rectangle([sx + 4 + seg_w * 2, cy - seg_h / 2 + 3,
                                                  sx + 4 + seg_w * 3, cy + seg_h / 2 - 3],
                                                 radius=PILL_R - 3, fill=(214, 172, 96, 34)))
    over(canvas, lambda dd: dd.rounded_rectangle([sx + 4 + seg_w * 2, cy - seg_h / 2 + 3,
                                                  sx + 4 + seg_w * 3, cy + seg_h / 2 - 3],
                                                 radius=PILL_R - 3, outline=(BRASS + (170,))))
    d = ImageDraw.Draw(canvas)
    for i, label in enumerate(MODE_SEGMENTS):
        ar_text(d, (sx + 4 + seg_w * i + seg_w / 2, cy - 8), label, T_CAP,
                BRASS_HOT if i == 2 else (150, 142, 130), anchor="ma")

    # the report, one Arabic sentence, and the seal state
    ar_text(d, (sx + total + 28, cy - 7), FOOTER_REPORT, T_BODY, (182, 174, 160))

    # two knobs: the only values the operator turns by hand
    for i, (val, label) in enumerate(((0.62, KNOB_LABELS[0]), (0.5, KNOB_LABELS[1]))):
        cx = x1 - 30 - i * 104
        knob_small(canvas, cx, cy, 42, val)
        d = ImageDraw.Draw(canvas)
        ar_text(d, (cx - 34, cy - 12), label, T_CAP, (150, 142, 130), anchor="ra")
        num(canvas, cx - 34, cy + 2, f"{val * 100:.0f}%", T_BODY, TEXT)


# ------------------------------------------------------------------ build
def build():
    canvas = ground()
    mask = rounded_mask((W, H), PANEL_R)

    page = Image.new("RGBA", (W, H), (5, 5, 6, 255))
    shadow = Image.new("RGBA", (W + 80, H + 80), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", (W, H), (0, 0, 0, 220)), (40, 40), mask)
    page.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(26)).crop((40, 40, 40 + W, 40 + H)))
    page.alpha_composite(canvas, (0, 0))
    canvas = page
    canvas.putalpha(mask)
    canvas.alpha_composite(inner_shadow((W, H), PANEL_R, 12, 120))
    over(canvas, lambda d: (d.rounded_rectangle([0, 0, W - 1, H - 1], radius=PANEL_R,
                                                outline=(255, 245, 230, 30)),
                            d.line([(PANEL_R, 1), (W - PANEL_R, 1)], fill=(255, 245, 230, 44))))

    draw_header(canvas)
    draw_graph(canvas)
    draw_take(canvas)
    draw_chips(canvas)
    draw_footer(canvas)

    canvas.putalpha(mask)
    canvas.save(OUT)
    print(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(build())
