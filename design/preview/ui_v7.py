#!/usr/bin/env python3
"""ui_v7.py - the panel as the product, not as the store.

v5 and v6 were a *reproduction*.  The user said what he actually wanted and it was
not subtle: "كنت اقصد الهوية البصرية و التحضر في التصميم مش اقصد نسخة" - the visual
identity and the crafted quality, not a copy.  And the question underneath it was
sharper: does this thing understand what the plugin is for?

It does: **AEQ Restore is about time and trust.**  It listens to the whole take
(bypass, bit-exact), *you* stop it, then it commits - and after the commit it sits
still.  A panel built out of four equal cards and a centred pill says "dashboard".
This one is built out of the three things that story actually has:

    the take     a bar that fills as it listens, and a machined clamp that seals it
    the decision a brass curve over a patina spectrum, in one shared well
    the record   a ledger of bands - static, dynamic, and the one that follows the note

The world is design/IDENTITY_AR.md: **brass = a decision, patina = a measurement**,
on warm lacquer black.  Those two hues never swap, and no third accent is allowed in
(which is also why the old blue is gone - the gate measures that).

Everything physical is still a render, not a drawing: the brass knob, the steel knob,
the clamp and the plate are materials (design/assets/src -> assets_prep.py).  Only
what must move with the DSP is drawn: the needles, the curve, the spectrum, the trail.

    /tmp/venv/bin/python design/preview/ui_v7.py
"""
from __future__ import annotations

import math
import pathlib
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

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
OUT = HERE / "ui_v7_1180x700.png"

# ---- the world (design/IDENTITY_AR.md) ----------------------------------------------
INK = (10, 9, 8)
TEXT = (242, 237, 227)
DIM = (167, 158, 144)
FAINT = (126, 118, 106)
BRASS = (201, 161, 90)
BRASS_HOT = (242, 214, 162)
PATINA = (111, 168, 147)
PATINA_DEEP = (44, 82, 71)

# One type scale, four steps, named by rank.  A size that is not one of these does
# not ship: three "1 px apart" sizes is how a panel starts looking assembled.
T_MICRO, T_CAP, T_BODY, T_VALUE = 8, 10, 12, 14

M = 40                                   # side margin
X0, X1 = M, W - M                        # 40 .. 1140

# ---- the grid: every block on it, and check_identity.py measures it ----------------
HEADER = (X0, 18, X1, 60)
TAKE = (X0, 68, X1, 168)
TAKE_BAR = (X0, 104, X1, 132)
WELL = (X0, 194, X1, 448)
LEDGER = (X0, 462, X1, 610)
FOOTER = (X0, 620, X1, 694)
ROW_H = 30                             # ledger row pitch (>= 30: it is a target)
SEAL_T = 0.795                           # where the person stopped it, of the whole take

F_MIN, F_MAX = 30.0, 18000.0
DB_MIN, DB_MAX = -18.0, 18.0

# ---- the copy, as data (the font gate reads exactly this) ---------------------------
TAGLINE = "بيسمع التيك كله، وإنت اللي بتوقفه"
SEAL_STATE = "مختوم"
TAKE_LABEL = "التيك"
TAKE_META = "تيك ٠١ · ٤٨ كيلو · ٢٣:٤١"
TAKE_DONE = "اتسمع ٢٤٫٩ من ٣١٫٢ ثانية"
STOP_MARK = "الوقفة · إنت وقفته"
MEASURED_KEY = "اللي اتقاس"
DECISION_KEY = "القرار"
LEDGER_COLS = (("الباند", ""), ("الوضع", ""), ("المكسب", "dB"), ("العرض", "Q"),
               ("التردد", "Hz"), ("الحركة", ""))
BAND_MODES = ("ثابت", "ثابت", "ديناميك", "متابع النغمة")
FOOTER_SEAL = "مش بيتغيّر لوحده"
FOOTER_REPORT = "اتظبط بعد ٢٤٫٩ ثانية · ٤ باندات · واحد ديناميك وواحد متابع"
KNOB_LABELS = ("القوة", "الخروج")

