#!/usr/bin/env python3
"""
ui_preview.py - render the plugin window as a PNG, without building the plugin.

This is a *design preview*, not a screenshot: it draws the same layout, palette and
geometry as plugin/Source/PluginEditor.cpp so the design can be reviewed on any
machine instead of only Windows + a DAW.

What is real: the standard curve and the +-1 sigma tolerance corridor are read out
of plugin/Source/model/EnvTargetCurve.h - the table the plugin compiles in.  The
"your take" line and the problem cards are illustrative and the caption says so; a
mock that invented measurements would be a lie dressed as data.

Needs: pillow, arabic-reshaper, python-bidi (Arabic is shaped and reordered here
because Pillow does not do it; JUCE does it itself through HarfBuzz).

Usage:  python3 analysis/scripts/ui_preview.py
Output: docs/ui_preview.png
"""
import math
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "plugin", "Source", "model", "EnvTargetCurve.h")
OUT = os.path.join(ROOT, "docs", "ui_preview.png")

W, H = 1240, 760
PAD = 18
FMIN, FMAX = 40.0, 16000.0
DBMAX, DBMIN = 18.0, -30.0

# palette, copied from plugin/Source/ui/Theme.h
BG0, BG1 = (0x10, 0x14, 0x1b), (0x0d, 0x10, 0x15)
PANEL, PANELHI, LINE = (0x15, 0x1a, 0x22), (0x1b, 0x21, 0x2b), (0x27, 0x2f, 0x3c)
TEXT, MUTED = (0xe8, 0xee, 0xf7), (0x8b, 0x97, 0xa9)
ACCENT, ACCENT2 = (0x38, 0xd8, 0xb8), (0x4a, 0x9d, 0xf8)
OK, WARN = (0x4e, 0xc9, 0x8a), (0xf2, 0xb0, 0x5c)
STANDARD = (0xe0, 0x5c, 0x8a)

FONT_DIR = "/usr/share/fonts/truetype/dejavu"


def read_table(name):
    src = open(HEADER, encoding="utf-8").read()
    m = re.search(r"static const std::array<float, kPoints>& " + name +
                  r"\(\) \{\s*static const std::array<float, kPoints> v \{(.*?)\};",
                  src, re.S)
    if not m:
        raise SystemExit(f"cannot find {name}() in {HEADER}")
    return [float(x.strip().rstrip("f")) for x in m.group(1).replace("\n", " ").split(",")
            if x.strip()]


def interp(vals, f):
    x = math.log2(max(f, 1.0) / 100.0) * 12.0
    if x <= 0:
        return vals[0]
    if x >= len(vals) - 1:
        return vals[-1]
    i = int(x)
    t = x - i
    return vals[i] * (1 - t) + vals[i + 1] * t


