#!/usr/bin/env python3
"""check_layout.py - the editor must lay its controls out at the size it opens at.

Written after 1.2.1 shipped with an empty dock: the constructor called setSize()
before buildControls(), resized() ran over a control list that did not exist yet,
and every knob, label and value kept the default 0x0 bounds.  The build was green,
the gates passed, the screenshot looked "empty but fine" at one size.

This is a static check of AeqAudioProcessorEditor, because the real layout can only
be exercised by a running editor (which needs a display).  It asserts the three
things that were wrong:

  1. buildControls() is called before the first setSize();
  2. resized() is called at least once after the last control is created;
  3. the default size comes from the display, not from a constant.

    python3 analysis/scripts/check_layout.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "plugin" / "Source" / "PluginEditor.cpp"


def main() -> int:
    if not EDITOR.is_file():
        print(f"missing {EDITOR}")
        return 2
    src = EDITOR.read_text(encoding="utf-8")
    ctor = src[src.find("AeqAudioProcessorEditor::AeqAudioProcessorEditor"):]
    ctor = ctor[:ctor.find("\n}\n") + 3]

    problems = []
    build = ctor.find("buildControls()")
    first_size = ctor.find("setSize (")
    if build < 0:
        problems.append("the constructor never calls buildControls()")
    if first_size < 0:
        problems.append("the constructor never sets a size")
    if build >= 0 and first_size >= 0 and build > first_size:
        problems.append("setSize() runs before buildControls(): resized() would lay out "
                        "an empty control list (this is what emptied the dock in 1.2.1)")

    after = ctor[first_size:] if first_size >= 0 else ""
    if "resized()" not in after:
        problems.append("no resized() after the initial setSize(): controls created later "
                        "stay at 0x0 until the host resizes the window")

    if "defaultWidth()" not in ctor or "getPrimaryDisplay" not in src:
        problems.append("the default window size does not consult the display, so it can "
                        "open taller than the screen the host gives it")

    for p in problems:
        print("  " + p)
    if problems:
        print(f"\n{len(problems)} layout problem(s)")
        return 1

    print("layout OK: controls are built, sized and laid out in that order, and the "
          "default size comes from the display")
    return 0


if __name__ == "__main__":
    sys.exit(main())
