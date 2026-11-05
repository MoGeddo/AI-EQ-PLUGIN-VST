#!/usr/bin/env python3
"""ui_v5.py - the panel, third attempt: In The Mix's grammar, our content.

The v4 render was one engraved plate with six knobs in a row.  The verdict on it was
that it still is not what the store actually sells, and looking at Bassmaker properly
- the panel, not the marketing shots - the difference is structural, not cosmetic:

  1. a HEADER BAR: wordmark far left, a centred pill that names the current state
     with chevrons on both sides, and round icon buttons at the far right;
  2. ONE BIG WELL for everything graphical, with the frequency labels along its TOP
     edge, a faint grid, hard spectrum teeth behind the fill, and a giant embossed
     wordmark sitting *inside* the glass;
  3. a ROW OF CARDS, not a row of knobs.  Each card is its own object: a micro-caps
     title, two tiny icon buttons, a big knob with a metallic ring and a wedge, and
     a *tinted* LCD with big numerals - blue for one section, gold for the next.  The
     colour of the LCD is how the eye groups them;
  4. a BOTTOM STRIP that is a different material from the cards: a thin HP slider,
     a BYPASS chip, and the two output knobs at the right;
  5. thin vertical meters hugging the left and right edges of the whole panel.

Everything here is our content in that grammar, because our panel has one thing his
does not: a band that *moves*.  The tracked band is drawn at three positions with
decreasing opacity, so the picture says "this follows the note" without a caption.

    /tmp/venv/bin/python design/preview/ui_v5.py
"""
from __future__ import annotations

import math
import pathlib
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
# the text shaping, surface and curve helpers are shared with v4 - one copy of each
from ui_v4 import (  # noqa: E402
    FONT_B, FONT_R, ar_text, caps, cmap_of, drop_shadow, font, glow, hpf_db,
    inner_shadow, over, peak_db, rounded_mask, shape, vertical_gradient,
)

ROOT = HERE.parents[1]
W, H = 1180, 700
OUT = HERE / "ui_v5_1180x700.png"

# ------------------------------------------------------------------ palette
# In The Mix runs a near-black panel, one cold and one warm family, and nothing else.
PAGE = (8, 8, 10)
BASE = (21, 22, 26)
TOP = (32, 34, 39)
CARD_T = (30, 31, 36)
CARD_B = (22, 23, 27)
WELL_T = (17, 18, 22)
WELL_B = (9, 9, 12)
TEXT = (241, 238, 233)
DIM = (156, 152, 147)        # >= 4.5:1 on the card
FAINT = (122, 118, 115)      # decorative only
AMBER = (247, 166, 84)
AMBER_HOT = (255, 219, 165)
BLUE = (104, 133, 240)
BLUE_HOT = (176, 194, 255)
IVORY = (228, 227, 224)

GRAPH = (32, 78, 1148, 424)
F_MIN, F_MAX = 30.0, 18000.0
DB_MIN, DB_MAX = -18.0, 18.0
SR = 48000.0


def fx(f):
    x0, _, x1, _ = GRAPH
    return x0 + (math.log10(f) - math.log10(F_MIN)) / (math.log10(F_MAX) - math.log10(F_MIN)) * (x1 - x0)


def dy(db):
    _, y0, _, y1 = GRAPH
    return y1 - (db - DB_MIN) / (DB_MAX - DB_MIN) * (y1 - y0)


def correction_db(f, bands, hpf):
    return (hpf_db(f, hpf) if hpf > 0 else 0.0) + sum(peak_db(f, *b) for b in bands)