def main():
    from PIL import Image, ImageDraw, ImageFont
    import arabic_reshaper
    from bidi.algorithm import get_display

    level = read_table("level")
    sd = read_table("sd")

    img = Image.new("RGB", (W, H), BG1)
    d = ImageDraw.Draw(img, "RGBA")

    # vertical wash
    for y in range(H):
        t = y / H
        c = tuple(int(BG0[i] * (1 - t) + BG1[i] * t) for i in range(3))
        d.line([(0, y), (W, y)], fill=c)

    def font(size, bold=False):
        name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
        return ImageFont.truetype(os.path.join(FONT_DIR, name), int(size))

    def ar(text):
        return get_display(arabic_reshaper.reshape(text))

    def text(xy, s, size, colour, bold=False, anchor="la", rtl=False):
        d.text(xy, ar(s) if rtl else s, font=font(size, bold), fill=colour, anchor=anchor)

    def card(box, fill, edge, radius=12, width=1):
        d.rounded_rectangle(box, radius=radius, fill=fill, outline=edge, width=width)

    def defined(f):
        v = interp(level, f)
        return 40.0 > v > -40.0 and 0.0 < interp(sd, f) < 40.0

    # ---------------------------------------------------------------- header
    card((PAD, 15, PAD + 34, 49), ACCENT, ACCENT, 9)
    text((PAD + 17, 24), "EQ", 14, BG1, True, "ma")
    text((PAD + 46, 16), "AEQ RESTORE", 15, TEXT, True)
    text((PAD + 46, 34), "أول EQ في السلسلة — المعيار: 15 ميكس لأمير محروس", 11, MUTED, rtl=True)

    cx = W - PAD - 86 - 10
    for label, colour, w in (("2 bands", ACCENT, 76), ("سمع 41 s", MUTED, 84), ("F0 264 Hz", ACCENT2, 92)):
        cx -= w
        d.rounded_rectangle((cx, 22, cx + w, 46), radius=12,
                            fill=colour + (40,), outline=colour + (140,), width=1)
        text((cx + w / 2, 34), label, 11, colour, True, "mm", rtl=("س" in label))
        cx -= 6
    card((W - PAD - 86, 20, W - PAD, 48), PANELHI, LINE, 7)
    text((W - PAD - 43, 34), "Bypass", 12, MUTED, anchor="mm")

    # ------------------------------------------------------------ curve panel
    px, py = PAD, 64 + 8
    pw, ph = W - PAD - 420 - 12 - PAD, H - 64 - 132 - 16 - 8
    card((px, py, px + pw, py + ph), PANEL, LINE, 14)
    plot = (px + 44, py + 38, px + pw - 44, py + ph - 22)

    def x_of(f):
        t = math.log(max(f, FMIN) / FMIN) / math.log(FMAX / FMIN)
        return plot[0] + t * (plot[2] - plot[0])

    def y_of(db):
        db = max(DBMIN - 1.0, min(DBMAX + 1.0, db))
        t = (db - DBMIN) / (DBMAX - DBMIN)
        return plot[3] - t * (plot[3] - plot[1])

    # legend
    lx = px + 16
    for colour, label in ((STANDARD, "المعيار"), (ACCENT2, "تسجيلك"), (ACCENT, "التصحيح")):
        d.rounded_rectangle((lx, py + 15, lx + 14, py + 18), radius=2, fill=colour)
        text((lx + 20, py + 10), label, 11, MUTED, rtl=True)
        lx += 96
    d.rounded_rectangle((px + pw - 266, py + 9, px + pw - 248, py + 21), radius=3,
                        fill=STANDARD + (36,), outline=STANDARD + (90,))
    text((px + pw - 242, py + 10), "مدى المهندس ±1σ", 11, MUTED, rtl=True)

    # grid
    for f in (50, 100, 200, 500, 1000, 2000, 5000, 10000):
        x = x_of(f)
        d.line([(x, plot[1]), (x, plot[3])], fill=LINE + (150,))
        text((x, plot[3] + 4), f"{f // 1000}k" if f >= 1000 else f"{f}", 10, MUTED, anchor="ma")
    for db in range(-30, 19, 6):
        y = y_of(db)
        d.line([(plot[0], y), (plot[2], y)], fill=LINE + ((230,) if db == 0 else (120,)))
        text((plot[0] - 6, y), f"{db:+d}", 10, MUTED, anchor="rm")

    # tolerance corridor (real data) + the standard itself
    up, lo = [], []
    for i in range(301):
        f = FMIN * (FMAX / FMIN) ** (i / 300.0)
        if f < 100 or not defined(f):
            continue
        mid, s = interp(level, f), max(0.2, min(8.0, interp(sd, f)))
        up.append((x_of(f), y_of(mid + s)))
        lo.append((x_of(f), y_of(mid - s)))
    for a, b in zip(up, lo):
        d.line([a, b], fill=STANDARD + (26,))
    if up:
        d.line(up, fill=STANDARD + (70,))
        d.line(lo, fill=STANDARD + (70,))
        d.line(up, fill=STANDARD + (150,), width=2)

    # "your take": illustrative (the caption says so)
    take, corr = [], []
    for i in range(301):
        f = FMIN * (FMAX / FMIN) ** (i / 300.0)
        if not defined(f):
            continue
        base = interp(level, f)
        bump = 3.2 * math.exp(-0.5 * (math.log2(f / 420.0) / 0.35) ** 2)
        dip = -2.2 * math.exp(-0.5 * (math.log2(f / 2600.0) / 0.45) ** 2)
        take.append((x_of(f), y_of(base + bump + dip)))
        g = -2.4 * math.exp(-0.5 * (math.log2(f / 420.0) / 0.5) ** 2) \
            + 1.1 * math.exp(-0.5 * (math.log2(f / 2600.0) / 0.6) ** 2)
        corr.append((x_of(f), y_of(g)))
    d.polygon(corr + [(x_of(FMAX), y_of(0)), (x_of(FMIN), y_of(0))], fill=ACCENT + (30,))
    d.line(corr, fill=ACCENT, width=2)
    d.line(take, fill=ACCENT2, width=2)

    # one open handle with its read-out
    hx, hy = x_of(420.0), y_of(-2.4)
    d.ellipse((hx - 9, hy - 9, hx + 9, hy + 9), fill=PANEL, outline=ACCENT, width=2)
    card((hx + 12, hy - 26, hx + 202, hy - 4), PANELHI, LINE, 6)
    text((hx + 107, hy - 22), "420 Hz   Q 1.1   −2.4 dB", 11, TEXT, anchor="ma")

    # ------------------------------------------------------- report column
    rx = px + pw + 12
    rw = W - PAD - rx
    card((rx, py, rx + rw, py + 86), OK + (26,), OK + (90,))
    text((rx + rw - 14, py + 14), "التسجيل داخل المعيار — مفيش تدخّل", 15, OK, True, "ra", rtl=True)
    text((rx + rw - 14, py + 40), "انحراف 3.00 dB RMS  ·  2 باند  ·  σ 1.42", 11, MUTED, anchor="ra", rtl=True)

    cards = [
        ("دليل، مش قرار: قريب من الحد", "انحراف 3.6 dB من أصل 3.8 مسموح",
         "مفيش حركة EQ — المقياس الأضيق بيسمح بقوله", WARN),
        ("صندوقية (Box)", "زيادة 1.4 dB في 520–850 Hz (على مظروف الصوت)",
         "خصم 1.1 dB عند 665 Hz (Q 1.1)", ACCENT2),
    ]
    cy = py + 96
    for title, detail, action, colour in cards:
        card((rx, cy, rx + rw, cy + 68), PANELHI, LINE, 10)
        d.rounded_rectangle((rx + rw - 5, cy + 8, rx + rw - 1, cy + 60), radius=2, fill=LINE)
        d.rounded_rectangle((rx + rw - 5, cy + 34, rx + rw - 1, cy + 60), radius=2, fill=colour)
        text((rx + rw - 16, cy + 9), title, 13, TEXT, True, "ra", rtl=True)
        text((rx + rw - 16, cy + 28), detail, 11, MUTED, anchor="ra", rtl=True)
        text((rx + rw - 16, cy + 46), action, 11, colour, True, "ra", rtl=True)
        cy += 74

    # ------------------------------------------------------------ transport
    ty = H - 140
    card((PAD, ty, W - PAD, ty + 112), PANEL + (200,), LINE, 14)

    for i, (label, on, w) in enumerate((("تحليل مستمر", True, 118), ("إعادة", False, 74))):
        bx = PAD + 16 + (0 if i == 0 else 126)
        card((bx, ty + 12, bx + w, ty + 38), PANELHI, ACCENT if on else LINE, 7)
        text((bx + w / 2, ty + 18), label, 12, ACCENT if on else MUTED, anchor="ma", rtl=True)
    card((PAD + 226, ty + 12, PAD + 466, ty + 38), PANELHI, LINE, 7)
    text((PAD + 238, ty + 18), "Restore — يصحّح المشاكل", 12, TEXT, rtl=True)

    knobs = [("المعيار", "100%", 0.95), ("القصّ تحت", "161 Hz", 0.35),
             ("De-ess", "2.9 dB", 0.30), ("دفء ↔ حضور", "0.0", 0.50),
             ("جسم ↔ هوا", "0.0", 0.50), ("Air", "0.0 dB", 0.50),
             ("تحليل أولاً", "30 s", 0.33)]
    n = len(knobs)
    kw = (W - 2 * PAD - 32) / n
    a0, a1 = math.radians(-125), math.radians(125)

    for i, (name, value, pos) in enumerate(knobs):
        ccx = PAD + 16 + kw * i + kw / 2
        ccy = ty + 66
        r = 26

        def pt(ang, rr):
            return ccx + rr * math.sin(ang), ccy - rr * math.cos(ang)

        d.arc((ccx - r, ccy - r, ccx + r, ccy + r),
              start=-90 + math.degrees(a0), end=-90 + math.degrees(a1),
              fill=LINE, width=4)
        va = a0 + (a1 - a0) * pos
        if abs(va) > 0.03:
            s_deg, e_deg = sorted((-90.0, -90 + math.degrees(va)))
            d.arc((ccx - r, ccy - r, ccx + r, ccy + r), start=s_deg, end=e_deg,
                  fill=ACCENT if pos > 0.5 else ACCENT2, width=4)
        d.ellipse((ccx - (r - 7), ccy - (r - 7), ccx + (r - 7), ccy + (r - 7)),
                  fill=PANELHI, outline=LINE)
        p1, p2 = pt(va, r - 14), pt(va, r - 4)
        d.line([p1, p2], fill=TEXT, width=2)
        text((ccx, ccy + 6), value, 10, TEXT, True, "mm")
        text((ccx, ty + 100), name, 11, MUTED, anchor="ma", rtl=True)

    text((W - PAD, H - 12),
         "معاينة تصميم · المعيار ومدى ±1σ من EnvTargetCurve.h (بيانات حقيقية) · "
         "«تسجيلك» والكروت توضيحية",
         9, MUTED, anchor="ra", rtl=True)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print(f"wrote {OUT} ({W}x{H})")


if __name__ == "__main__":
    main()
