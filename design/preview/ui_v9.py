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

# ---- the world (design/IDENTITY_AR.md) ----------------------------------------------
# A world is one ink, two accents and the light that falls on them.  The roles never
# change (accent = a decision, second accent = a measurement); the hues and the chroma
# are a *mood*, and the mood is allowed to move without the product changing.
WORLDS = {
    # warm indigo night, amber decision, mint-aqua measurement.  The default: it is the
    # brightest of the three while keeping the two accents far apart in hue.
    "dawn": dict(ink_top=(21, 19, 32), ink_bot=(7, 8, 13), text=(247, 244, 236),
                 soft=(196, 192, 208), dim=(166, 163, 182), faint=(132, 130, 150),
                 line=(240, 244, 255),
                 accent=(255, 183, 64), accent_hot=(255, 226, 158),
                 second=(56, 232, 190), second_hot=(160, 255, 232),
                 chip=(23, 22, 34), blooms=((0.10, -0.26, 0.72, "accent", 30),
                                            (0.96, 1.18, 0.66, "second", 26)),
                 wells=((0.24, 0.66, 0.55, "second", 20), (0.78, 0.30, 0.50, "accent", 14)),
                 knob="brass", knob_tint=None),
    # deep violet, magenta decision, cyan measurement: the loudest, closest to the
    # 2025 crop of instrument UIs.
    # violet field, coral decision, sky measurement: the warm one.  Coral is a colour a
    # person likes looking at, and the field under it is violet rather than brown, so it
    # reads 2025 and not 1975.
    "sunrise": dict(ink_top=(28, 21, 44), ink_bot=(10, 9, 17), text=(252, 246, 244),
                    soft=(214, 202, 212), dim=(184, 172, 190), faint=(146, 136, 158),
                    line=(252, 246, 255),
                    accent=(255, 128, 92), accent_hot=(255, 194, 172),
                    second=(88, 214, 255), second_hot=(196, 240, 255),
                    chip=(28, 22, 38), blooms=((0.08, -0.28, 0.72, "accent", 30),
                                               (0.97, 1.20, 0.66, "second", 26)),
                    wells=((0.24, 0.66, 0.58, "second", 28), (0.78, 0.30, 0.52, "accent", 22)),
                    knob="brass", knob_tint=(255, 128, 92)),
    # 1.2.9: the ground was lifted.  The first pass at this world was correct in hue and
    # too dark in value - "ألوان مبهجة أكتر من كدة حاسه قديم اوي" is a complaint about
    # brightness, not about taste - so the field, the plate and the ambient blooms all
    # gain light.  The two accents do not move; the roles do not move.
    "neon": dict(ink_top=(52, 33, 74), ink_bot=(17, 12, 30), text=(252, 246, 254),
                 soft=(206, 196, 220), dim=(176, 168, 196), faint=(140, 134, 160),
                 line=(246, 240, 250),
                 accent=(255, 94, 168), accent_hot=(255, 186, 220),
                 second=(84, 214, 255), second_hot=(186, 240, 255),
                 chip=(40, 30, 56), blooms=((0.08, -0.28, 0.74, "accent", 44),
                                            (0.97, 1.20, 0.68, "second", 40)),
                 wells=((0.22, 0.68, 0.60, "second", 30), (0.80, 0.28, 0.56, "accent", 24)),
                 knob="brass", knob_tint=(255, 94, 168)),
    # ink-teal, lime decision, coral measurement: the most cheerful pair and the one
    # that reads least "audio plugin".
    "citrus": dict(ink_top=(16, 24, 26), ink_bot=(6, 9, 11), text=(244, 250, 246),
                   soft=(196, 214, 206), dim=(166, 186, 178), faint=(128, 148, 142),
                   line=(240, 255, 248),
                   accent=(202, 255, 74), accent_hot=(236, 255, 176),
                   second=(255, 122, 92), second_hot=(255, 190, 170),
                   chip=(18, 26, 28), blooms=((0.10, -0.26, 0.72, "accent", 24),
                                               (0.96, 1.18, 0.66, "second", 30)),
                   wells=((0.24, 0.66, 0.55, "accent", 16), (0.78, 0.30, 0.50, "second", 22)),
                   knob="brass", knob_tint=(202, 255, 74)),
}

