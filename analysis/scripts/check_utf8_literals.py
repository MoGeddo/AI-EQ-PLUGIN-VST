#!/usr/bin/env python3
"""check_utf8_literals.py - no Arabic may reach juce::String by accident.

The 1.2.1 build rendered its Latin text and turned every Arabic letter into a
.box.  The cause was not the font and not the compiler: JUCE 8 builds
juce::String from a `const char*` with CharPointer_ASCII (juce_String.cpp:307), so
a UTF-8 literal that goes through that constructor becomes one character per
BYTE.  "تخنة" (4 characters, 8 bytes) arrived as 8 code points, none of them a
letter the font could draw.

The fix is one funnel, aeq::ui::u8(), and this script is what keeps it a funnel:

  * every non-ASCII string literal in plugin/Source must be wrapped in u8(...),
    drawn through ui::label/draw/drawParagraph/chip/caption (which take
    const char*), or converted with String::fromUTF8;
  * comments are ignored - they cannot reach the screen.

Exit 0 when clean, 1 when a literal is not converted, 2 when the source cannot be
read.  Run from the repository root:

    python3 analysis/scripts/check_utf8_literals.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "Source"

# functions that take UTF-8 bytes and convert them themselves (ArabicText.h)
SAFE_NAMES = ("label(", "draw(", "drawParagraph(", "chip(", "caption(", "measure(", "u8(")


def strip_comments(text: str) -> str:
    """Remove // and /* */ comments, keeping string literals intact."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\":
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def enclosed_by_safe_call(code: str, pos: int) -> bool:
    depth = 0
    i = pos - 1
    while i >= 0:
        ch = code[i]
        if ch == ")":
            depth += 1
        elif ch == "(":
            if depth == 0:
                # the name in front of this '(', skipping the space JUCE style
                # puts there: "u8 ("...")"
                j = i - 1
                while j >= 0 and code[j] in " \t\n\r":
                    j -= 1
                while j >= 0 and (code[j].isalnum() or code[j] in "_:"):
                    j -= 1
                name = code[j + 1:i].strip().split("::")[-1] + "("
                if name in SAFE_NAMES:
                    return True
                # keep walking out: an inner safe call inside an unsafe one still
                # converts before the outer one sees the bytes
                i -= 1
                continue
            depth -= 1
        elif ch in ";{}":
            return False
        i -= 1
    return False


def follows_a_converted_string(code: str, pos: int) -> bool:
    """True when this literal is a right-hand operand of a + chain whose left side
    is already a juce::String.

    juce::operator+ (String, const char*) appends with CharPointer_UTF8, so
    u8 ("خصم ") + juce::String (x) + " dB عند " is safe; the leading literal is the
    only one that goes through String (const char*) and its ASCII reader.
    """
    start = pos
    while start > 0 and code[start - 1] not in ";{}":
        start -= 1
    slice_ = code[start:pos]
    if not slice_.rstrip().endswith("+"):
        return False
    return ("u8 (" in slice_) or ("fromUTF8" in slice_) or ("String" in slice_)


def check_file(path: Path):
    try:
        raw = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        print(f"cannot read {path}: {exc}")
        raise SystemExit(2)

    code = strip_comments(raw)
    problems = []

    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', code):
        literal = m.group(1)
        if all(ord(ch) < 128 for ch in literal):
            continue

        # Which call encloses this literal?  Walk backwards over the expression and
        # find the parenthesis that is still open, then look at the name in front of
        # it.  (A long Arabic sentence is often split across several literals, and
        # only the first one sits next to the call.)
        if not (enclosed_by_safe_call(code, m.start()) or follows_a_converted_string(code, m.start())):
            line = code.count("\n", 0, m.start()) + 1
            problems.append((line, literal))

    return problems


def main():
    if not SRC.is_dir():
        print(f"no source tree at {SRC}")
        return 2

    files = sorted(list(SRC.glob("*.cpp")) + list(SRC.glob("*.h"))
                   + list(SRC.glob("ui/*.cpp")) + list(SRC.glob("ui/*.h")))
    total = 0
    bad = 0
    for f in files:
        problems = check_file(f)
        for line, literal in problems:
            print(f"  {f.relative_to(ROOT)}:{line}: not converted through ui::u8: "
                  f"{literal[:40]}")
            bad += 1
        total += 1

    print(f"\n{total} files, {bad} non-ASCII literal(s) bypassing the UTF-8 funnel")
    if bad:
        print("FAIL: a literal that is not converted will paint as .notdef boxes")
        return 1
    print("OK: every Arabic literal is converted by ui::u8 or String::fromUTF8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