# Every part the panel draws and the four states it has to answer.  A still cannot
# show hover/press, so the module declares them and the craft gate (G51) checks the
# declaration covers every target the panel actually draws.  When the panel is
# ported to JUCE these become the Component::paint branches, one per state.
PART_STATES = {
    "knob-curve": ("idle", "hover", "press", "disabled"),
    "knob-output": ("idle", "hover", "press", "disabled"),
    "seal-clamp": ("idle", "hover", "press", "disabled"),
    "band-row-1": ("idle", "hover", "press", "disabled"),
    "band-row-2": ("idle", "hover", "press", "disabled"),
    "band-row-3": ("idle", "hover", "press", "disabled"),
    "band-row-4": ("idle", "hover", "press", "disabled"),
}
LEDGER_LIVE = "الحركة دلوقتي"

ARABIC_STRINGS = (TAGLINE, SEAL_STATE, TAKE_LABEL, TAKE_META, TAKE_DONE, STOP_MARK,
                  MEASURED_KEY, DECISION_KEY, *(c[0] for c in LEDGER_COLS), *BAND_MODES,
                  FOOTER_SEAL, FOOTER_REPORT, *KNOB_LABELS, LEDGER_LIVE)

# the market cues this panel deliberately does NOT carry (G50 asserts it in pixels)
CENTRED_PILL = False
EQUAL_CARD_GRID = False
WATERMARK = None
ACCENTS = (BRASS, PATINA)                # no third accent is allowed in

BANDS = (
    # hz, q, gain dB, mode(0 static 1 static 2 dynamic 3 tracked), live activity
    (118.0, 0.82, -3.1, 0, 0.0),
    (440.0, 1.10, -4.6, 1, 0.0),
    (3000.0, 1.00, 2.6, 2, 0.42),
    (600.0, 1.60, 3.4, 3, 0.78),
)
HPF = 88.0


# ------------------------------------------------------------------ measuring text
def ar_w(s: str, size: int, bold: bool = False) -> float:
    """Width of a shaped Arabic string, measured the same way it is drawn."""
    from PIL import ImageFont
    f = font(FONT_B if bold else FONT_R, size)
    return f.getlength(shape(s))


def num(canvas, x, y, s, size=13, col=None, anchor="ra", bold=True, tracking=1.3):
    """Tabular readout: Cairo has no monospace, so tracking is what keeps a column of
    numbers reading as a column instead of as ragged text."""
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


def text_w(s: str, size: int, bold: bool = True, tracking: float = 1.4) -> float:
    f = font(FONT_B if bold else FONT_R, size)
    return sum(f.getlength(c) + tracking for c in s) - tracking


# ------------------------------------------------------------------ small helpers
# ---- what actually got drawn -------------------------------------------------------
# The gates used to scrape this file with regexes, which quietly matched nothing and
# left three checks vacuous.  Recording at the call site means a gate can ask the
# render what it drew: sizes, Arabic strings, every live number.
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


def fx(f: float) -> float:
    """Frequency axis.  Deliberately left-to-right: every engineer, Arabic-speaking
    included, learned spectra that way, and the identity is in the language and the
    materials - not in making a familiar axis unreadable."""
    return WELL[0] + (math.log10(f) - math.log10(F_MIN)) / (
        math.log10(F_MAX) - math.log10(F_MIN)) * (WELL[2] - WELL[0])


def dy(db: float) -> float:
    return WELL[3] - (db - DB_MIN) / (DB_MAX - DB_MIN) * (WELL[3] - WELL[1])


def correction_db(f: float, bands=BANDS, hpf: float = HPF) -> float:
    v = hpf_db(f, hpf) if hpf > 0 else 0.0
    for bf, q, g, _m, _a in bands:
        v += peak_db(f, bf, q, g)
    return v


def hairline(canvas, x0, y, x1, a=24):
    over(canvas, lambda d: d.line([(x0, y), (x1, y)], fill=(255, 245, 230, a), width=1))


