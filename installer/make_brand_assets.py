#!/usr/bin/env python3
"""
make_brand_assets.py - his logo, cut into the shapes Windows asks for.

The picture is his own site's (`mostafageddo.com/images/logo.png`, parked by
`.github/workflows/fetch-brand.yml` because this sandbox cannot open his domain).  It is
white ink on transparent ground.  Three things need different shapes of it:

  plugin/Source/assets/geddo.ico   window + .exe + installer icon: the mark in white on the
                                   plugin's own plate, in a rounded square, at 16..256 px.
                                   A bare white mark is invisible on a light taskbar and a
                                   bare black one on a dark one, so it carries its plate.
  installer/wizard.png             the Inno sidebar page image: plate, mark, one rule.
  installer/wizard_small.png       the 55x55 corner image: the mark alone.

`docs/brand.json` keeps the hashes; G93 re-derives them and fails if the picture used here
is not the picture the site served.

    .venv/bin/python installer/make_brand_assets.py
"""
from __future__ import annotations

import hashlib
import json
import os
import struct

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "brand-logo.png")
MARK = os.path.join(ROOT, "plugin", "Source", "assets", "geddo-mark.png")

PLATE_TOP = (0x24, 0x17, 0x33)      # the plugin's plate, top
PLATE_BOT = (0x14, 0x0E, 0x1E)      # ... and bottom
INK = (0xFC, 0xF6, 0xFE)            # Palette::text()


def load_mark():
    im = Image.open(SRC).convert("RGBA")
    a = im.getchannel("A").point(lambda v: 255 if v > 16 else 0)
    return im.crop(a.getbbox())