def card(canvas, box, radius=12):
    """One section card: gradient body, hairline rim, soft shadow."""
    x0, y0, x1, y1 = box
    size = (x1 - x0, y1 - y0)
    shape_mask = rounded_mask(size, radius)
    full_mask = Image.new("L", canvas.size, 0)
    full_mask.paste(shape_mask, (x0, y0))
    canvas = drop_shadow(canvas, full_mask, (0, 6), 9, 150)
    body = vertical_gradient(size, CARD_T, CARD_B).convert("RGBA")
    gloss = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(gloss).ellipse([-size[0] * 0.3, -size[1] * 1.1, size[0] * 1.3, size[1] * 0.45],
                                  fill=(255, 255, 255, 12))
    body = Image.alpha_composite(body, gloss.filter(ImageFilter.GaussianBlur(12)))
    body.putalpha(shape_mask)
    canvas.alpha_composite(body, (x0, y0))
    over(canvas, lambda d: (d.rounded_rectangle(box, radius=radius, outline=(255, 255, 255, 26)),
                            d.line([(x0 + radius, y0 + 1), (x1 - radius, y0 + 1)],
                                   fill=(255, 255, 255, 34))))
    return canvas


def lcd(canvas, box, value, unit, warm=True, big=27):
    """The dark tinted readout with the big numerals - the thing the eye groups by."""
    x0, y0, x1, y1 = box
    col = AMBER_HOT if warm else BLUE_HOT
    tint = (255, 176, 92, 26) if warm else (110, 140, 250, 30)
    over(canvas, lambda d: (d.rounded_rectangle(box, radius=7, fill=(6, 7, 9, 210)),
                            d.rounded_rectangle(box, radius=7, fill=tint),
                            d.rounded_rectangle(box, radius=7, outline=(255, 255, 255, 22)),
                            d.line([(x0 + 6, y0 + 2), (x1 - 6, y0 + 2)],
                                   fill=(0, 0, 0, 180))))
    d = ImageDraw.Draw(canvas)
    f = font(FONT_B, big)
    wv = d.textlength(value, font=f)
    fu = font(FONT_R, 12)
    wu = d.textlength(unit, font=fu)
    total = wv + (6 + wu if unit else 0)
    x = (x0 + x1) / 2 - total / 2
    glow(canvas, lambda dd: dd.text((x, y0 + (y1 - y0) / 2 - big * 0.66), value, font=f, fill=col),
         blur=7, alpha=110)
    d = ImageDraw.Draw(canvas)
    d.text((x, y0 + (y1 - y0) / 2 - big * 0.66), value, font=f, fill=col)
    if unit:
        d.text((x + wv + 6, y1 - 18), unit, font=fu, fill=(255, 255, 255, 120))
    return canvas


def knob(canvas, cx, cy, r, value, accent=False, glyph=None):
    """A big knob: soft shadow, brushed body, dark ring, gloss arc, wedge pointer."""
    mask = Image.new("L", (int(r * 2 + 2), int(r * 2 + 2)), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, mask.size[0] - 1, mask.size[1] - 1], fill=255)
    sh = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    sh.paste(Image.new("RGBA", mask.size, (0, 0, 0, 165)), (int(cx - r), int(cy - r + 8)), mask)
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(11)))

    body = Image.new("RGBA", (int(r * 2), int(r * 2)), (0, 0, 0, 0))
    bd = ImageDraw.Draw(body)
    for i in range(int(r)):
        t = i / r
        c = int(88 - 46 * t)
        bd.ellipse([i, i, body.size[0] - 1 - i, body.size[1] - 1 - i], fill=(c, c + 1, c + 5, 255))
    # brushed metal: a few faint horizontal arcs
    for k in range(7):
        yy = int(r * (0.35 + 0.09 * k))
        bd.arc([2, yy - r * 0.5, body.size[0] - 2, yy + r * 0.6], 190, 350,
               fill=(255, 255, 255, 10), width=1)
    gloss = Image.new("RGBA", body.size, (0, 0, 0, 0))
    ImageDraw.Draw(gloss).ellipse([int(r * 0.20), int(r * 0.08), int(r * 1.80), int(r * 1.00)],
                                  fill=(255, 255, 255, 30))
    body = Image.alpha_composite(body, gloss.filter(ImageFilter.GaussianBlur(r * 0.20)))
    body.putalpha(rounded_mask(body.size, int(r)))
    canvas.alpha_composite(body, (int(cx - r), int(cy - r)))

    # the recessed groove the pointer sits in
    over(canvas, lambda d: (d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(0, 0, 0, 220)),
                            d.arc([cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1], 188, 352,
                                  fill=(255, 255, 255, 54), width=2)))

    def ticks(d):
        for i in range(21):
            aa = math.radians(-135 + 13.5 * i)
            r0 = r + 5
            r1 = r + (10 if i % 5 == 0 else 7)
            d.line([(cx + r0 * math.sin(aa), cy - r0 * math.cos(aa)),
                    (cx + r1 * math.sin(aa), cy - r1 * math.cos(aa))],
                   fill=(255, 255, 255, 60) if i % 5 else (255, 255, 255, 100), width=1)
    over(canvas, ticks)

    if accent:
        glow(canvas, lambda d: d.arc([cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1], 188, 352,
                                     fill=AMBER + (170,), width=2), blur=5, alpha=180)
    ang = math.radians(-135 + 270 * max(0.0, min(1.0, value)))
    col = AMBER if accent else (246, 243, 237)
    tipx, tipy = cx + (r - 7) * math.sin(ang), cy - (r - 7) * math.cos(ang)
    glow(canvas, lambda d: d.line([(cx, cy), (tipx, tipy)], fill=col, width=4), blur=6, alpha=200)
    d = ImageDraw.Draw(canvas)
    d.line([(cx, cy), (tipx, tipy)], fill=col, width=3)
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(18, 19, 22), outline=(255, 255, 255, 34))
    if glyph:
        caps(d, (cx, cy + r * 0.42), glyph, 8, (255, 255, 255, 90), 1.4, anchor="ma")
    return canvas