def knob(canvas, cx, cy, size, value, brass=True):
    """A real machined knob in its recess.  Brass when the value is a decision the
    product owns; steel when it is an operator preference."""
    body = sprite("knob_brass.png" if brass else "knob_body.png", size)
    rec = int(size * 1.08)
    boss = Image.new("RGBA", (rec, rec), (0, 0, 0, 0))
    ImageDraw.Draw(boss).ellipse([0, 0, rec - 1, rec - 1], fill=(4, 3, 3, 246))
    canvas.alpha_composite(boss.filter(ImageFilter.GaussianBlur(1.4)),
                           (int(cx - rec / 2), int(cy - rec / 2)))
    hb = [cx - rec / 2, cy - rec / 2, cx + rec / 2, cy + rec / 2]
    over(canvas, lambda d: (d.arc(hb, 198, 342, fill=(255, 236, 205, 34), width=2),
                            d.arc(hb, 18, 162, fill=(0, 0, 0, 170), width=3)))
    sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", (size, size), (0, 0, 0, 185)), (0, 0),
             body.getchannel("A").point(lambda v: int(v * 0.9)))
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(max(3, size // 24))),
                           (int(cx - size / 2), int(cy - size / 2 + size // 26)))
    canvas.alpha_composite(body, (int(cx - size / 2), int(cy - size / 2)))
    needle(canvas, cx, cy, size, value)
    return canvas


def needle(canvas, cx, cy, size, value):
    """The indicator, drawn (a rotated 3 px sprite turns to mush) and supersampled."""
    r = size / 2
    ss = 4
    n = int(size * ss)
    lay = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    c = n / 2
    a = knob_angle(value)
    cdx, cdy = math.sin(a), -math.cos(a)
    px, py = -cdy, cdx
    p0 = (c + cdx * r * 0.02, c + cdy * r * 0.02)
    p1 = (c + cdx * r * 0.66, c + cdy * r * 0.66)
    wd = r * 0.052 * ss

    def capsule(q0, q1, wid, fill):
        d.line([q0, q1], fill=fill, width=int(wid))
        for q in (q0, q1):
            d.ellipse([q[0] - wid / 2, q[1] - wid / 2, q[0] + wid / 2, q[1] + wid / 2], fill=fill)

    capsule((p0[0] + px * 0.4, p0[1] + py * 0.4), p1, wd + 2.6 * ss, (18, 12, 5, 170))
    capsule(p0, p1, wd, BRASS + (255,))
    capsule(p0, (p1[0] - cdx * 1.6 * ss, p1[1] - cdy * 1.6 * ss), wd * 0.42,
            BRASS_HOT + (240,))
    canvas.alpha_composite(lay.resize((int(size), int(size)), Image.LANCZOS),
                           (int(cx - r), int(cy - r)))


def led(canvas, cx, cy, r=4, on=True):
    if on:
        glow(canvas, lambda d: d.ellipse([cx - r * 2.6, cy - r * 2.6, cx + r * 2.6, cy + r * 2.6],
                                         fill=BRASS), blur=6, alpha=180)
    over(canvas, lambda d: d.ellipse([cx - r, cy - r, cx + r, cy + r],
                                     fill=(BRASS_HOT if on else (86, 80, 72)) + (255,)))


# ------------------------------------------------------------------ the take bar
def draw_take(canvas):
    """Time reads right-to-left: it fills from the right as it learns, and the clamp
    lands exactly where the person stopped it - the one place with no number."""
    x0, y0, x1, y1 = TAKE_BAR
    d = ImageDraw.Draw(canvas)

    # row above the bar: what this is (right), how much has been heard (left)
    ar_text(d, (x1, TAKE[1] - 1), TAKE_LABEL, T_VALUE, TEXT, anchor="ra")
    ar_text(d, (x0, TAKE[1] + 1), TAKE_DONE, T_BODY, PATINA)

    # the groove
    over(canvas, lambda dd: dd.rounded_rectangle([x0, y0, x1, y1], radius=6,
                                                 fill=(6, 5, 5, 240)))
    canvas.alpha_composite(inner_shadow((x1 - x0, y1 - y0), 6, 6, 150), (x0, y0))

    seal_x = x1 - (x1 - x0) * SEAL_T

    # the learned mass: patina, hatched, so it reads as a measurement not a progress bar
    mask = Image.new("L", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(mask).rectangle([seal_x - x0, 0, x1 - x0, y1 - y0], fill=255)
    grad = Image.new("RGBA", (x1 - x0, y1 - y0))
    gp = grad.load()
    for x in range(x1 - x0):
        t = (x - (seal_x - x0)) / max(1.0, (x1 - seal_x))
        col = mix(PATINA_DEEP, PATINA, min(1.0, t * 1.35))
        for y in range(y1 - y0):
            gp[x, y] = col + (int(110 + 110 * min(1.0, t * 1.35)),)
    band = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    band.paste(grad, (0, 0), mask)
    canvas.alpha_composite(band, (x0, y0))
    over(canvas, lambda dd: [dd.line([(x, y0 + 3), (x, y1 - 3)], fill=(10, 20, 17, 80))
                             for x in range(int(seal_x) + 4, x1, 7)])
    over(canvas, lambda dd: dd.line([(seal_x, y0), (seal_x, y1)], fill=PATINA + (235,), width=2))

    # ticks: four stops of ten seconds, and no label on the seal itself (the clamp is it)
    for t, lab in ((0.0, "٠"), (0.32, "١٠"), (0.64, "٢٠"), (0.96, "٣٠")):
        tx = x1 - (x1 - x0) * t
        over(canvas, lambda dd, tx=tx: dd.line([(tx, y1 + 3), (tx, y1 + 9)],
                                               fill=(255, 245, 230, 55)))
        ar_text(d, (tx, y1 + 12), lab, T_CAP, FAINT, anchor="ma")

    # the clamp: a real machined part, vertical, on the seal point
    cl = Image.open(ASSETS / "clamp.png").convert("RGBA").rotate(90, expand=True)
    ch = 58
    cw = max(1, int(cl.size[0] * ch / cl.size[1]))
    cl = cl.resize((cw, ch), Image.LANCZOS)
    cxx, cyy = int(seal_x - cw / 2), int((y0 + y1) / 2 - ch / 2)
    shadow = Image.new("RGBA", cl.size, (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", cl.size, (0, 0, 0, 200)), (0, 0), cl.getchannel("A"))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(7)), (cxx + 4, cyy + 5))
    canvas.alpha_composite(cl, (cxx, cyy))
    led(canvas, cxx + cw // 2, cyy - 9, 3)

    # the stop is named on the tick row, at the seal, and it is a person - there is
    # no timer anywhere on this panel
    ar_text(d, (seal_x - 8, y1 + 11), STOP_MARK, T_CAP, BRASS_HOT, anchor="ra")


# ------------------------------------------------------------------ the well
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
    return [(f, dy(-14.5 + (v - lo) / max(1e-6, hi - lo) * 15.5)) for f, v in pts]


def pitch_trail(n=96):
    rnd = random.Random(11)
    out, f = [], 600.0
    for i in range(n):
        target = 600.0 * (1 + 0.30 * math.sin(i / 9.0) + 0.10 * math.sin(i / 3.1 + 1.2))
        f += (target - f) * 0.22
        out.append(f * (1 + rnd.uniform(-0.012, 0.012)))
    return out


def draw_well(canvas):
    x0, y0, x1, y1 = WELL
    size = (x1 - x0, y1 - y0)
    well = Image.new("RGBA", size, (12, 11, 10, 255))
    for (cxr, cyr, r, col, a) in ((0.18, 0.05, 0.55, PATINA, 15), (0.86, 0.92, 0.5, BRASS, 10)):
        g = Image.new("RGBA", size, (0, 0, 0, 0))
        rr = int(max(size) * r)
        ImageDraw.Draw(g).ellipse([cxr * size[0] - rr, cyr * size[1] - rr,
                                   cxr * size[0] + rr, cyr * size[1] + rr], fill=col + (a,))
        well = Image.alpha_composite(well, g.filter(ImageFilter.GaussianBlur(rr / 3)))
    well.putalpha(rounded_mask(size, 12))
    canvas.alpha_composite(well, (x0, y0))

    def grid(d):
        f = F_MIN
        while f <= F_MAX:
            x = fx(f)
            decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
            d.line([(x, y0 + 30), (x, y1 - 28)], fill=(255, 245, 230, 18 if decade else 8))
            f *= 2 ** (1 / 3)
        for db in (-12, -6, 0, 6, 12):
            y = dy(db)
            d.line([(x0 + 8, y), (x1 - 8, y)], fill=(255, 245, 230, 20 if db == 0 else 8))
            if db:
                caps(d, (x0 + 12, y - 10), f"{db:+d}", T_MICRO, (255, 245, 230, 88), 1.0)
        for f, lab in ((31.5, "31"), (63, "63"), (125, "125"), (250, "250"), (500, "500"),
                       (1000, "1k"), (2000, "2k"), (4000, "4k"), (8000, "8k"),
                       (16000, "16k")):
            x = fx(f)
            d.line([(x, y0 + 4), (x, y0 + 24)], fill=(255, 245, 230, 26))
            caps(d, (x, y0 + 8), lab, T_MICRO, (255, 245, 230, 118), 1.2, anchor="ma", bold=True)
    over(canvas, grid)

    # ---- the measurement: patina, a filled shape fading into the floor
    pts = analyser()
    mask = Image.new("L", size, 0)
    poly = [(fx(f) - x0, y - y0) for f, y in pts] + [(size[0], size[1] - 22), (0, size[1] - 22)]
    ImageDraw.Draw(mask).polygon(poly, fill=255)
    grad = Image.new("RGBA", size)
    gp = grad.load()
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        col = mix((126, 186, 163), (26, 48, 43), min(1.0, t * 1.1))
        a = int(168 * max(0.0, 1.0 - t) ** 1.15)
        for x in range(size[0]):
            gp[x, y] = col + (a,)
    spec = Image.new("RGBA", size, (0, 0, 0, 0))
    spec.paste(grad, (0, 0), mask)
    canvas.alpha_composite(spec, (x0, y0))
    edge = [(fx(f), y) for f, y in pts]
    glow(canvas, lambda d: d.line(edge, fill=PATINA + (140,), width=3, joint="curve"),
         blur=8, alpha=120)
    over(canvas, lambda d: d.line(edge, fill=(178, 224, 205, 220), width=1, joint="curve"))

    # ---- the note's trail: dotted, because it is a record of where the band has been
    trail = pitch_trail()
    ty0, ty1 = y0 + 66, y1 - 46
    for i, f in enumerate(trail):
        tx = fx(f)
        ty = ty1 - (i / max(1, len(trail) - 1)) * (ty1 - ty0)
        r = 1.3 if i % 3 else 1.9
        over(canvas, lambda d, tx=tx, ty=ty, r=r, i=i:
             d.ellipse([tx - r, ty - r, tx + r, ty + r],
                       fill=PATINA + (36 + int(120 * i / len(trail)),)))

    # ---- the decision: brass curve, drawn on top of the measurement
    cpts = []
    for i in range(561):
        f = F_MIN * (F_MAX / F_MIN) ** (i / 560)
        cpts.append((fx(f), dy(max(DB_MIN, min(DB_MAX, correction_db(f))))))
    glow(canvas, lambda d: d.line(cpts, fill=BRASS + (200,), width=5, joint="curve"),
         blur=14, alpha=190)
    glow(canvas, lambda d: d.line(cpts, fill=BRASS_HOT + (150,), width=2, joint="curve"),
         blur=3, alpha=170)
    over(canvas, lambda d: d.line(cpts, fill=(250, 226, 178, 250), width=2, joint="curve"))

    # ---- bands: a brass ring each; the tracked one is a lens, not a dot
    d = ImageDraw.Draw(canvas)
    for bf, q, g, mode, live in BANDS:
        x, y = fx(bf), dy(correction_db(bf))
        if mode == 3:
            hw = 10 + 9 * live
            glow(canvas, lambda dd, x=x, y=y, hw=hw:
                 dd.ellipse([x - hw, y - hw * 2.2, x + hw, y + hw * 2.2], fill=BRASS),
                 blur=13, alpha=150)
            d = ImageDraw.Draw(canvas)
            d.rounded_rectangle([x - hw, y - hw * 2.2, x + hw, y + hw * 2.2], radius=int(hw),
                                outline=BRASS_HOT, width=2)
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=BRASS_HOT)
            over(canvas, lambda dd, x=x: dd.line([(x, y0 + 30), (x, y1 - 28)],
                                                  fill=BRASS + (58,), width=1))
        else:
            d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=(12, 10, 9), outline=BRASS, width=2)
            if live > 0.05:
                over(canvas, lambda dd, x=x, y=y, live=live:
                     dd.ellipse([x - 3 - 4 * live, y - 3, x + 3 + 4 * live, y + 3],
                                fill=PATINA + (205,)))

    # the tracked band's readout, at the lens rather than in a card
    tx = fx(600.0)
    d = ImageDraw.Draw(canvas)
    caps(d, (tx, y0 + 30), "FOLLOWING 600 Hz", T_MICRO, BRASS_HOT, 1.4, anchor="ma", bold=True)

    # legends: two hues, two jobs, in the strip under the axis (never over the curve)
    by = y1 - 17
    hairline(canvas, x0 + 10, by - 7, x1 - 10, 18)
    lx = x0 + 12
    for label, col in ((MEASURED_KEY, PATINA), (DECISION_KEY, BRASS)):
        over(canvas, lambda dd, lx=lx: dd.ellipse([lx, by, lx + 7, by + 7], fill=col + (240,)))
        d = ImageDraw.Draw(canvas)
        ar_text(d, (lx + 12, by - 5), label, T_CAP, DIM, anchor="la")
        lx += 12 + ar_w(label, T_CAP) + 22

    over(canvas, lambda dd: (dd.rounded_rectangle([x0, y0, x1, y1], radius=12,
                                                  outline=(0, 0, 0, 200)),
                             dd.line([(x0 + 16, y0 + 1), (x1 - 16, y0 + 1)],
                                     fill=(255, 245, 230, 34))))
    canvas.alpha_composite(inner_shadow(size, 12, 12, 150), (x0, y0))


# ------------------------------------------------------------------ the ledger
def draw_ledger(canvas):
    """Four bands as four rows, not four cards: a ledger reads as a record.

    Columns are anchored on their right edge, RTL, and every number carries its unit
    inside its own column - nothing is placed relative to a label it does not own.
    """
    x0, y0, x1, y1 = LEDGER
    d = ImageDraw.Draw(canvas)
    COL = {"idx": x1, "mode": x1 - 56, "gain": x1 - 196, "q": x1 - 316, "hz": x1 - 420,
           "live_r": x1 - 560}
    live_l = x0 + 8

    # header
    for key, (label, unit) in zip(("idx", "mode", "gain", "q", "hz"), LEDGER_COLS):
        ar_text(d, (COL[key], y0), label, T_CAP, FAINT, anchor="ra")
        if unit:
            caps(d, (COL[key] - ar_w(label, T_CAP) - 7, y0 + 3), unit, T_MICRO, (150, 140, 126), 0.4,
                 anchor="ra")
    ar_text(d, (COL["live_r"], y0), LEDGER_LIVE, T_CAP, FAINT, anchor="ra")
    hairline(canvas, x0, y0 + 18, x1, 26)

    row_h = ROW_H
    for i, (bf, q, g, mode, live) in enumerate(BANDS):
        ry = y0 + 26 + i * row_h
        hot = mode == 3
        if hot:
            over(canvas, lambda dd, ry=ry: dd.rectangle([x0, ry - 5, x1, ry + row_h - 9],
                                                        fill=BRASS + (16,)))
            over(canvas, lambda dd, ry=ry: dd.rectangle([x0, ry - 5, x0 + 3, ry + row_h - 9],
                                                        fill=BRASS + (210,)))
        d = ImageDraw.Draw(canvas)
        caps(d, (COL["idx"], ry), f"0{i + 1}", T_MICRO, BRASS_HOT if hot else DIM, 1.4, anchor="ra")
        ar_text(d, (COL["mode"], ry - 3), BAND_MODES[mode], T_BODY,
                BRASS_HOT if hot else (PATINA if mode == 2 else DIM), anchor="ra")

        num(canvas, COL["gain"], ry - 2, f"{g:+.1f}", T_VALUE, TEXT)
        num(canvas, COL["q"], ry - 1, f"{q:.2f}", T_BODY, DIM, bold=False)
        num(canvas, COL["hz"], ry - 2, f"{bf:.0f}", T_VALUE, TEXT)

        # activity: fills from the right (RTL), patina because it is a measurement
        over(canvas, lambda dd, ry=ry: dd.rounded_rectangle([live_l, ry + 4, COL["live_r"], ry + 13],
                                                            radius=4, fill=(255, 245, 230, 14)))
        if live > 0.02:
            wpx = (COL["live_r"] - live_l) * live
            over(canvas, lambda dd, ry=ry, wpx=wpx: dd.rounded_rectangle(
                [COL["live_r"] - wpx, ry + 4, COL["live_r"], ry + 13], radius=4,
                fill=PATINA + (215,)))
        elif hot:
            caps(d, (COL["live_r"], ry + 4), "TRACKING", T_MICRO, BRASS_HOT, 1.4, anchor="ra")
        else:
            caps(d, (COL["live_r"], ry + 4), "HOLD", T_MICRO, FAINT, 1.4, anchor="ra")
        if i < len(BANDS) - 1:
            hairline(canvas, x0, ry + row_h - 5, x1, 16)




# ------------------------------------------------------------------ footer
def draw_footer(canvas):
    x0, y0, x1, y1 = FOOTER
    d = ImageDraw.Draw(canvas)
    hairline(canvas, x0, y0 - 12, x1, 24)

    # the seal: the product's one irreversible action, stated plainly
    cl = Image.open(ASSETS / "clamp.png").convert("RGBA")
    cl = cl.resize((cl.size[0] * 3 // 4, cl.size[1] * 3 // 4), Image.LANCZOS)
    canvas.alpha_composite(cl, (x0, y0 + 12))
    d = ImageDraw.Draw(canvas)
    sx = x0 + cl.size[0] + 12
    ar_text(d, (sx, y0 + 8), SEAL_STATE, T_VALUE, BRASS_HOT)
    ar_text(d, (sx, y0 + 30), FOOTER_SEAL, T_CAP, FAINT)

    # the report: one Arabic sentence carrying the whole state
    ar_text(d, (x0 + 360, y0 + 22), FOOTER_REPORT, T_BODY, (184, 174, 159))

    # two brass knobs - the only values the operator turns by hand
    for i, (val, label) in enumerate(((0.62, KNOB_LABELS[0]), (0.5, KNOB_LABELS[1]))):
        cx = x1 - 40 - i * 128
        cy = y0 + 26
        knob(canvas, cx, cy, 58, val, brass=True)
        d = ImageDraw.Draw(canvas)
        ar_text(d, (cx - 42, cy - 14), label, T_CAP, DIM, anchor="ra")
        num(canvas, cx - 42, cy + 1, f"{val * 100:.0f}%", T_BODY, TEXT)

    caps(d, (x1, y1 - 4), "V1.3.0", T_MICRO, FAINT, 1.8, anchor="ra")


# ------------------------------------------------------------------ the panel
def build():
    plate = Image.open(ASSETS / "panel_plate.png").convert("RGBA")
    mask = rounded_mask((W, H), 18)
    page = Image.new("RGBA", (W, H), INK + (255,))
    shadow = Image.new("RGBA", (W + 80, H + 80), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", (W, H), (0, 0, 0, 215)), (40, 40), mask)
    page.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(22))
                         .crop((40, 40, 40 + W, 40 + H)))
    page.alpha_composite(plate, (0, 0))
    canvas = page
    canvas.putalpha(mask)
    canvas.alpha_composite(inner_shadow((W, H), 18, 14, 130))
    over(canvas, lambda d: d.rounded_rectangle([0, 0, W - 1, H - 1], radius=18,
                                               outline=(255, 245, 230, 34)))

    d = ImageDraw.Draw(canvas)

    # ---- header: the wordmark reads left-to-right (it is a Latin trademark, not an
    # Arabic sentence - anchoring it to the RTL edge reversed it), the state is at the
    # left edge where an Arabic reader finishes looking.
    mark = "AEQ RESTORE"
    total = text_w(mark, T_VALUE, True, 4.6)
    caps(d, (X1 - total, HEADER[1] + 2), mark, T_VALUE, BRASS_HOT, 4.6, bold=True)
    ar_text(d, (X1, HEADER[1] + 25), TAGLINE, T_BODY, (184, 174, 159), anchor="ra")

    led(canvas, X0 + 5, HEADER[1] + 9, 4)
    d = ImageDraw.Draw(canvas)
    ar_text(d, (X0 + 18, HEADER[1] + 1), SEAL_STATE, T_BODY, TEXT)
    caps(d, (X0 + 18, HEADER[1] + 24), "SEALED · 23:41", T_MICRO, FAINT, 1.4)
    hairline(canvas, X0, HEADER[3] + 2, X1, 22)

    draw_take(canvas)
    draw_well(canvas)
    draw_ledger(canvas)
    draw_footer(canvas)

    canvas.putalpha(mask)
    canvas.save(OUT)
    print(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(build())