import os

PALETTE = os.environ.get("AEQ_PALETTE", "neon")
DEFAULT_PALETTE = "neon"
_W = WORLDS[PALETTE]

INK_TOP = _W["ink_top"]
INK_BOT = _W["ink_bot"]
TEXT = _W["text"]
SOFT = _W["soft"]
DIM = _W["dim"]
FAINT = _W["faint"]
LINE = _W["line"]
BRASS = _W["accent"]
BRASS_HOT = _W["accent_hot"]
PATINA = _W["second"]
PATINA_HOT = _W["second_hot"]
CHIP_FILL = _W["chip"]

OUT = HERE / f"ui_v9_{PALETTE}_1180x700.png"
BLOOMS = tuple((cxr, cyr, r, (_W[c] if c in ("accent", "second") else c), a)
               for (cxr, cyr, r, c, a) in _W["blooms"])
WELLS = tuple((cxr, cyr, r, (_W[c] if c in ("accent", "second") else c), a)
              for (cxr, cyr, r, c, a) in _W["wells"])

# ---- one type scale, four steps ----------------------------------------------------
# 1.2.10: everything went up a step and the numbers went up two - the panel is read
# at arm's length on a 1180 px window, and 8 px micro-caps are a hat, not a label.
T_MICRO, T_CAP, T_BODY, T_VALUE = 11, 13, 16, 21

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
TAGLINE = "listens to the whole take; you stop it"
SEAL_STATE = "SEALED"
TAKE_LABEL = "TAKE"
TAKE_DONE = "HEARD 24.9 OF 31.2 S"
REMAINING = "6.3 S LEFT"
STOP_MARK = "STOPPED BY YOU"
MEASURED_KEY = "measured"
DECISION_KEY = "decision"
BAND_MODES = ("STATIC", "STATIC", "DYNAMIC", "TRACKED")
MODE_SEGMENTS = ("RESTORE", "MATCH")
FOOTER_SEAL = "nothing moves on its own"
FOOTER_REPORT = "4 bands - 1 dynamic - 1 tracked"
KNOB_LABELS = ("strength", "listen")
CHIP_UNIT = "dB"
CHIP_HZ = "Hz"
FOLLOWING = "TRACKED 600 Hz"

# Every string the panel draws, so a gate can check the inventory rather than trust it.
PANEL_STRINGS = (TAGLINE, SEAL_STATE, TAKE_LABEL, STOP_MARK, REMAINING, MEASURED_KEY,
                 DECISION_KEY, *BAND_MODES, *MODE_SEGMENTS, FOOTER_SEAL, FOOTER_REPORT,
                 *KNOB_LABELS, CHIP_UNIT, CHIP_HZ, FOLLOWING)
ARABIC_STRINGS = PANEL_STRINGS          # kept for the older gates: nothing is Arabic now

CENTRED_PILL = False
EQUAL_CARD_GRID = False
WATERMARK = None
PALETTE_NAMES = tuple(WORLDS)
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
    """Draw one line.  The panel's copy is Latin now (1.2.10), so a plain string goes
    through the Latin path and the Arabic shaper is only kept for anything that still
    carries Arabic - which today is nothing, and the gates assert that."""
    DRAWN["sizes"].add(size)
    if s.isascii():
        DRAWN["caps"].add(s)
        return _caps(d, xy, s, size, *a, **k)
    DRAWN["arabic"].add(s)
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
    for (cxr, cyr, r, col, a) in BLOOMS:
        rr = int(max(W, H) * r)
        g = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([cxr * W - rr, cyr * H - rr, cxr * W + rr, cyr * H + rr],
                                  fill=col + (a,))
        canvas.alpha_composite(g.filter(ImageFilter.GaussianBlur(rr / 2.6)))
    canvas.alpha_composite(grain((W, H)))
    return canvas


