#!/usr/bin/env python3
"""assets_prep.py - turn the rendered materials into the sprites the panel draws.

The materials themselves are not drawn in code.  A knob that looks machined needs real
specular behaviour: a knurled ring that catches light, an anodised bevel, a brushed face
with anisotropic streaks.  Vector drawing cannot produce that, and the first three panel
attempts are the evidence - they were flat because every pixel was a shape.

So the parts are rendered once, as photographs of nothing in particular:

    design/assets/src/knob_plain.png    a large machined knob, plain face
    design/assets/src/button_unlit.png  a smoked-glass button, off
    design/assets/src/button_round.png  the same button, lit amber (from under the glass)
    design/assets/src/plate_carbon.png  the brushed panel material

and this script cuts them into what the panel actually composites:

    knob_body.png / knob_body_sm.png    the knob with its face present and *no pointer*
    knob_body_warm.png                  the same face, tinted warm for the learn section
    button_off.png / button_on.png      the two states
    panel_plate.png                     the plate, graded and vignetted to the window

Nothing here bakes a value into a picture: the knob ships with a plain face and the
pointer is drawn in code at the value the DSP reports.  A baked pointer would need 270
renders to be honest, and the one that *was* baked (src/knob_dark.png) has a fixed angle
that is wrong for every value but one.

    /tmp/venv/bin/python design/assets_prep.py
"""
from __future__ import annotations

import math
import pathlib
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / "assets" / "src"
OUT = HERE / "assets"


def cut(img: Image.Image, thresh: int = 14, feather: float = 1.6) -> Image.Image:
    """A render on black becomes RGBA with a soft edge (the glow around a lit part)."""
    g = img.convert("L")
    a = g.point(lambda v: 0 if v < thresh else min(255, int((v - thresh) * 255 / 55)))
    a = a.filter(ImageFilter.GaussianBlur(feather))
    out = img.convert("RGBA")
    out.putalpha(a)
    return out


def tight(im: Image.Image) -> Image.Image:
    bb = im.getchannel("A").point(lambda v: 255 if v > 10 else 0).getbbox()
    return im.crop(bb)


def load(name: str) -> Image.Image:
    return Image.open(SRC / name).convert("RGB")


def save(im: Image.Image, name: str) -> None:
    im.save(OUT / name, optimize=True)


def main() -> int:
    # ---- knob: one body, two sizes, no baked pointer -------------------------------
    knob = tight(cut(load("knob_plain.png")))
    save(knob.resize((320, 320), Image.LANCZOS), "knob_body.png")
    save(knob.resize((196, 196), Image.LANCZOS), "knob_body_sm.png")

    # a warm variant for the "learn" section, tinted rather than re-rendered
    warm = knob.resize((320, 320), Image.LANCZOS).convert("RGB")
    r, g, b = warm.split()
    warm = Image.merge("RGB", (r.point(lambda v: min(255, int(v * 1.06))),
                               g.point(lambda v: min(255, int(v * 0.99))),
                               b.point(lambda v: min(255, int(v * 0.90)))))
    warm = ImageEnhance.Contrast(warm).enhance(1.04)
    warm.putalpha(knob.resize((320, 320), Image.LANCZOS).getchannel("A"))
    save(warm, "knob_body_warm.png")

    # ---- buttons: the two real states ---------------------------------------------
    on = tight(cut(load("button_round.png")))
    off = tight(cut(load("button_unlit.png")))
    for im, name in ((on, "button_on"), (off, "button_off")):
        save(im.resize((132, 132), Image.LANCZOS), f"{name}.png")
        save(im.resize((72, 72), Image.LANCZOS), f"{name}_sm.png")

    # ---- the plate: the panel is this material, graded for the window --------------
    plate = load("plate_carbon.png").convert("RGB")
    w, h = 1180, 700
    tile = plate.resize((w, h), Image.LANCZOS)
    tile = ImageEnhance.Brightness(tile).enhance(0.92)
    # a soft top-down gradient so the plate reads as lit from above
    grad = Image.new("L", (1, h))
    gp = grad.load()
    for y in range(h):
        gp[0, y] = int(255 - 70 * (y / h))
    grad = grad.resize((w, h), Image.BILINEAR)
    tile = Image.composite(ImageEnhance.Brightness(tile).enhance(1.06),
                           ImageEnhance.Brightness(tile).enhance(0.62), grad)
    vign = Image.new("RGB", (w, h), (0, 0, 0))
    vmask = Image.new("L", (w, h), 0)
    vd = ImageDraw.Draw(vmask)
    vd.ellipse([-w * 0.25, -h * 0.35, w * 1.25, h * 1.35], fill=255)
    vmask = vmask.filter(ImageFilter.GaussianBlur(120))
    tile = Image.composite(tile, vign, vmask)
    save(tile, "panel_plate.png")

    missing = [p.name for p in [OUT / n for n in
               ("knob_body.png", "knob_body_sm.png", "knob_body_warm.png",
                "button_on.png", "button_off.png", "panel_plate.png")]
               if not p.exists()]
    if missing:
        print("missing sprites: " + ", ".join(missing), file=sys.stderr)
        return 1
    print("sprites written: 7 files from 4 materials")
    return 0


if __name__ == "__main__":
    sys.exit(main())