def learn_button(canvas, cx, cy, r, progress=0.82, frozen=True):
    """The one control that matters, drawn as a round transport button."""
    over(canvas, lambda d: d.ellipse([cx - r - 9, cy - r - 9, cx + r + 9, cy + r + 9],
                                     fill=(0, 0, 0, 90)))
    # progress ring: how much of the take has been heard
    over(canvas, lambda d: d.arc([cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6], 0, 360,
                                 fill=(255, 255, 255, 22), width=3))
    if progress > 0:
        glow(canvas, lambda d: d.arc([cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6], -90,
                                     -90 + 360 * progress, fill=AMBER + (210,), width=3),
             blur=6, alpha=170)
    body = vertical_gradient((int(r * 2), int(r * 2)), (46, 47, 53), (24, 25, 29)).convert("RGBA")
    body.putalpha(rounded_mask(body.size, r))
    canvas.alpha_composite(body, (int(cx - r), int(cy - r)))
    over(canvas, lambda d: (d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(0, 0, 0, 210)),
                            d.arc([cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1], 186, 340,
                                  fill=(255, 255, 255, 46))))
    d = ImageDraw.Draw(canvas)
    if frozen:                                    # ✓ - the pass is done and frozen
        w = 8
        glow(canvas, lambda dd: dd.line([(cx - w, cy), (cx - w * 0.25, cy + w * 0.8),
                                         (cx + w, cy - w * 0.85)],
                                        fill=AMBER, width=4, joint="curve"), blur=8, alpha=200)
        d = ImageDraw.Draw(canvas)
        d.line([(cx - w, cy), (cx - w * 0.25, cy + w * 0.8), (cx + w, cy - w * 0.85)],
               fill=AMBER_HOT, width=4, joint="curve")
    else:                                         # ■ - listening, press to stop
        d.rounded_rectangle([cx - 7, cy - 7, cx + 7, cy + 7], radius=2, fill=AMBER)
    return canvas


def icon_button(canvas, cx, cy, r=11, kind="dot"):
    over(canvas, lambda d: (d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 14)),
                            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 255, 255, 40))))
    d = ImageDraw.Draw(canvas)
    c = (255, 255, 255, 150)
    if kind == "gear":
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], outline=c, width=1)
        for k in range(6):
            a = k * math.pi / 3
            d.line([(cx + 4 * math.cos(a), cy + 4 * math.sin(a)),
                    (cx + 6.5 * math.cos(a), cy + 6.5 * math.sin(a))], fill=c, width=1)
    elif kind == "loop":
        d.arc([cx - 5, cy - 5, cx + 5, cy + 5], 40, 320, fill=c, width=1)
        d.ellipse([cx + 3, cy - 6, cx + 6, cy - 3], fill=c)
    else:
        d.ellipse([cx - 2, cy - 2, cx + 2, cy + 2], fill=c)
    return canvas