# ------------------------------------------------------------------ parts
def _hue255(col):
    r, g, b = [c / 255.0 for c in col[:3]]
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        return 0
    if mx == r:
        h = (60 * ((g - b) / d)) % 360
    elif mx == g:
        h = 60 * ((b - r) / d) + 120
    else:
        h = 60 * ((r - g) / d) + 240
    return int(h / 360 * 255)


def anodize(img, col, amount=0.82):
    """Rotate the metal's hue onto the world's accent without touching its shading.

    A brass knob in a magenta world is the third hue nobody asked for; a steel knob is
    dull.  Anodised metal is the thing that exists in both worlds - the render keeps
    every highlight and the hue becomes the accent.

    Two traps, both measured rather than guessed: the sprite's own hue has to come from
    its *opaque* pixels (transparent RGB is black and drags the average to red), and the
    hue circle has no middle, so the shift is a rotation and never an interpolation.
    """
    alpha = img.getchannel("A")
    hsv = img.convert("RGB").convert("HSV")
    h, s_, v = hsv.split()

    opaque = alpha.point(lambda a: 255 if a > 128 else 0)
    masked = Image.composite(h, Image.new("L", h.size, 0), opaque)
    hist = masked.histogram()
    total = sum(hist[1:]) or 1
    base = sum(i * n for i, n in enumerate(hist) if i) / total

    step = int(((_hue255(col) - base + 128) % 256 - 128) * amount)
    h = h.point(lambda x: (x + step) % 256)
    s_ = s_.point(lambda x: min(255, int(x * 1.10)))
    out = Image.merge("HSV", (h, s_, v)).convert("RGB").convert("RGBA")
    out.putalpha(alpha)
    return out


def knob_small(canvas, cx, cy, size, value, brass=True):
    body = sprite("knob_brass.png" if brass else "knob_body.png", size)
    if _W.get("knob_tint") is not None and brass:
        body = anodize(body, _W["knob_tint"])
    seat = int(size * 1.08)
    boss = Image.new("RGBA", (seat, seat), (0, 0, 0, 0))
    ImageDraw.Draw(boss).ellipse([0, 0, seat - 1, seat - 1], fill=(3, 3, 4, 240))
    canvas.alpha_composite(boss.filter(ImageFilter.GaussianBlur(1.2)),
                           (int(cx - seat / 2), int(cy - seat / 2)))
    over(canvas, lambda d: (d.arc([cx - seat / 2, cy - seat / 2, cx + seat / 2, cy + seat / 2],
                                  200, 340, fill=LINE + (30,), width=1),
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
                                     fill=(BRASS_HOT if on else mix(FAINT, INK_BOT, 0.45)) + (255,)))