def plate(size, radius_frac=0.22, pad_frac=0.16, tint=INK):
    """A rounded plate with the mark on it, `size` px square."""
    ss = 4                                   # supersample, then box down
    S = size * ss
    ground = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(ground)
    grad = Image.new("RGBA", (1, S))
    for y in range(S):
        t = y / max(S - 1, 1)
        grad.putpixel((0, y), tuple(int(PLATE_TOP[i] + (PLATE_BOT[i] - PLATE_TOP[i]) * t)
                                    for i in range(3)) + (255,))
    grad = grad.resize((S, S))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * radius_frac),
                                           fill=255)
    ground.paste(grad, (0, 0), mask)

    m = load_mark()
    inner = int(S * (1 - 2 * pad_frac))
    mw = inner
    mh = max(1, round(m.height * mw / m.width))
    if mh > inner:
        mh = inner
        mw = max(1, round(m.width * mh / m.height))
    m = m.resize((mw, mh), Image.LANCZOS)
    tinted = Image.new("RGBA", m.size, tint + (0,))
    tinted.putalpha(m.getchannel("A"))
    ground.alpha_composite(tinted, ((S - mw) // 2, (S - mh) // 2))
    return ground.resize((size, size), Image.LANCZOS)


def write_ico(path, images):
    """A multi-size .ico with PNG entries - what Windows 10/11 reads."""
    blobs = []
    for im in images:
        tmp = path + f".{im.width}.png"
        im.save(tmp, format="PNG")
        blobs.append((im.width, open(tmp, "rb").read()))
        os.remove(tmp)
    header = struct.pack("<HHH", 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries, data = b"", b""
    for size, blob in blobs:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
        data += blob
    open(path, "wb").write(header + entries + data)


FONT_DIR = os.path.join(ROOT, "plugin", "Source", "assets")
NAME = "GEDDO AEQ"


def load_font(size):
    """Cairo, the same face the plugin draws its own words in (SIL OFL, shipped with it)."""
    from PIL import ImageFont
    for f in ("Cairo-SemiBold.ttf", "Cairo-Regular.ttf"):
        p = os.path.join(FONT_DIR, f)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def tracked(draw, xy, text, font, fill, tracking, anchor_center_x=None):
    """Draw text with extra letter spacing - PIL has none, and the product's name is tracked."""
    widths = [draw.textlength(ch, font=font) for ch in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x = xy[0] if anchor_center_x is None else anchor_center_x - total / 2
    for ch, w in zip(text, widths):
        draw.text((x, xy[1]), ch, font=font, fill=fill)
        x += w + tracking
    return total


def wizard_page(w=164, h=314):
    ss = 2
    W, H = w * ss, h * ss
    page = Image.new("RGBA", (W, H))
    for y in range(H):
        t = y / (H - 1)
        page.paste(tuple(int(PLATE_TOP[i] + (PLATE_BOT[i] - PLATE_TOP[i]) * t)
                         for i in range(3)) + (255,), (0, y, W, y + 1))
    m = load_mark()
    mw = int(W * 0.74)
    mh = max(1, round(m.height * mw / m.width))
    m = m.resize((mw, mh), Image.LANCZOS)
    tinted = Image.new("RGBA", m.size, INK + (0,))
    tinted.putalpha(m.getchannel("A"))
    page.alpha_composite(tinted, ((W - mw) // 2, int(H * 0.30)))
    d = ImageDraw.Draw(page)
    y = int(H * 0.30) + mh + int(H * 0.045)
    # the name under his mark: the mark is a picture and cannot be searched for, and the
    # installer is where somebody decides what they are installing
    font = load_font(int(W * 0.075))
    tracked(d, (0, y), NAME, font, INK + (255,), tracking=int(W * 0.012),
            anchor_center_x=W / 2)
    y += int(W * 0.13)
    d.line([(int(W * 0.12), y), (int(W * 0.88), y)], fill=(0xFF, 0xBA, 0xDC, 200),
           width=max(1, ss))
    return page.resize((w, h), Image.LANCZOS)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    mark_out = os.path.join(ROOT, "plugin", "Source", "assets", "geddo-mark.png")
    m = load_mark()
    mh = 200
    mw = max(1, round(m.width * mh / m.height))
    m = m.resize((mw, mh), Image.LANCZOS)
    m.save(mark_out, optimize=True)

    ico = os.path.join(ROOT, "plugin", "Source", "assets", "geddo.ico")
    write_ico(ico, [plate(s) for s in (16, 24, 32, 48, 64, 128, 256)])
    # JUCE's build takes PNGs for the window/.exe icon (juceaide winicon) - the .ico above is
    # for the installer, which wants a real multi-size icon file.
    for s in (256, 32):
        plate(s).save(os.path.join(ROOT, "plugin", "Source", "assets",
                                   f"geddo-icon-{s}.png"), optimize=True)

    page = wizard_page()
    page.save(os.path.join(ROOT, "installer", "wizard.png"), optimize=True)
    small = plate(55, radius_frac=0.30, pad_frac=0.12)
    small.save(os.path.join(ROOT, "installer", "wizard_small.png"), optimize=True)
    # the Inno script reads the .bmp copies on Windows
    page.convert("RGB").save(os.path.join(ROOT, "installer", "wizard.bmp"))
    small.convert("RGB").save(os.path.join(ROOT, "installer", "wizard_small.bmp"))
    plate(256).save(os.path.join(ROOT, "installer", "preview.png"), optimize=True)
    # the installer's own icon + every shortcut it makes
    write_ico(os.path.join(ROOT, "installer", "aeq.ico"),
              [plate(s) for s in (16, 24, 32, 48, 64, 128, 256)])

    brand = json.load(open(os.path.join(ROOT, "docs", "brand.json")))
    derived = {d["path"]: d for d in brand.get("derived", [])}
    for rel in ("plugin/Source/assets/geddo-mark.png", "plugin/Source/assets/geddo.ico",
                "plugin/Source/assets/geddo-icon-256.png",
                "plugin/Source/assets/geddo-icon-32.png",
                "installer/wizard.png", "installer/wizard_small.png",
                "installer/wizard.bmp", "installer/wizard_small.bmp",
                "installer/preview.png", "installer/aeq.ico"):
        derived[rel] = {"path": rel, "bytes": os.path.getsize(os.path.join(ROOT, rel)),
                        "sha256": sha(os.path.join(ROOT, rel)),
                        "from": "docs/brand-logo.png"}
    brand["derived"] = [derived[k] for k in sorted(derived)]
    brand["derived_note"] = ("regenerate with installer/make_brand_assets.py from "
                             "docs/brand-logo.png; G93 compares these hashes with the files")
    json.dump(brand, open(os.path.join(ROOT, "docs", "brand.json"), "w"), indent=1)

    for rel in sorted(derived):
        p = os.path.join(ROOT, rel)
        print(f"  {rel:<42} {os.path.getsize(p):>8,} bytes")


if __name__ == "__main__":
    main()