def analyser(seed=7):
    """A vocal spectrum as teeth, not as a smooth line.

    The reference panel's graph is spiky because it is an FFT of a voice: a picket fence
    of partials with a formant envelope over them.  A smooth curve - what v4 drew - reads
    as "an EQ" instead of "a spectrum", and that is most of why it did not look like the
    hardware-inspired plugins.  So: envelope + a tooth at every 1/12 octave.
    """
    rnd = random.Random(seed)

    def envelope(f):
        db = -1.15 * math.log2(max(f, 40.0) / 620.0)
        for fc, bw, amp in ((620, 210, 12.5), (1850, 430, 8.2), (2950, 720, 4.4), (830, 400, -3.6)):
            db += amp / (1.0 + ((f - fc) / bw) ** 2)
        db -= 30.0 / (1.0 + (6800.0 / max(f, 60.0)) ** 2)
        db -= 9.0 / (1.0 + (f / 68.0) ** 2)
        return db

    pts = []
    f = F_MIN
    i = 0
    while f < F_MAX:
        # every third bin is a deep notch: that is what makes a spectrum read as teeth
        # instead of as a curve, and the reference's graph is all teeth
        tooth = -3.4 if i % 3 == 0 else rnd.uniform(-0.5, 0.9)
        v = envelope(f) + tooth + 0.22 * math.sin(i / 6.0)
        pts.append((f, v))
        f *= 2 ** (1 / 12)
        i += 1
    lo = min(v for _, v in pts)
    hi = max(v for _, v in pts)
    top, bottom = 1.0, -15.0            # the spectrum occupies the lower half of the well
    return [(f, dy(bottom + (v - lo) / max(1e-6, hi - lo) * (top - bottom))) for f, v in pts]


def draw_analyser(canvas, pts):
    """Vertical bars to the floor with a bright edge: the fill *is* the comb."""
    x0, y0, x1, y1 = GRAPH

    def bars(d):
        for i in range(len(pts) - 1):
            x, y = pts[i]
            xn = pts[i + 1][0]
            px, pxn = fx(x), fx(xn)
            d.rectangle([px, y, max(px + 1.2, pxn - 0.6), y1], fill=BLUE + (88,))
    over(canvas, bars)

    edge = [(fx(f), y) for f, y in pts]
    glow(canvas, lambda d: d.line(edge, fill=BLUE_HOT + (150,), width=3, joint="curve"), blur=9, alpha=140)
    over(canvas, lambda d: d.line(edge, fill=(206, 219, 255, 225), width=1, joint="curve"))


def draw_tracked_band(canvas, hz, w=15):
    """Our unique thing: a band that follows the note.  Three ghost positions, then live.

    Drawn as a soft column with dashed edges, not a solid bar: it has to read as "this
    one moves" without shouting over the curve, and a solid block did shout.
    """
    x0, y0, x1, y1 = GRAPH
    for off, a in ((-0.075, 26), (-0.038, 48)):
        gx = fx(hz * (1 + off))
        over(canvas, lambda d, gx=gx: d.line([(gx, y0 + 26), (gx, y1)], fill=AMBER + (a,), width=2))

    x = fx(hz)
    glow(canvas, lambda d: d.line([(x, y0 + 26), (x, y1)], fill=AMBER + (120,), width=6), blur=12, alpha=150)
    col = Image.new("RGBA", (int(w), int(y1 - y0)), (0, 0, 0, 0))
    cd = ImageDraw.Draw(col)
    for i in range(int(w)):
        t = 1.0 - abs(i - w / 2) / (w / 2)
        cd.line([(i, 0), (i, col.size[1])], fill=AMBER + (int(30 * t),))
    canvas.alpha_composite(col, (int(x - w / 2), int(y0)))
    for sgn in (-1, 1):
        ex = x + sgn * w / 2
        over(canvas, lambda d, ex=ex: d.line([(ex, y0 + 26), (ex, y1)], fill=AMBER + (110,), width=1))

    # a small tag on the band itself, under the frequency ruler
    tag = "600 Hz"
    tw = 34
    over(canvas, lambda d: (d.rounded_rectangle([x - tw, y0 + 30, x + tw, y0 + 48], radius=7,
                                                fill=(0, 0, 0, 200)),
                            d.rounded_rectangle([x - tw, y0 + 30, x + tw, y0 + 48], radius=7,
                                                outline=AMBER + (150,))))
    dd = ImageDraw.Draw(canvas)
    caps(dd, (x, y0 + 34), tag, 8, AMBER_HOT, 0.8, anchor="ma", bold=True)
    dd.polygon([(x - 5, y0 + 52), (x + 5, y0 + 52), (x, y0 + 58)], fill=AMBER_HOT)


