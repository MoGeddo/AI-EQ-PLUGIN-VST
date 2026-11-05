#!/usr/bin/env python3
"""ui_v6.py - the panel built on rendered materials instead of drawn shapes.

v3, v4 and v5 were all vector-ish: every pixel was a rectangle, a circle or a gradient,
and the verdict on all three was the same.  The reason is not taste - a machined knob
needs real specular behaviour (a knurled ring catching light, an anodised bevel, radial
brushing).  You cannot draw that with primitives, and code that tries produces the flat
"primitive" look the user kept naming.

So: the parts are rendered as materials (design/assets/src, processed by
design/assets_prep.py) and composited here, and only the things that must change with
the DSP are drawn in code - the pointer rotates to the value, the lit button lights, the
curve and the spectrum are the numbers.

    /tmp/venv/bin/python design/preview/ui_v6.py
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
    rounded_mask,
)

ROOT = HERE.parents[1]
ASSETS = ROOT / "design" / "assets"
W, H = 1180, 700
OUT = HERE / "ui_v6_1180x700.png"

TEXT = (241, 238, 233)
DIM = (158, 154, 149)
FAINT = (124, 120, 117)
AMBER = (247, 166, 84)
AMBER_HOT = (255, 219, 165)
BLUE = (104, 133, 240)
BLUE_HOT = (176, 194, 255)

GRAPH = (32, 78, 1148, 424)
STRIP = (32, 624, 1148, 676)
CARD_Y0, CARD_Y1 = 452, 612
CARD_XS = (32, 311, 590, 869)
CARD_W = 267

# The copy is data, not literals buried in build(): check_panel.py asserts the font
# covers every one of these, and it can only do that if it can ask the module.
HEADER_PILL = "التزام محفوظ · 4 باندات"
GRAPH_NOTE = "الباند المتحرك بيثبّت 600 هرتز"
FOOTER = "اتظبط لوحده بعد ٢٤.٩ ثانية · ٤ باندات · واحد شغّال ديناميك"
BYPASS_NOTE = "معزول"
CARDS = (
    # eng, arabic, knob value, knob label, lcd value, lcd unit, line 1, line 2, lcd warm
    ("LEARN", "التعلم", 0.38, "الحساسية", "24.9", "ثانية", "اتسمع ٩٦ باند", "وأنت وقفت التعلم", True),
    ("CURVE", "المنحنى", 0.62, "القوة", "4", "باند", "٦٢٪ من التصحيح", "٣ حركات متكررة", True),
    ("TRACK", "المتحرك", 0.44, "التثبيت", "600", "هرتز", "ماشي مع النغمة", "باند ديناميكي واحد", False),
    ("OUTPUT", "الخروج", 0.5, "المستوى", "0.0", "dB", "", "", True),
)
ARABIC_STRINGS = tuple(
    [HEADER_PILL, GRAPH_NOTE, FOOTER, BYPASS_NOTE]
    + [c[1] for c in CARDS] + [c[3] for c in CARDS] + [c[6] for c in CARDS]
    + [c[7] for c in CARDS] + [c[5] for c in CARDS]
)
F_MIN, F_MAX = 30.0, 18000.0
DB_MIN, DB_MAX = -18.0, 18.0
SR = 48000.0

_sprite_cache: dict[tuple[str, int], Image.Image] = {}


def sprite(name: str, size: int) -> Image.Image:
    key = (name, size)
    if key not in _sprite_cache:
        im = Image.open(ASSETS / name).convert("RGBA")
        _sprite_cache[key] = im.resize((size, size), Image.LANCZOS)
    return _sprite_cache[key]


def clamp01(v):
    return max(0.0, min(1.0, v))


def knob_angle(value):
    """-135 deg at 0, +135 deg at 1, measured clockwise from 12 o'clock."""
    return math.radians(-135.0 + 270.0 * clamp01(value))