# ------------------------------------------------------------------ header
def draw_header(canvas):
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = HEADER
    cy = (y0 + y1) / 2

    # wordmark: light weight, wide tracking, left.  No pill in the middle (G50).
    caps(d, (x0, cy - 7), "AEQ", T_BODY, BRASS_HOT, 3.4, bold=True)
    caps(d, (x0 + caps_w("AEQ", T_BODY, 3.4, True) + 8, cy - 7), "RESTORE", T_BODY,
         LINE + (120,), 3.4)

    # state: a pill at the right edge, and the seal LED
    label_w = ar_w(SEAL_STATE, T_CAP) + 40
    pill(canvas, (x1 - label_w, cy - 11, x1, cy + 11), SEAL_STATE, T_CAP,
         fill=BRASS + (26,), line=BRASS + (90,), text_col=BRASS_HOT)
    led(canvas, x1 - label_w + 12, cy, 3)

    # the seal never changes by itself: said once, at the seal
    ar_text(d, (x1, cy + 14), FOOTER_SEAL, T_MICRO, DIM, anchor="ra")

    # tagline under the wordmark: the one line that says what this is
    ar_text(d, (x0, y1 - 9), TAGLINE, T_BODY, SOFT)


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
    # the well is the world's own dark, not a neutral black: a black inset in a violet
    # panel reads as a hole, and at this size the hole was most of what the eye saw.
    plate = Image.new("RGBA", size, mix(INK_BOT, INK_TOP, 0.34) + (222,))
    plate.putalpha(rounded_mask(size, GRAPH_R))
    canvas.alpha_composite(plate, (x0, y0))
    over(canvas, lambda d: (d.rounded_rectangle([x0, y0, x1, y1], radius=GRAPH_R,
                                                outline=LINE + (26,)),
                            d.line([(x0 + GRAPH_R, y0 + 1), (x1 - GRAPH_R, y0 + 1)],
                                   fill=LINE + (34,))))

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
        ky = TAKE_RIBBON[3] + 12
        lw = ar_w(label, T_CAP)
        ar_text(ImageDraw.Draw(canvas), (rx, ky), label, T_CAP, DIM, anchor="ra")
        over(canvas, lambda dd, rx=rx, lw=lw: dd.ellipse([rx - lw - 18, ky + 5, rx - lw - 10, ky + 13],
                                                        fill=col + (230,)))
        rx -= lw + 44