def draw_curve(canvas, bands, hpf):
    pts = []
    for i in range(561):
        f = F_MIN * (F_MAX / F_MIN) ** (i / 560)
        pts.append((fx(f), dy(max(DB_MIN, min(DB_MAX, correction_db(f, bands, hpf))))))
    glow(canvas, lambda d: d.line(pts, fill=AMBER + (210,), width=5, joint="curve"), blur=15, alpha=210)
    glow(canvas, lambda d: d.line(pts, fill=AMBER_HOT + (160,), width=2, joint="curve"), blur=3, alpha=180)
    over(canvas, lambda d: d.line(pts, fill=(255, 228, 172, 255), width=2, joint="curve"))


def handles(canvas, bands, hpf):
    d = ImageDraw.Draw(canvas)
    for bf, _bq, _bg in bands:
        x, y = fx(bf), dy(correction_db(bf, bands, hpf))
        glow(canvas, lambda dd, x=x, y=y: dd.ellipse([x - 7, y - 7, x + 7, y + 7], fill=AMBER),
             blur=9, alpha=210)
        d = ImageDraw.Draw(canvas)
        d.ellipse([x - 8, y - 8, x + 8, y + 8], fill=(12, 12, 15), outline=AMBER, width=2)
        d.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5], fill=AMBER_HOT)
        lab = f"{bf/1000:.1f}k" if bf >= 1000 else f"{bf:.0f}"
        gain = correction_db(bf, bands, hpf)
        txt = f"{lab} · {gain:+.1f}"
        w = 8 + 5.4 * len(txt)
        d.rounded_rectangle([x - w, y + 13, x + w, y + 31], radius=8, fill=(0, 0, 0, 170))
        over(canvas, lambda dd, x=x, y=y, w=w: dd.rounded_rectangle(
            [x - w, y + 13, x + w, y + 31], radius=8, outline=(255, 255, 255, 26)))
        caps(d, (x, y + 16), txt, 9, AMBER_HOT, 0.4, anchor="ma", bold=True)