def needle(canvas, cx, cy, size, value):
    """The value indicator: a machined pointer cut into the knob face.

    Drawn rather than pasted because it is the one part that *must* answer the
    DSP - a sprite would have to be rotated about its centre, and a 3 px line
    resampled at 96 px turns to mush.  Supersampled 4x, then a bright amber
    core inside a dark groove so it reads on both the polished and the brushed
    face.
    """
    r = size / 2
    ss = 4
    n = int(size * ss)
    lay = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    c = n / 2
    a = knob_angle(value)
    dx, dy = math.sin(a), -math.cos(a)
    px, py = -dy, dx
    p0 = (c + dx * r * 0.02, c + dy * r * 0.02)
    p1 = (c + dx * r * 0.70, c + dy * r * 0.70)
    w = r * 0.055 * ss

    def capsule(q0, q1, wid, fill):
        d.line([q0, q1], fill=fill, width=int(wid))
        for q in (q0, q1):
            d.ellipse([q[0] - wid / 2, q[1] - wid / 2, q[0] + wid / 2, q[1] + wid / 2], fill=fill)

    capsule((p0[0] + px * 0.4, p0[1] + py * 0.4), (p1[0], p1[1]), w + 2.4 * ss, (12, 9, 6, 165))
    capsule(p0, p1, w, (250, 163, 66, 255))
    capsule(p0, (p1[0] - dx * 1.5 * ss, p1[1] - dy * 1.5 * ss), w * 0.42, (255, 232, 194, 235))
    lay = lay.resize((int(size), int(size)), Image.LANCZOS)
    canvas.alpha_composite(lay, (int(cx - r), int(cy - r)))


