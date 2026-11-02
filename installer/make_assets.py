#!/usr/bin/env python3
"""
make_assets.py - the installer's visual assets, generated from the project's own
data instead of hand-drawn art.

  installer/aeq.ico            multi-size icon (16..256, 32-bit BMP entries)
  installer/wizard.png         164x314 page image for the Inno wizard
  installer/wizard_small.png   55x55 image for the wizard's corner
  installer/preview.png        256x256 preview so a human can look at it

The graphic is the actual consensus target curve from presets/target_curve.csv,
so the icon changes when the standard changes.  Pure stdlib: no Pillow, no
dependency to install on a build machine.
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "installer")

BG_TOP = (0x12, 0x17, 0x1F)
BG_BOT = (0x08, 0x0A, 0x0E)
GRID = (0x27, 0x30, 0x3D)
CURVE = (0xFF, 0xB4, 0x54)          # amber, the same accent the plugin's UI uses
CURVE_DIM = (0xFF, 0x8A, 0x2B)
BORDER = (0x2E, 0x38, 0x47)


# ---------------------------------------------------------------- target curve
def load_curve():
    path = os.path.join(ROOT, "presets", "target_curve.csv")
    pts = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.strip().split(",")
            if parts[0].lower().startswith("freq"):
                continue
            pts.append((float(parts[0]), float(parts[2])))
    return pts


def db_at(curve, f):
    """log-linear interpolation, clamped at both ends"""
    if f <= curve[0][0]:
        return curve[0][1]
    if f >= curve[-1][0]:
        return curve[-1][1]
    import math
    lf = math.log(f)
    for i in range(1, len(curve)):
        if curve[i][0] >= f:
            f0, d0 = curve[i - 1]
            f1, d1 = curve[i]
            t = (lf - math.log(f0)) / (math.log(f1) - math.log(f0))
            return d0 + t * (d1 - d0)
    return curve[-1][1]


# --------------------------------------------------------------------- drawing
class Canvas:
    """float RGB, painted in linear order, exported to 8-bit"""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[[0.0, 0.0, 0.0] for _ in range(w)] for _ in range(h)]

    def set(self, x, y, rgb, a=1.0):
        if 0 <= x < self.w and 0 <= y < self.h and a > 0.0:
            p = self.px[y][x]
            p[0] += (rgb[0] - p[0]) * a
            p[1] += (rgb[1] - p[1]) * a
            p[2] += (rgb[2] - p[2]) * a

    def vgrad(self, top, bot):
        for y in range(self.h):
            t = y / max(self.h - 1, 1)
            r = top[0] + (bot[0] - top[0]) * t
            g = top[1] + (bot[1] - top[1]) * t
            b = top[2] + (bot[2] - top[2]) * t
            for x in range(self.w):
                self.px[y][x] = [r, g, b]

    def rect(self, x0, y0, x1, y1, rgb, a=1.0):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.set(x, y, rgb, a)

    def bytes_rgb(self):
        out = bytearray()
        for row in self.px:
            for p in row:
                out += bytes((
                    max(0, min(255, int(round(p[0])))),
                    max(0, min(255, int(round(p[1])))),
                    max(0, min(255, int(round(p[2]))))))
        return bytes(out)


def draw_graphic(size, curve, ss=3):
    """the shared graphic: dark panel, faint grid, the target curve, a soft glow"""
    import math
    W = size * ss
    c = Canvas(W, W)
    c.vgrad(BG_TOP, BG_BOT)

    pad = max(1, int(W * 0.11))
    x0, x1 = pad, W - pad
    y0, y1 = pad, W - pad
    db_hi, db_lo = 14.0, -40.0

    def fx(f):
        return x0 + (math.log(f / 100.0) / math.log(160.0)) * (x1 - x0)

    def fy(db):
        t = (db_hi - db) / (db_hi - db_lo)
        return y0 + t * (y1 - y0)

    # faint grid: 100 Hz / 1 kHz / 10 kHz uprights and 0 dB / -20 dB flats
    gw = max(1, ss)
    for f in (100.0, 1000.0, 10000.0):
        x = int(fx(f))
        for y in range(y0, y1):
            c.set(x, y, GRID, 0.55)
            if gw > 1:
                c.set(x + 1, y, GRID, 0.25)
    for db in (0.0, -20.0):
        y = int(fy(db))
        for x in range(x0, x1):
            c.set(x, y, GRID, 0.55)

    # the curve: fill below, then the stroke, then a wider translucent halo
    thick = max(1.6, W * 0.026)
    for (rgb, alpha, half) in ((CURVE_DIM, 0.06, thick * 2.2), (CURVE, 0.16, thick * 1.3), (CURVE, 1.0, thick * 0.5)):
        for x in range(x0, x1):
            fmin, fmax = 100.0, 16000.0
            f = fmin * (fmax / fmin) ** ((x - x0) / max(x1 - x0 - 1, 1))
            db = db_at(curve, f)
            yc = fy(db)
            lo = int(round(yc - half))
            hi = int(round(yc + half))
            for y in range(max(y0, lo), min(y1, hi + 1)):
                d = abs((y + 0.5) - yc)
                a = alpha * max(0.0, min(1.0, half - d + 0.5))
                c.set(x, y, rgb, a)

    # hairline frame so the icon does not melt into a dark taskbar
    for i in range(max(1, ss)):
        for x in range(W):
            c.set(x, i, BORDER, 0.9)
            c.set(x, W - 1 - i, BORDER, 0.9)
        for y in range(W):
            c.set(i, y, BORDER, 0.9)
            c.set(W - 1 - i, y, BORDER, 0.9)
    return c


def resize(src, size):
    """box resample to exactly size x size (source may be any resolution)"""
    out = Canvas(size, size)
    sx = src.w / size
    sy = src.h / size
    for y in range(size):
        y0, y1 = int(y * sy), max(int(y * sy) + 1, int((y + 1) * sy))
        for x in range(size):
            x0, x1 = int(x * sx), max(int(x * sx) + 1, int((x + 1) * sx))
            r = g = b = 0.0
            n = 0
            for yy in range(min(y0, src.h - 1), min(y1, src.h)):
                for xx in range(min(x0, src.w - 1), min(x1, src.w)):
                    p = src.px[yy][xx]
                    r += p[0]
                    g += p[1]
                    b += p[2]
                    n += 1
            n = max(n, 1)
            out.px[y][x] = [r / n, g / n, b / n]
    return out


# --------------------------------------------------------------------- encoders
def write_png(path, canvas):
    w, h = canvas.w, canvas.h
    raw = bytearray()
    for y in range(h):
        raw.append(0)                                   # filter: none
        for x in range(w):
            p = canvas.px[y][x]
            raw += bytes((max(0, min(255, int(round(p[0])))),
                          max(0, min(255, int(round(p[1])))),
                          max(0, min(255, int(round(p[2]))))))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def ico_entry(canvas):
    """32-bit BMP inside an ICO - accepted by every Windows version, unlike
    PNG-in-ICO which older shell code paths refuse."""
    w, h = canvas.w, canvas.h
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    pixels = bytearray()
    for y in range(h - 1, -1, -1):                      # bottom-up
        for x in range(w):
            p = canvas.px[y][x]
            pixels += bytes((max(0, min(255, int(round(p[2])))),
                             max(0, min(255, int(round(p[1])))),
                             max(0, min(255, int(round(p[0])))), 255))
    mask_row = ((w + 31) // 32) * 4                     # 1 bpp, DWORD aligned
    mask = bytes(mask_row * h)                          # all opaque: alpha wins
    return header + bytes(pixels) + mask


def write_ico(path, canvases):
    images = [ico_entry(c) for c in canvases]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    dirs = b""
    for c, img in zip(canvases, images):
        dim = 0 if c.w >= 256 else c.w
        dirs += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    open(path, "wb").write(out + dirs + b"".join(images))


def write_bmp(path, canvas):
    """24-bit BMP.  Inno Setup takes BMP on every version; PNG support depends on
    the build, and a failed image must not be the reason an installer will not
    compile."""
    w, h = canvas.w, canvas.h
    row_pad = (-w * 3) % 4
    pixels = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            p = canvas.px[y][x]
            pixels += bytes((max(0, min(255, int(round(p[2])))),
                             max(0, min(255, int(round(p[1])))),
                             max(0, min(255, int(round(p[0]))))))
        pixels += b"\x00" * row_pad
    file_size = 14 + 40 + len(pixels)
    header = (b"BM" + struct.pack("<IHHI", file_size, 0, 0, 54)
              + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0))
    open(path, "wb").write(header + bytes(pixels))


def write_wizard_png(path, curve, w=164, h=314):
    """vertical page image: the pane colour, the curve, a rule under it"""
    import math
    c = Canvas(w, h)
    c.vgrad(BG_TOP, BG_BOT)
    for x in range(w):
        c.set(x, 0, BORDER, 0.8)
        c.set(x, h - 1, BORDER, 0.8)
    for y in range(h):
        c.set(0, y, BORDER, 0.8)
        c.set(w - 1, y, BORDER, 0.8)

    band_top, band_bot = int(h * 0.42), int(h * 0.78)
    db_hi, db_lo = 14.0, -40.0
    for x in range(8, w - 8):
        fmin, fmax = 100.0, 16000.0
        f = fmin * (fmax / fmin) ** ((x - 8) / max(w - 17, 1))
        db = db_at(curve, f)
        yc = band_top + ((db_hi - db) / (db_hi - db_lo)) * (band_bot - band_top)
        for y in range(band_top, band_bot):
            if y > yc:
                c.set(x, y, CURVE_DIM, 0.10)
        for (rgb, alpha, half) in ((CURVE, 0.25, 2.4), (CURVE, 1.0, 0.9)):
            lo, hi = int(yc - half), int(yc + half)
            for y in range(max(band_top, lo), min(band_bot, hi + 1)):
                d = abs((y + 0.5) - yc)
                c.set(x, y, rgb, alpha * max(0.0, min(1.0, half - d + 0.5)))

    # a short amber rule near the top, reads as a brand mark at any size
    for y in range(int(h * 0.30), int(h * 0.30) + 3):
        for x in range(14, w - 14):
            c.set(x, y, CURVE, 0.9)
    return c


def main():
    os.makedirs(DEST, exist_ok=True)
    curve = load_curve()
    if len(curve) < 64:
        raise SystemExit("target_curve.csv looks wrong (%d rows)" % len(curve))

    big = resize(draw_graphic(256, curve, ss=3), 256)   # 3x supersampled, boxed
    sizes = [16, 24, 32, 48, 64, 128, 256]
    canvases = [resize(big, s) for s in sizes]
    write_ico(os.path.join(DEST, "aeq.ico"), canvases)
    write_png(os.path.join(DEST, "preview.png"), big)
    wiz = write_wizard_png(os.path.join(DEST, "wizard.png"), curve)
    write_bmp(os.path.join(DEST, "wizard.bmp"), wiz)
    small = resize(big, 55)                             # 55x55 corner image
    write_png(os.path.join(DEST, "wizard_small.png"), small)
    write_bmp(os.path.join(DEST, "wizard_small.bmp"), small)

    for f in ("aeq.ico", "preview.png", "wizard.bmp", "wizard_small.bmp"):
        p = os.path.join(DEST, f)
        print(f"  {f:<18} {os.path.getsize(p):>8,} bytes")


if __name__ == "__main__":
    main()