def draw_graph_content(canvas, top, bot):
    x0, y0, x1, y1 = GRAPH
    size = (x1 - x0, y1 - y0)

    def grid(d):
        f = F_MIN
        while f <= F_MAX:
            x = fx(f)
            decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
            d.line([(x, top), (x, bot)], fill=LINE + ((16 if decade else 7),))
            f *= 2 ** (1 / 3)
        for db in (-12, -6, 0, 6, 12):
            y = y0 + 26 + (y1 - y0 - 26) * (DB_MAX - db) / (DB_MAX - DB_MIN) * 0.86
            d.line([(x0 + 12, y), (x1 - 12, y)], fill=LINE + ((14 if db == 0 else 6),))
            if db:
                caps(d, (x0 + 16, y - 9), f"{db:+d}", T_MICRO, LINE + (84,), 0.8)
        for f, lab in ((31.5, "31"), (63, "63"), (125, "125"), (250, "250"), (500, "500"),
                       (1000, "1k"), (2000, "2k"), (4000, "4k"), (8000, "8k"), (16000, "16k")):
            x = fx(f)
            caps(d, (x, y0 + 8), lab, T_MICRO, LINE + (108,), 1.1, anchor="ma")
    over(canvas, grid)

    # one soft bloom inside the well, tinted by whichever accent owns that side
    for (cxr, cyr, r, col, a) in WELLS:
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
        col = mix(mix(PATINA, TEXT, 0.22), mix(PATINA, INK_BOT, 0.72),
                  min(1.0, t * 1.05))
        a = int(120 * max(0.0, 1.0 - t) ** 1.3) + 22
        for x in range(size[0]):
            gp[x, y] = col + (a,)
    spec = Image.new("RGBA", size, (0, 0, 0, 0))
    spec.paste(grad, (0, 0), mask)
    canvas.alpha_composite(spec, (x0, y0))
    edge = [(fx(f), y) for f, y in pts]
    glow(canvas, lambda d: d.line(edge, fill=PATINA + (120,), width=2, joint="curve"), blur=7,
         alpha=110)
    over(canvas, lambda d: d.line(edge, fill=PATINA_HOT + (238,), width=2, joint="curve"))

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
    glow(canvas, lambda d: d.line(cpts, fill=BRASS + (210,), width=6, joint="curve"), blur=20,
         alpha=210)
    glow(canvas, lambda d: d.line(cpts, fill=BRASS_HOT + (170,), width=3, joint="curve"),
         blur=4, alpha=180)
    over(canvas, lambda d: d.line(cpts, fill=BRASS_HOT + (252,), width=2, joint="curve"))

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
                 BRASS_HOT, 1.3)
        else:
            glow(canvas, lambda dd, x=x, y=y: dd.ellipse([x - 9, y - 9, x + 9, y + 9],
                                                         fill=BRASS), blur=10, alpha=120)
            d = ImageDraw.Draw(canvas)
            d.ellipse([x - 8, y - 8, x + 8, y + 8], fill=INK_BOT, outline=BRASS, width=2)
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
        col = mix(mix(PATINA, INK_BOT, 0.78), mix(PATINA, TEXT, 0.18), t)
        # the last stretch is where the measurement meets the decision: the take turns
        # into the accent as it reaches the clamp, so the stop is the only place the two
        # hues touch - which is exactly what the panel means by "you stopped it here"
        if t > 0.86:
            col = mix(col, BRASS, min(1.0, (t - 0.86) / 0.14) * 0.9)
        for y in range(y1 - y0):
            gp[x, y] = col + (int(72 + 108 * t),)
    band = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    band.paste(grad, (0, 0), mask)
    canvas.alpha_composite(band, (x0, y0))

    over(canvas, lambda dd: (dd.line([(seal_x, y0 - 2), (seal_x, y1 + 2)],
                                     fill=(255, 246, 232, 40), width=6),
                             dd.line([(seal_x, y0), (seal_x, y1)], fill=PATINA_HOT + (235,), width=2)))

    # the clamp: the real machined part, vertical, sitting on the seal
    cl = Image.open(ASSETS / "clamp.png").convert("RGBA").rotate(90, expand=True)
    if _W.get("knob_tint") is not None:
        cl = anodize(cl, _W["knob_tint"], 0.72)
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
    caps(d, (x0, y1 + 12), "HEARD 24.9 S  ·  96 BANDS  ·  15 REFERENCE TAKES", T_MICRO,
         DIM, 1.1)
    # the seal itself is labelled: above the band, where nothing else lives
    mark_w = ar_w(STOP_MARK, T_MICRO)
    over(canvas, lambda dd: dd.rounded_rectangle(
        [seal_x - mark_w / 2 - 8, y0 - 24, seal_x + mark_w / 2 + 8, y0 - 5],
        radius=4, fill=(10, 8, 16, 200)))
    d = ImageDraw.Draw(canvas)
    ar_text(d, (seal_x, y0 - 21), STOP_MARK, T_MICRO, BRASS_HOT, anchor="ma")


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
            [cx0, y0, cx1, y1], radius=CHIP_R, fill=CHIP_FILL + (232,)))
        if hot:
            glow(canvas, lambda dd, cx0=cx0, cx1=cx1, y0=y0, y1=y1: dd.rounded_rectangle(
                [cx0, y0, cx1, y1], radius=CHIP_R, fill=BRASS), blur=14, alpha=22)
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.rounded_rectangle(
            [cx0, y0, cx1, y1], radius=CHIP_R,
            outline=(BRASS + (150,) if hot else LINE + (30,))))
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.line(
            [(cx0 + 10, y0 + 1), (cx1 - 10, y0 + 1)], fill=LINE + (26,)))

        d = ImageDraw.Draw(canvas)
        if mode >= 2:
            over(canvas, lambda dd, cx0=cx0, cx1=cx1, hot=hot: dd.rounded_rectangle(
                [cx0, y0 + 12, cx0 + 3, y1 - 12], radius=2,
                fill=(BRASS if hot else PATINA) + (255,)))
        d = ImageDraw.Draw(canvas)
        # two rows, not three: the number owns the left, the spec owns the right, and
        # the chip keeps the height the band row has always had.
        caps(d, (cx0 + 12, y0 + 7), f"0{i + 1}", T_MICRO, BRASS_HOT if hot else FAINT, 1.3)
        ar_text(d, (cx1 - 12, y0 + 5), BAND_MODES[mode], T_CAP,
                BRASS_HOT if hot else (PATINA if mode == 2 else DIM), anchor="ra")
        w = num(canvas, cx0 + 12, y0 + 22, f"{g:+.1f}", T_VALUE,
                BRASS_HOT if hot else TEXT, anchor="la")
        d = ImageDraw.Draw(canvas)
        caps(d, (cx0 + 17 + w, y0 + 30), CHIP_UNIT, T_MICRO, DIM, 0.3)
        caps(d, (cx1 - 12, y0 + 31), f"{bf:.0f} Hz · Q {q:.2f}", T_MICRO, FAINT, 0.9,
             anchor="ra")
        if live > 0.02:
            over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.rounded_rectangle(
                [cx0 + 12, y1 - 11, cx0 + 12 + (cx1 - cx0 - 24) * live, y1 - 8], radius=2,
                fill=PATINA + (200,)))