def knob(canvas, cx, cy, size, value, warm=False):
    """A real machined knob, with the pointer rotated to the value the DSP reports.

    The part is a render, so it arrives with its own specular highlights; what it
    cannot arrive with is a *hole* in the card.  A shallow recess behind it puts
    the knurled octagon in shadow and stops the knob reading as a pasted sticker.
    """
    body = sprite("knob_body_warm.png" if warm else "knob_body.png", size)
    rec = int(size * 1.05)
    boss = Image.new("RGBA", (rec, rec), (0, 0, 0, 0))
    ImageDraw.Draw(boss).ellipse([0, 0, rec - 1, rec - 1], fill=(3, 3, 5, 246))
    boss = boss.filter(ImageFilter.GaussianBlur(1.4))
    canvas.alpha_composite(boss, (int(cx - rec / 2), int(cy - rec / 2)))
    hb = [cx - rec / 2, cy - rec / 2, cx + rec / 2, cy + rec / 2]
    over(canvas, lambda d: (d.arc(hb, 198, 342, fill=(255, 255, 255, 30), width=2),
                            d.arc(hb, 18, 162, fill=(0, 0, 0, 165), width=3)))
    # contact shadow under the part, so it sits in the recess instead of floating
    sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", (size, size), (0, 0, 0, 185)), (0, 0),
             body.getchannel("A").point(lambda v: int(v * 0.9)))
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(max(3, size // 24))),
                           (int(cx - size / 2), int(cy - size / 2 + size // 26)))
    canvas.alpha_composite(body, (int(cx - size / 2), int(cy - size / 2)))
    needle(canvas, cx, cy, size, value)
    return canvas


def push_button(canvas, cx, cy, size, on=True):
    im = sprite("button_on.png" if on else "button_off.png", size)
    if on:
        g = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        g.paste(im, (0, 0), im)
        canvas.alpha_composite(g.filter(ImageFilter.GaussianBlur(size // 8)),
                               (int(cx - size / 2), int(cy - size / 2)))
    canvas.alpha_composite(im, (int(cx - size / 2), int(cy - size / 2)))
    return canvas


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def lcd(canvas, box, value, unit, warm=True, big=27):
    """A dark glass readout: the value is the loudest thing in the card.

    One opaque face colour, not a black fill followed by a warm one: ImageDraw
    *replaces* a pixel, it does not blend, so a second translucent fill in the
    same layer throws the first one away and the "LCD" comes out lighter than
    the card it sits on.  Blend the tint into the face colour up front.
    """
    x0, y0, x1, y1 = box
    col = AMBER_HOT if warm else BLUE_HOT
    face = mix((3, 4, 6), (255, 176, 92) if warm else (104, 132, 246), 0.10)
    size = (x1 - x0, y1 - y0)
    over(canvas, lambda d: d.rounded_rectangle(box, radius=7, fill=face + (255,)))
    canvas.alpha_composite(inner_shadow(size, 7, 5, 96), (x0, y0))
    over(canvas, lambda d: (d.rounded_rectangle(box, radius=7, outline=(255, 255, 255, 34)),
                            d.line([(x0 + 5, y0 + 1), (x1 - 5, y0 + 1)], fill=(0, 0, 0, 190)),
                            d.line([(x0 + 5, y1 - 1), (x1 - 5, y1 - 1)], fill=(255, 255, 255, 20))))
    d = ImageDraw.Draw(canvas)
    f = font(FONT_B, big)
    bb = d.textbbox((0, 0), value, font=f)
    vx = x0 + 13
    vy = y0 + (y1 - y0) / 2 - (bb[3] - bb[1]) / 2 - bb[1] - 2
    glow(canvas, lambda dd: dd.text((vx, vy), value, font=f, fill=col), blur=9, alpha=95)
    d = ImageDraw.Draw(canvas)
    d.text((vx, vy), value, font=f, fill=col)
    if unit:
        ucol = (250, 240, 228) if warm else (214, 224, 255)
        if all(ord(c) < 128 for c in unit):
            # a Latin unit ("dB") is not an Arabic run - bidi would reverse it
            caps(d, (x1 - 10, y1 - 19), unit, 9, ucol, 1.4, anchor="ra", bold=True)
        else:
            ar_text(d, (x1 - 10, y1 - 24), unit, 11, ucol, anchor="ra")
    return canvas


CARBON = Image.open(ASSETS / "src" / "plate_carbon.png").convert("RGB")
_CARD_TEX: dict[tuple[int, int, int], Image.Image] = {}


def card_texture(size, seed=0):
    """A card face cut from the carbon, then lit: brighter than the plate so it
    reads as raised, with the light falling off towards the bottom edge."""
    key = (size[0], size[1], seed)
    if key in _CARD_TEX:
        return _CARD_TEX[key]
    w, h = size
    tw, th = CARBON.size
    ox = (seed * 173) % max(1, tw - w)
    oy = (seed * 97) % max(1, th - h)
    tex = CARBON.crop((ox, oy, ox + w, oy + h)).resize((w, h), Image.LANCZOS)
    tex = tex.point(lambda v: min(255, int(v * 1.17 + 7))).convert("RGBA")

    def veil(colour, top_a, bot_a, power):
        m = Image.new("L", (1, h))
        for y in range(h):
            t = (y / max(1, h - 1)) ** power
            m.putpixel((0, y), int(top_a + (bot_a - top_a) * t))
        m = m.resize((w, h), Image.BILINEAR)
        lay = Image.new("RGBA", (w, h), colour + (255,))
        lay.putalpha(m)
        tex.alpha_composite(lay)

    veil((255, 255, 255), 34, 0, 2.2)     # light catching the top edge
    veil((0, 0, 0), 0, 52, 1.6)           # the bottom tucks away from the light
    _CARD_TEX[key] = tex
    return tex


def plate_card(canvas, box, radius=12, seed=0):
    """A card cut from the same anodised material, lit from above."""
    x0, y0, x1, y1 = box
    size = (x1 - x0, y1 - y0)
    m = rounded_mask(size, radius)
    sh = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", size, (0, 0, 0, 175)), (x0, y0 + 8), m)
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(11)))
    body = card_texture(size, seed).convert("RGBA")
    body.putalpha(m)
    canvas.alpha_composite(body, (x0, y0))
    # top light, bottom shade, hairline rim: the three cues that make a surface a panel
    over(canvas, lambda d: (d.rounded_rectangle(box, radius=radius, outline=(255, 255, 255, 34)),
                            d.line([(x0 + radius, y0 + 1), (x1 - radius, y0 + 1)],
                                   fill=(255, 255, 255, 52)),
                            d.line([(x0 + radius, y1 - 1), (x1 - radius, y1 - 1)],
                                   fill=(0, 0, 0, 190))))
    return canvas


def caps_width(d, s, size=10, tracking=2.4, bold=False):
    f = font(FONT_B if bold else FONT_R, size)
    return sum(d.textlength(ch, font=f) + tracking for ch in s) - tracking


# ---------------------------------------------------------------- the graph maths
def fx(f):
    x0, _, x1, _ = GRAPH
    return x0 + (math.log10(f) - math.log10(F_MIN)) / (math.log10(F_MAX) - math.log10(F_MIN)) * (x1 - x0)


def dy(db):
    _, y0, _, y1 = GRAPH
    return y1 - (db - DB_MIN) / (DB_MAX - DB_MIN) * (y1 - y0)


def correction_db(f, bands, hpf):
    return (hpf_db(f, hpf) if hpf > 0 else 0.0) + sum(peak_db(f, *b) for b in bands)


def analyser(seed=7):
    rnd = random.Random(seed)

    def envelope(f):
        db = -1.15 * math.log2(max(f, 40.0) / 620.0)
        for fc, bw, amp in ((620, 210, 12.5), (1850, 430, 8.2), (2950, 720, 4.4), (830, 400, -3.6)):
            db += amp / (1.0 + ((f - fc) / bw) ** 2)
        db -= 30.0 / (1.0 + (6800.0 / max(f, 60.0)) ** 2)
        db -= 9.0 / (1.0 + (f / 68.0) ** 2)
        return db

    pts, f, i = [], F_MIN, 0
    while f < F_MAX:
        v = envelope(f) + (-3.4 if i % 3 == 0 else rnd.uniform(-0.5, 0.9)) + 0.22 * math.sin(i / 6.0)
        pts.append((f, v))
        f *= 2 ** (1 / 12)
        i += 1
    lo = min(v for _, v in pts)
    hi = max(v for _, v in pts)
    return [(f, dy(-15.0 + (v - lo) / max(1e-6, hi - lo) * 16.0)) for f, v in pts]


def draw_graph(canvas, bands, hpf, tracked_hz=600.0):
    x0, y0, x1, y1 = GRAPH
    size = (x1 - x0, y1 - y0)
    # the well: darker than the plate, with the glass reflections baked in as gradients
    well = Image.new("RGBA", size, (7, 8, 10, 255))
    for (cxr, cyr, r, col, a) in ((0.20, 0.02, 0.60, BLUE, 34), (0.84, 0.95, 0.55, AMBER, 12)):
        g = Image.new("RGBA", size, (0, 0, 0, 0))
        rr = int(max(size) * r)
        ImageDraw.Draw(g).ellipse([cxr * size[0] - rr, cyr * size[1] - rr,
                                   cxr * size[0] + rr, cyr * size[1] + rr], fill=col + (a,))
        well = Image.alpha_composite(well, g.filter(ImageFilter.GaussianBlur(rr / 3)))
    well.putalpha(rounded_mask(size, 14))
    canvas.alpha_composite(well, (x0, y0))

    def grid(d):
        f = F_MIN
        while f <= F_MAX:
            x = fx(f)
            decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
            d.line([(x, y0 + 26), (x, y1)], fill=(255, 255, 255, 24 if decade else 10))
            f *= 2 ** (1 / 3)
        for db in (-12, -6, 0, 6, 12):
            y = dy(db)
            d.line([(x0, y), (x1, y)], fill=(255, 255, 255, 30 if db == 0 else 12))
            if db:
                caps(d, (x1 - 12, y - 11), f"{db:+d}", 8, (255, 255, 255, 100), 1.0, anchor="ra")
        for f, lab in ((31.5, "31"), (63, "63"), (125, "125"), (250, "250"), (500, "500"),
                       (1000, "1k"), (2000, "2k"), (4000, "4k"), (8000, "8k"), (16000, "16k")):
            x = fx(f)
            d.line([(x, y0 + 4), (x, y0 + 22)], fill=(255, 255, 255, 30))
            caps(d, (x, y0 + 6), lab, 8, (255, 255, 255, 120), 1.2, anchor="ma", bold=True)
    over(canvas, grid)

    wm = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    caps(ImageDraw.Draw(wm), ((x0 + x1) / 2, y0 + 50), "R E S T O R E", 104,
         (255, 255, 255, 10), 3.2, anchor="ma", bold=True)
    canvas.alpha_composite(wm.filter(ImageFilter.GaussianBlur(0.6)))

    pts = analyser()

    # the spectrum: one filled shape, cut from a vertical gradient so the mass
    # fades into the floor instead of sitting on it as a flat blue slab
    mask = Image.new("L", size, 0)
    md = ImageDraw.Draw(mask)
    poly = [(fx(f) - x0, y - y0) for f, y in pts] + [(size[0], size[1]), (0, size[1])]
    md.polygon(poly, fill=255)
    grad = Image.new("RGBA", size)
    gp = grad.load()
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        col = mix((126, 154, 255), (30, 40, 96), min(1.0, t * 1.1))
        a = int(46 + 168 * (1.0 - t) ** 1.05)
        for x in range(size[0]):
            gp[x, y] = col + (a,)
    spec = Image.new("RGBA", size, (0, 0, 0, 0))
    spec.paste(grad, (0, 0), mask)
    canvas.alpha_composite(spec, (x0, y0))

    edge = [(fx(f), y) for f, y in pts]
    glow(canvas, lambda d: d.line(edge, fill=BLUE_HOT + (150,), width=3, joint="curve"), blur=9, alpha=140)
    over(canvas, lambda d: d.line(edge, fill=(214, 225, 255, 232), width=1, joint="curve"))

    # the band that follows the note: two ghosts, then the live column
    for off, a in ((-0.075, 30), (-0.038, 52)):
        gx = fx(tracked_hz * (1 + off))
        over(canvas, lambda d, gx=gx: d.line([(gx, y0 + 26), (gx, y1)], fill=AMBER + (a,), width=2))
    tx = fx(tracked_hz)
    glow(canvas, lambda d: d.line([(tx, y0 + 26), (tx, y1)], fill=AMBER + (120,), width=6), blur=12, alpha=150)
    over(canvas, lambda d: (d.line([(tx - 7, y0 + 26), (tx - 7, y1)], fill=AMBER + (110,)),
                            d.line([(tx + 7, y0 + 26), (tx + 7, y1)], fill=AMBER + (110,))))

    # correction curve
    cpts = []
    for i in range(561):
        f = F_MIN * (F_MAX / F_MIN) ** (i / 560)
        cpts.append((fx(f), dy(max(DB_MIN, min(DB_MAX, correction_db(f, bands, hpf))))))
    glow(canvas, lambda d: d.line(cpts, fill=AMBER + (210,), width=5, joint="curve"), blur=15, alpha=210)
    glow(canvas, lambda d: d.line(cpts, fill=AMBER_HOT + (160,), width=2, joint="curve"), blur=3, alpha=180)
    over(canvas, lambda d: d.line(cpts, fill=(255, 228, 172, 255), width=2, joint="curve"))

    # handles
    d = ImageDraw.Draw(canvas)
    for bf, _q, _g in bands:
        x, y = fx(bf), dy(correction_db(bf, bands, hpf))
        glow(canvas, lambda dd, x=x, y=y: dd.ellipse([x - 7, y - 7, x + 7, y + 7], fill=AMBER),
             blur=9, alpha=210)
        d = ImageDraw.Draw(canvas)
        d.ellipse([x - 8, y - 8, x + 8, y + 8], fill=(10, 10, 12), outline=AMBER, width=2)
        d.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5], fill=AMBER_HOT)
        lab = f"{bf/1000:.1f}k" if bf >= 1000 else f"{bf:.0f}"
        txt = f"{lab} · {correction_db(bf, bands, hpf):+.1f}"
        w = 8 + 5.4 * len(txt)
        over(canvas, lambda dd, x=x, y=y, w=w: (dd.rounded_rectangle([x - w, y + 13, x + w, y + 31],
                                                                     radius=8, fill=(0, 0, 0, 190)),
                                                dd.rounded_rectangle([x - w, y + 13, x + w, y + 31],
                                                                     radius=8,
                                                                     outline=(255, 255, 255, 26))))
        caps(d, (x, y + 16), txt, 9, AMBER_HOT, 0.4, anchor="ma", bold=True)

    tag = f"{tracked_hz:.0f} Hz"
    over(canvas, lambda dd: (dd.rounded_rectangle([tx - 34, y0 + 30, tx + 34, y0 + 48], radius=7,
                                                  fill=(0, 0, 0, 205)),
                             dd.rounded_rectangle([tx - 34, y0 + 30, tx + 34, y0 + 48], radius=7,
                                                  outline=AMBER + (150,))))
    d = ImageDraw.Draw(canvas)
    caps(d, (tx, y0 + 34), tag, 8, AMBER_HOT, 0.8, anchor="ma", bold=True)
    d.polygon([(tx - 5, y0 + 52), (tx + 5, y0 + 52), (tx, y0 + 58)], fill=AMBER_HOT)

    over(canvas, lambda dd: (dd.rounded_rectangle([x0, y0, x1, y1], radius=14, outline=(0, 0, 0, 210)),
                             dd.line([(x0 + 18, y0 + 1), (x1 - 18, y0 + 1)], fill=(255, 255, 255, 44))))
    canvas.alpha_composite(inner_shadow(size, 14, 12, 155), (x0, y0))


def build():
    canvas = Image.open(ASSETS / "panel_plate.png").convert("RGBA")
    mask = rounded_mask((W, H), 20)
    sh = Image.new("RGBA", (W + 80, H + 80), (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", (W, H), (0, 0, 0, 210)), (40, 40), mask)
    sh = sh.filter(ImageFilter.GaussianBlur(22)).crop((40, 40, 40 + W, 40 + H))
    page = Image.new("RGBA", (W, H), (6, 6, 8, 255))
    page.alpha_composite(sh)
    page.alpha_composite(canvas, (0, 0))
    canvas = page
    canvas.putalpha(mask)

    # inner shadow around the whole window, so the plate sits *inside* a frame
    canvas.alpha_composite(inner_shadow((W, H), 20, 14, 120))
    over(canvas, lambda d: d.rounded_rectangle([0, 0, W - 1, H - 1], radius=20,
                                               outline=(255, 255, 255, 36)))
    d = ImageDraw.Draw(canvas)

    # ---------------------------------------------------------------- header
    over(canvas, lambda dd: dd.line([(28, 70), (W - 28, 70)], fill=(255, 255, 255, 18)))
    caps(d, (44, 30), "AEQ", 12, (255, 255, 255, 240), 4.2, bold=True)
    caps(d, (90, 30), "RESTORE", 12, (255, 255, 255, 100), 4.2)

    px0, px1 = 452, 736
    over(canvas, lambda dd: (dd.rounded_rectangle([px0, 24, px1, 58], radius=9, fill=(0, 0, 0, 120)),
                             dd.rounded_rectangle([px0, 24, px1, 58], radius=9,
                                                  outline=(255, 255, 255, 34)),
                             dd.line([(px0 + 10, 25), (px1 - 10, 25)], fill=(255, 255, 255, 30))))
    d = ImageDraw.Draw(canvas)
    for sx, sgn in ((px0 + 22, -1), (px1 - 22, 1)):
        d.line([(sx - 4 * sgn, 35), (sx, 41), (sx - 4 * sgn, 47)], fill=(255, 255, 255, 130), width=2)
    ar_text(d, ((px0 + px1) / 2, 33), HEADER_PILL, 12, TEXT, anchor="ma")

    push_button(canvas, 1080, 41, 30, on=True)     # a lit button as the "engaged" cue
    push_button(canvas, 1118, 41, 30, on=False)
    push_button(canvas, 1156, 41, 30, on=False)

    draw_graph(canvas, [(440.0, 1.1, -4.6), (3000.0, 1.0, 2.6), (12000.0, 0.7, 1.8)], 88.0)

    d = ImageDraw.Draw(canvas)
    caps(d, (GRAPH[0] + 16, GRAPH[1] + 32), "LEARNED", 8, AMBER_HOT, 2.4, bold=True)
    caps(d, (GRAPH[0] + 16, GRAPH[1] + 46), "24.9 s · 96 BANDS", 8, (255, 255, 255, 100), 1.8)
    tx = fx(600.0)
    ar_text(d, (tx, GRAPH[3] + 7), GRAPH_NOTE, 10, AMBER_HOT, anchor="ma")

    # ---------------------------------------------------------------- cards
    # One grammar per card, always the same: micro-caps title and its Arabic
    # name on the header line, two 22 px buttons at the right edge, a machined
    # knob on the left with the control's name cut underneath it, and a dark
    # glass readout on the right holding the number the DSP is reporting.
    card_y0, card_y1 = CARD_Y0, CARD_Y1
    xs = CARD_XS
    CW = CARD_W
    for i, (eng, arl, kv, klab, val, unit, l1, l2, warm) in enumerate(CARDS):
        cx0 = xs[i]
        cx1 = cx0 + CW
        plate_card(canvas, (cx0, card_y0, cx1, card_y1), seed=i + 1)
        d = ImageDraw.Draw(canvas)
        caps(d, (cx0 + 16, card_y0 + 12), eng, 9, (255, 255, 255, 176), 2.4, bold=True)
        tw = caps_width(d, eng, 9, 2.4, True)
        ar_text(d, (cx0 + 22 + tw, card_y0 + 10), arl, 11, TEXT)
        push_button(canvas, cx1 - 51, card_y0 + 21, 22, on=False)
        push_button(canvas, cx1 - 29, card_y0 + 21, 22, on=(eng == "TRACK"))
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.line(
            [(cx0 + 14, card_y0 + 38), (cx1 - 14, card_y0 + 38)], fill=(255, 255, 255, 22)))

        knob(canvas, cx0 + 58, card_y0 + 90, 96, kv, warm=False)
        d = ImageDraw.Draw(canvas)
        ar_text(d, (cx0 + 58, card_y0 + 136), klab, 11, (206, 201, 194), anchor="ma")
        lcd(canvas, (cx0 + 112, card_y0 + 48, cx1 - 14, card_y0 + 96), val, unit,
            warm=warm, big=26)
        d = ImageDraw.Draw(canvas)
        if l1:
            ar_text(d, (cx1 - 14, card_y0 + 103), l1, 11, (196, 191, 184), anchor="ra")
        if l2:
            ar_text(d, (cx1 - 14, card_y0 + 120), l2, 11, (168, 164, 158), anchor="ra")

    # output card: the one destructive control gets its own latching row
    ox = xs[3]
    over(canvas, lambda dd: dd.rounded_rectangle([ox + 112, 556, ox + CW - 14, 590], radius=9,
                                                 fill=(0, 0, 0, 150)))
    over(canvas, lambda dd: dd.rounded_rectangle([ox + 112, 556, ox + CW - 14, 590], radius=9,
                                                 outline=(255, 255, 255, 34)))
    push_button(canvas, ox + 132, 573, 24, on=False)
    d = ImageDraw.Draw(canvas)
    caps(d, (ox + 150, 568), "BYPASS", 8, (255, 255, 255, 150), 1.6, bold=True)
    ar_text(d, (ox + CARD_W - 26, 568), BYPASS_NOTE, 10, DIM, anchor="ra")

    # ---------------------------------------------------------------- bottom strip
    strip = STRIP
    over(canvas, lambda dd: (dd.rounded_rectangle(strip, radius=10, fill=(0, 0, 0, 130)),
                             dd.rounded_rectangle(strip, radius=10, outline=(255, 255, 255, 24)),
                             dd.line([(44, 625), (1136, 625)], fill=(255, 255, 255, 26))))
    d = ImageDraw.Draw(canvas)
    sx0, sx1, sy = 60, 300, 650
    over(canvas, lambda dd: dd.rounded_rectangle([sx0, sy - 2, sx1, sy + 2], radius=2,
                                                 fill=(255, 255, 255, 28)))
    hx = sx0 + (sx1 - sx0) * 0.22
    over(canvas, lambda dd: dd.line([(sx0, sy), (hx, sy)], fill=AMBER + (235,), width=4))
    glow(canvas, lambda dd: dd.ellipse([hx - 7, sy - 7, hx + 7, sy + 7], fill=AMBER), blur=8, alpha=190)
    d = ImageDraw.Draw(canvas)
    d.ellipse([hx - 6, sy - 6, hx + 6, sy + 6], fill=(250, 240, 230))
    caps(d, (sx0, 630), "HP", 8, (255, 255, 255, 130), 1.8, bold=True)
    caps(d, (sx1 - 4, 630), "88 Hz", 8, AMBER_HOT, 1.2, anchor="ra", bold=True)
    ar_text(d, (470, 636), FOOTER, 12, TEXT, anchor="ma")
    caps(d, (1132, 630), "V1.3.0", 9, FAINT, 1.8, anchor="ra")
    for i in range(12):
        on = i < 9
        over(canvas, lambda dd, i=i, on=on: dd.rounded_rectangle(
            [1000 + i * 8, 650, 1005 + i * 8, 655], radius=2,
            fill=AMBER + (210,) if on else (255, 255, 255, 30)))

    # edge meters
    for mx in (20, W - 20):
        over(canvas, lambda dd: dd.rounded_rectangle([mx - 1, 84, mx + 1, 598], radius=1,
                                                     fill=(255, 255, 255, 20)))
        over(canvas, lambda dd: dd.rounded_rectangle([mx - 1, 300, mx + 1, 470], radius=1,
                                                     fill=AMBER + (130,)))

    canvas.putalpha(mask)
    canvas.save(OUT)
    print(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(build())
