#!/usr/bin/env python3
"""Every Arabic string is laid out right-to-left, whatever it happens to start with.

The base direction of a paragraph is decided by its first strong character, so a
card that reads

    202 Hz ثابت على مدى الأداء (متوسط +40.1 dB، تغيّر 15.0 dB)

is laid out left-to-right with Arabic runs inside it.  That is wrong for a design in
Arabic, and on Windows it is fatal: the DirectWrite layout of those runs took the
process down, and every card whose paragraph began with Arabic survived while every
one that began with a number died (probe-text-*.png on the runner).

The fix is a single place - `withRtlBase()` in ArabicText.cpp puts a RIGHT-TO-LEFT
MARK in front of any string whose first strong character is Latin or a digit - and
this script is the reason it cannot quietly go away:

  * the three text entry points (draw, drawParagraph, measure) must call it;
  * nothing outside that file may build its own TextLayout around Arabic text.

Exit code 0 with one line on success, 1 with every violation found.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "Source"
FUNNEL = SRC / "ui" / "ArabicText.cpp"

problems = []


def body_of(text, signature):
    """The text between the braces of the function whose header contains `signature`."""
    start = text.find(signature)
    if start < 0:
        return None
    brace = text.find("{", start)
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i]
    return None


def check_funnel():
    if not FUNNEL.exists():
        problems.append("plugin/Source/ui/ArabicText.cpp is gone - there is no text funnel")
        return
    text = FUNNEL.read_text(encoding="utf-8")

    if "withRtlBase" not in text:
        problems.append("ArabicText.cpp: withRtlBase() disappeared - mixed paragraphs are "
                        "laid out left-to-right again")
        return

    for name, signature in [
        ("draw", "void draw(juce::Graphics& g, const juce::String& utf8"),
        ("drawParagraph", "void drawParagraph(juce::Graphics& g, const juce::String& utf8"),
        ("measure", "float measure(const juce::String& utf8"),
    ]:
        body = body_of(text, signature)
        if body is None:
            problems.append(f"ArabicText.cpp: could not find {name}() - the script has drifted "
                            f"from the source, fix the script before trusting it")
            continue
        if "withRtlBase" not in body:
            problems.append(f"ArabicText.cpp: {name}() does not set the paragraph base "
                            f"direction (no withRtlBase call)")


def check_no_side_door():
    """A TextLayout built anywhere else is a paragraph the funnel never saw."""
    allowed = {FUNNEL.resolve(),
               (SRC / "ui" / "Theme.h").resolve()}   # caption(): Latin only, tracked out
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".h", ".cpp") or not path.is_file():
            continue
        if path.resolve() in allowed:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        # juce::TextLayout, not AudioProcessorValueTreeState::createLayout()
        for match in re.finditer(r"juce::TextLayout\b", text):
            line = text.count("\n", 0, match.start()) + 1
            problems.append(f"{path.relative_to(ROOT)}:{line}: builds its own TextLayout - "
                            f"text must go through aeq::ui::draw/drawParagraph")


def check_caption_is_latin_only():
    """caption() letter-spaces its text; tracking Arabic breaks the joins."""
    theme = SRC / "ui" / "Theme.h"
    if not theme.exists():
        return
    text = theme.read_text(encoding="utf-8")
    body = body_of(text, "inline void caption (juce::Graphics& g, const juce::String& text")
    if body is None:
        problems.append("Theme.h: caption() not found")
        return
    if "withExtraKerningFactor" in body and "u8 (" in body:
        problems.append("Theme.h: caption() both tracks letters and takes Arabic - tracking "
                        "breaks Arabic joins")


check_funnel()
check_no_side_door()
check_caption_is_latin_only()

if problems:
    print("bidi funnel violations:")
    for p in problems:
        print("  - " + p)
    sys.exit(1)

files = [p for p in SRC.rglob("*") if p.suffix in (".h", ".cpp")]
print(f"{len(files)} files, every paragraph goes through the RTL-base funnel")