# ------------------------------------------------------------------ footer
def draw_footer(canvas):
    x0, y0, x1, y1 = FOOTER
    d = ImageDraw.Draw(canvas)
    cy = (y0 + y1) / 2

    # segmented mode control: one pill, three segments - the modern replacement for
    # three checkboxes and, in this product, the honest state of the band layer
    seg_w, seg_h = 90, 26
    total = seg_w * len(MODE_SEGMENTS) + 8
    sx = x0
    over(canvas, lambda dd: dd.rounded_rectangle([sx, cy - seg_h / 2, sx + total, cy + seg_h / 2],
                                                 radius=PILL_R, fill=(255, 255, 255, 10)))
    over(canvas, lambda dd: dd.rounded_rectangle([sx + 5, cy - seg_h / 2 + 3,
                                                  sx + 5 + seg_w, cy + seg_h / 2 - 3],
                                                 radius=PILL_R - 3, fill=BRASS + (34,)))
    over(canvas, lambda dd: dd.rounded_rectangle([sx + 5, cy - seg_h / 2 + 3,
                                                  sx + 5 + seg_w, cy + seg_h / 2 - 3],
                                                 radius=PILL_R - 3, outline=(BRASS + (170,))))
    d = ImageDraw.Draw(canvas)
    for i, label in enumerate(MODE_SEGMENTS):
        ar_text(d, (sx + 5 + seg_w * i + seg_w / 2, cy - 8), label, T_CAP,
                BRASS_HOT if i == 0 else DIM, anchor="ma")

    # the report, one Arabic sentence, and the seal state
    ar_text(d, (sx + total + 28, cy - 7), FOOTER_REPORT, T_BODY, SOFT)

    # two knobs: the only values the operator turns by hand
    for i, (val, label) in enumerate(((0.62, KNOB_LABELS[0]), (0.5, KNOB_LABELS[1]))):
        cx = x1 - 30 - i * 128
        knob_small(canvas, cx, cy, 42, val, brass=(_W.get("knob", "brass") == "brass"))
        d = ImageDraw.Draw(canvas)
        ar_text(d, (cx - 34, cy - 19), label, T_MICRO, DIM, anchor="ra")
        num(canvas, cx - 34, cy + 1, f"{val * 100:.0f}%", T_BODY, TEXT, anchor="ra")


# ------------------------------------------------------------------ build
def build():
    # AEQ_GROUND_ONLY draws the field and nothing else, to ui_v9_<world>_ground.png.
    # The identity gate diffs the panel against it: on a panel whose *ground* is
    # colourful, a saturation-only hue count reports the room's light instead of the
    # drawing's ink, and "exactly two accents" stops meaning anything.
    if os.environ.get("AEQ_GROUND_ONLY"):
        g = ground()
        g.putalpha(rounded_mask((W, H), PANEL_R))
        g.save(HERE / f"ui_v9_{PALETTE}_ground.png")
        return 0

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
                                                outline=LINE + (30,)),
                            d.line([(PANEL_R, 1), (W - PANEL_R, 1)], fill=LINE + (44,))))

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
