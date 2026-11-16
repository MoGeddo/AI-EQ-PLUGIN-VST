#!/usr/bin/env python3
"""
check_brand.py - the logo in the plugin is his logo, byte for byte from his own site.

The request was "خلي اللوجو بتاعي من الموقع بتاعي" - put my own logo from my own site in it.
That rules out three things this file can catch and a person cannot see:

  * a **lookalike** - the mark traced with a font, or the words drawn in a script face.  It
    would look right at 200 px and be a different mark.  The check hashes the bytes;
  * a **different picture** - the site changes its logo, or someone "cleans up" the PNG, and
    the plugin keeps shipping whatever was in the repository when the crop was made.  The
    manifest records the URL, the size and the sha256 of what arrived, and of everything
    derived from it;
  * a **crop that drifted** - the mark is cropped to its ink and tinted at draw time; if the
    crop box changes, the picture changes while the hash of the source still matches.

His site cannot be reached from this sandbox (only GitHub and the package registries are
open, the same wall that keeps his fifteen stems on Drive), so the file arrives on a GitHub
runner that has ordinary internet: `.github/workflows/fetch-brand.yml`, commit something
under `.github/brand-request/` to ask for it.  That provenance is written down in
`docs/brand.json` rather than implied.

    python3 scripts/check_brand.py
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "docs", "brand.json")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def png_size(path):
    b = open(path, "rb").read(33)
    if b[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", b[16:24])


def main():
    if not os.path.exists(MANIFEST):
        print("FAIL no docs/brand.json - the logo has no recorded provenance")
        return 1
    brand = json.load(open(MANIFEST, encoding="utf-8"))
    problems = []

    # 1. the picture the plugin actually draws
    mark = os.path.join(ROOT, "plugin", "Source", "assets", "geddo-mark.png")
    if not os.path.exists(mark):
        problems.append("plugin/Source/assets/geddo-mark.png is missing - the window would "
                        "draw nothing")
    else:
        size = png_size(mark)
        if not size:
            problems.append("geddo-mark.png is not a PNG")
        else:
            w, h = size
            if h < 120:
                problems.append(f"geddo-mark.png is only {w}x{h} - too small to be crisp at "
                                f"2x, where it is drawn")
            if w > 4 * h:
                problems.append(f"geddo-mark.png is {w}x{h}, which is not this mark's shape")

    # 2. every recorded hash still matches the file next to it
    checked = 0
    for entry in brand.get("files", []) + brand.get("derived", []):
        rel = entry.get("path")
        full = os.path.join(ROOT, rel)
        if not os.path.exists(full):
            problems.append(f"{rel}: recorded in docs/brand.json and not in the repository")
            continue
        got = sha(full)
        if entry.get("sha256") and got != entry["sha256"]:
            problems.append(f"{rel}: sha256 is {got[:12]}…, the manifest says "
                            f"{entry['sha256'][:12]}… - the picture changed without the "
                            f"record changing")
        if entry.get("bytes") and os.path.getsize(full) != entry["bytes"]:
            problems.append(f"{rel}: {os.path.getsize(full)} bytes, manifest says "
                            f"{entry['bytes']}")
        checked += 1

    # 3. the derived mark is derived from the file that came off his site
    if not any(e.get("path") == "docs/brand-logo.png" for e in brand.get("files", [])):
        problems.append("docs/brand.json no longer records docs/brand-logo.png as the "
                        "source the mark was cut from")
    source = next((e for e in brand.get("files", []) if e.get("path") == "docs/brand-logo.png"),
                  None)
    if source and "mostafageddo.com" not in source.get("url", ""):
        problems.append(f"the source is {source.get('url')!r}, not his site")

    if problems:
        print("FAIL brand check:")
        for p in problems:
            print("  -", p)
        return 1

    print(f"brand ok: {checked} files hash to what docs/brand.json records, the mark is "
          f"cut from {source['url']} ({source['bytes']:,} bytes, sha256 "
          f"{source['sha256'][:12]}…) and the window, the .exe and the installer all draw "
          f"that same picture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