def build():
    canvas = Image.new("RGBA", (W, H), PAGE + (255,))
    mask = rounded_mask((W, H), 20)
    canvas = drop_shadow(canvas, mask, (0, 16), 30, 220)

    panel = vertical_gradient((W, H), TOP, BASE).convert("RGBA")
    vig = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    vd = ImageDraw.Draw(vig)
    for i in range(80):
        vd.rectangle([i, i, W - 1 - i, H - 1 - i], outline=(0, 0, 0, int(2 + i * 0.85)))
    panel = Image.alpha_composite(panel, vig.filter(ImageFilter.GaussianBlur(26)))
    panel.putalpha(mask)
    canvas.alpha_composite(panel)
    over(canvas, lambda d: d.rounded_rectangle([0, 0, W - 1, H - 1], radius=20,
                                               outline=(255, 255, 255, 30)))

    d = ImageDraw.Draw(canvas)

    # ---------------------------------------------------------------- header
    over(canvas, lambda dd: dd.line([(28, 70), (W - 28, 70)], fill=(255, 255, 255, 16)))
    caps(d, (44, 32), "AEQ", 12, (255, 255, 255, 235), 4.2, bold=True)
    caps(d, (44 + 46, 32), "RESTORE", 12, (255, 255, 255, 96), 4.2)

    # centred state pill, with the chevrons the store's plugins use for A/B browsing
    px0, px1 = 452, 736
    over(canvas, lambda dd: (dd.rounded_rectangle([px0, 26, px1, 58], radius=9,
                                                  fill=(255, 255, 255, 10)),
                             dd.rounded_rectangle([px0, 26, px1, 58], radius=9,
                                                  outline=(255, 255, 255, 30))))
    d = ImageDraw.Draw(canvas)
    for sx, sgn in ((px0 + 22, -1), (px1 - 22, 1)):
        d.line([(sx - 4 * sgn, 36), (sx, 42), (sx - 4 * sgn, 48)], fill=(255, 255, 255, 120), width=2)
    ar_text(d, ((px0 + px1) / 2, 35), "التزام محفوظ · 4 باندات", 12, TEXT, anchor="ma")
    icon_button(canvas, 1084, 42, 12, "loop")
    icon_button(canvas, 1114, 42, 12, "dot")
    icon_button(canvas, 1146, 42, 12, "gear")

    # ---------------------------------------------------------------- well
    x0, y0, x1, y1 = GRAPH
    size = (x1 - x0, y1 - y0)
    well = vertical_gradient(size, WELL_T, WELL_B).convert("RGBA")
    for (cxr, cyr, r, col, a) in ((0.20, 0.06, 0.62, BLUE, 30), (0.84, 0.92, 0.55, AMBER, 14)):
        g = Image.new("RGBA", size, (0, 0, 0, 0))
        rr = int(max(size) * r)
        ImageDraw.Draw(g).ellipse([cxr * size[0] - rr, cyr * size[1] - rr,
                                   cxr * size[0] + rr, cyr * size[1] + rr], fill=col + (a,))
        well = Image.alpha_composite(well, g.filter(ImageFilter.GaussianBlur(rr / 3)))
    well.putalpha(rounded_mask(size, 14))
    canvas.alpha_composite(well, (x0, y0))

    def grid(dd):
        f = F_MIN
        while f <= F_MAX:
            x = fx(f)
            decade = abs(math.log10(f) - round(math.log10(f))) < 1e-6
            dd.line([(x, y0 + 26), (x, y1)], fill=(255, 255, 255, 24 if decade else 10))
            f *= 2 ** (1 / 3)
        for db in (-12, -6, 0, 6, 12):
            y = dy(db)
            dd.line([(x0, y), (x1, y)], fill=(255, 255, 255, 30 if db == 0 else 12))
            if db:
                caps(dd, (x1 - 12, y - 11), f"{db:+d}", 8, (255, 255, 255, 100), 1.0, anchor="ra")
        # frequency labels along the TOP edge - his panels put them there
        for f, lab in ((31.5, "31"), (63, "63"), (125, "125"), (250, "250"), (500, "500"),
                       (1000, "1k"), (2000, "2k"), (4000, "4k"), (8000, "8k"), (16000, "16k")):
            x = fx(f)
            dd.line([(x, y0 + 4), (x, y0 + 22)], fill=(255, 255, 255, 30))
            caps(dd, (x, y0 + 6), lab, 8, (255, 255, 255, 118), 1.2, anchor="ma", bold=True)
    over(canvas, grid)

    # giant embossed wordmark inside the glass
    wm = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    caps(ImageDraw.Draw(wm), ((x0 + x1) / 2, y0 + 52), "R E S T O R E", 104,
         (255, 255, 255, 9), 3.2, anchor="ma", bold=True)
    canvas.alpha_composite(wm.filter(ImageFilter.GaussianBlur(0.5)))
    caps(ImageDraw.Draw(canvas), ((x0 + x1) / 2, y0 + 53), "R E S T O R E", 104,
         (0, 0, 0, 55), 3.2, anchor="ma", bold=True)

    pts = analyser()
    draw_analyser(canvas, pts)

    # the tracked band, drawn before the curve so the curve reads on top of it
    draw_tracked_band(canvas, 600.0)
    bands = [(440.0, 1.1, -4.6), (3000.0, 1.0, 2.6), (12000.0, 0.7, 1.8)]
    hpf = 88.0
    draw_curve(canvas, bands, hpf)
    handles(canvas, bands, hpf)

    # well edge, rim light, inner shadow
    over(canvas, lambda dd: (dd.rounded_rectangle([x0, y0, x1, y1], radius=14, outline=(0, 0, 0, 210)),
                             dd.line([(x0 + 18, y0 + 1), (x1 - 18, y0 + 1)], fill=(255, 255, 255, 44))))
    canvas.alpha_composite(inner_shadow(size, 14, 12, 165), (x0, y0))

    # corner readouts inside the well
    d = ImageDraw.Draw(canvas)
    caps(d, (x0 + 16, y0 + 32), "LEARNED", 8, AMBER_HOT, 2.4, bold=True)
    caps(d, (x0 + 16, y0 + 46), "24.9 s · 96 BANDS", 8, (255, 255, 255, 96), 1.8)

    # the tracked band's own caption, in the gap under the well
    tx = fx(600.0)
    ar_text(d, (tx, y1 + 7), "الباند المتحرك بيثبّت 600 هرتز", 10, AMBER_HOT, anchor="ma")

    # ---------------------------------------------------------------- cards
    card_y0, card_y1 = 452, 612
    cards = [
        ("LEARN", "التعلّم", None),
        ("CURVE", "المنحنى", None),
        ("TRACK", "المتحرك", None),
        ("OUTPUT", "الخروج", None),
    ]
    xs = [32, 311, 590, 869]
    for (eng, arl, _), cx0 in zip(cards, xs):
        cx1 = cx0 + 267
        canvas = card(canvas, (cx0, card_y0, cx1, card_y1))
        d = ImageDraw.Draw(canvas)
        caps(d, (cx0 + 16, card_y0 + 13), eng, 9, (255, 255, 255, 160), 2.4, bold=True)
        tw = sum(d.textlength(c, font=font(FONT_B, 9)) + 2.4 for c in eng) - 2.4
        ar_text(d, (cx0 + 26 + tw, card_y0 + 11), "· " + arl, 11, DIM)
        icon_button(canvas, cx1 - 46, card_y0 + 19, 8, "dot")
        icon_button(canvas, cx1 - 24, card_y0 + 19, 8, "loop")
        over(canvas, lambda dd, cx0=cx0, cx1=cx1: dd.line(
            [(cx0 + 14, card_y0 + 36), (cx1 - 14, card_y0 + 36)], fill=(255, 255, 255, 18)))

    # --- card 1: learn / stop
    canvas = learn_button(canvas, xs[0] + 62, 512, 30, 0.82, frozen=True)
    canvas = lcd(canvas, (xs[0] + 118, 468, xs[0] + 251, 516), "24.9", "ث · مسموع", warm=True, big=26)
    d = ImageDraw.Draw(canvas)
    ar_text(d, (xs[0] + 62, card_y0 + 96), "إيقاف", 10, DIM, anchor="ma")
    ar_text(d, (xs[0] + 118, card_y0 + 74), "اللي اتسمع قبل الإيقاف", 9, FAINT)

    # --- card 2: curve strength
    canvas = knob(canvas, xs[1] + 62, 512, 38, 0.62, accent=True)
    canvas = lcd(canvas, (xs[1] + 118, 468, xs[1] + 251, 516), "4", "باند · 3 حركات", warm=True, big=28)
    d = ImageDraw.Draw(canvas)
    ar_text(d, (xs[1] + 62, card_y0 + 106), "القوة", 10, DIM, anchor="ma")
    ar_text(d, (xs[1] + 118, card_y0 + 74), "٦٢٪ من التصحيح", 9, FAINT)

    # --- card 3: the moving band (our own section, in his grammar)
    canvas = knob(canvas, xs[2] + 62, 512, 38, 0.44, accent=True)
    canvas = lcd(canvas, (xs[2] + 118, 468, xs[2] + 251, 516), "600", "هرتز · تابع", warm=False, big=28)
    d = ImageDraw.Draw(canvas)
    ar_text(d, (xs[2] + 62, card_y0 + 106), "التثبيت", 10, DIM, anchor="ma")
    ar_text(d, (xs[2] + 118, card_y0 + 74), "بيمشي مع النغمة", 9, FAINT)

    # --- card 4: output
    canvas = knob(canvas, xs[3] + 62, 512, 38, 0.5, accent=False)
    canvas = lcd(canvas, (xs[3] + 118, 468, xs[3] + 251, 516), "0.0", "dB · خروج", warm=True, big=28)
    d = ImageDraw.Draw(canvas)
    ar_text(d, (xs[3] + 62, card_y0 + 106), "المستوى", 10, DIM, anchor="ma")
    over(canvas, lambda dd: dd.rounded_rectangle([xs[3] + 118, 526, xs[3] + 251, 552], radius=8,
                                                 fill=(255, 255, 255, 10)))
    over(canvas, lambda dd: dd.rounded_rectangle([xs[3] + 118, 526, xs[3] + 251, 552], radius=8,
                                                 outline=(255, 255, 255, 34)))
    d = ImageDraw.Draw(canvas)
    caps(d, (xs[3] + 184, 533), "BYPASS", 8, (255, 255, 255, 130), 1.6, anchor="ma", bold=True)

    # ---------------------------------------------------------------- bottom strip
    strip = (32, 624, 1148, 676)
    over(canvas, lambda dd: dd.rounded_rectangle(strip, radius=10, fill=(0, 0, 0, 90)))
    over(canvas, lambda dd: dd.rounded_rectangle(strip, radius=10, outline=(255, 255, 255, 22)))
    d = ImageDraw.Draw(canvas)
    # HP slider on the left, thin, like his
    sx0, sx1, sy = 60, 300, 637
    over(canvas, lambda dd: (dd.rounded_rectangle([sx0, sy - 2, sx1, sy + 2], radius=2,
                                                  fill=(255, 255, 255, 26)),
                             dd.rounded_rectangle([sx0, sy - 2, fx(88) - 0, sy + 2], radius=2,
                                                  fill=AMBER + (190,))))
    hx = sx0 + (sx1 - sx0) * 0.22
    over(canvas, lambda dd: dd.line([(sx0, sy - 2), (hx, sy - 2)], fill=AMBER + (230,), width=4))
    d = ImageDraw.Draw(canvas)
    d.polygon([(hx, sy - 7), (hx + 5, sy), (hx, sy + 7), (hx - 5, sy)], fill=AMBER_HOT)
    caps(d, (sx0, 616), "HP", 8, (255, 255, 255, 120), 1.8, bold=True)
    caps(d, (sx1 - 4, 616), "88 Hz", 8, AMBER_HOT, 1.2, anchor="ra", bold=True)

    # the report line, in Arabic, at the centre of the strip
    ar_text(d, (470, 636), "اتظبط لوحده بعد ٢٤.٩ ثانية · ٤ باندات · واحد شغّال ديناميك", 12, TEXT, anchor="ma")

    # right: version + LED row, the way his footer ends
    caps(d, (1132, 630), "V1.3.0", 9, FAINT, 1.8, anchor="ra")
    for i in range(12):
        on = i < 9
        over(canvas, lambda dd, i=i, on=on: dd.rounded_rectangle(
            [1000 + i * 8, 650, 1005 + i * 8, 655], radius=2,
            fill=AMBER + (210,) if on else (255, 255, 255, 28)))

    # ---------------------------------------------------------------- edge meters
    for mx in (20, W - 20):
        over(canvas, lambda dd: dd.rounded_rectangle([mx - 1, 84, mx + 1, 598], radius=1,
                                                     fill=(255, 255, 255, 18)))
        over(canvas, lambda dd: dd.rounded_rectangle([mx - 1, 300, mx + 1, 470], radius=1,
                                                     fill=AMBER + (120,)))

    canvas.putalpha(mask)
    canvas.save(OUT)
    print(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(build())
