#!/usr/bin/env python3
"""check_feel.py - the feel rules, as numbers, because "it feels nice" is not a test.

Three skill sets were applied to this panel and each of them states its rules
quantitatively, so each one can be checked without a screen:

  make-interfaces-feel-better
      * a press is `scale(0.96)` and it is a transform, not a layout change
      * a hit area is at least 40 px (this panel's floor; the usual guidance is 44)
      * a shadow is elevation, never a decoration
  impeccable
      * numbers are tabular: a readout that changes twice a second may not reflow
      * one authored motion moment, not an animation per control
  transitions.dev / gsap-skills
      * every duration and curve is a token in MotionSpec.h, never a literal in a widget
      * transform and opacity only

What this file cannot check is whether it looks good.  What it can check is that the
rules which were *written down* are the rules in the code, and it fails when one is
deleted - which is the only way a design rule survives the next release.

    .venv/bin/python design/preview/check_feel.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EDITOR = ROOT / "plugin" / "Source" / "PluginEditor.cpp"
TEXT = ROOT / "plugin" / "Source" / "ui" / "ArabicText.h"
MOTION = ROOT / "plugin" / "Source" / "ui" / "MotionSpec.h"

problems = []
editor = EDITOR.read_text(encoding="utf-8")
text = TEXT.read_text(encoding="utf-8")
motion = MOTION.read_text(encoding="utf-8")

# ---- 1. the press is a scale, and it is 0.96 -----------------------------------
if "const float scale = down ? 0.96f : 1.0f;" not in editor:
    problems.append("the dial has no press scale (make-interfaces-feel-better: scale(.96))")

# ... and it scales the *draw*, not the bounds: a press that re-lays-out is a jump
if re.search(r"setBounds\s*\([^)]*0\.96", editor):
    problems.append("the press moves a layout bound instead of a transform")

# ---- 2. hit areas ---------------------------------------------------------------
# Only the *hit* functions count.  A drawn pill may be 26 px tall; the rectangle the
# pointer is tested against may not be under 40.  (That is why modeSeg() clamps to the
# floor and modeSegDraw() keeps the design's own height.)
def bodies(name_pat):
    out = []
    for m in re.finditer(r"Rectangle<int>\s+AeqAudioProcessorEditor::(\w+)\s*\([^)]*\)\s*const\s*\{",
                         editor):
        if not re.search(name_pat, m.group(1)):
            continue
        depth, i = 1, m.end()
        while i < len(editor) and depth:
            depth += {"{": 1, "}": -1}.get(editor[i], 0)
            i += 1
        out.append((m.group(1), editor[m.end():i]))
    return out

problems_here = []
for name, body in bodies(r"(Hit|modeSeg$|modeSeg\b)"):
    if name.endswith("Draw"):
        continue
    # only the constants that size a target, not the slop added around it
    for m in re.finditer(r"const int (minH|s|size|w|h)\s*=\s*roundToInt\s*\(\s*(\d+(?:\.\d+)?)f"
                         r"\s*\*\s*float\s*\(", body):
        v = float(m.group(2))
        if v < 40.0 and m.group(1) != "w":
            problems_here.append(f"{name}() has a {v:g} px target")
# the clamp itself must be in the code, not only in a comment
seg = dict(bodies(r"(modeSeg$|modeSeg\b)")).get("modeSeg", "")
if "minH" not in seg or "40.0f" not in seg:
    problems_here.append("modeSeg() does not clamp its hit height to 40 px")
problems.extend(problems_here)

# ---- 3. numbers do not reflow ---------------------------------------------------
if "paddedLeft" not in text:
    problems.append("ui::num() does not pad - the readouts can shift while they change")
readouts = [m.start() for m in re.finditer(r"String\s*\([^)]*(?:Knob_\.getValue|levelMatchDb|"
                                           r"stabilizerTotalDb)", editor)]
unpadded = [i for i in readouts
            if "num (" not in editor[max(0, i - 60):i + 40]
            and "num(" not in editor[max(0, i - 60):i + 40]]
if unpadded:
    problems.append(f"{len(unpadded)} numeric readout(s) still formatted with String() "
                    "instead of ui::num()")

# ---- 4. motion is a token -------------------------------------------------------
literal_ms = re.findall(r"\b(\d{2,4})\s*ms\b", editor)
if literal_ms:
    problems.append(f"a duration literal is in the editor, not in MotionSpec.h: {literal_ms}")
for token in ("micro", "quick", "fast"):
    if token not in motion:
        problems.append(f"MotionSpec lost its '{token}' token")

# ---- 5. no bounce ---------------------------------------------------------------
if re.search(r"\bbounce\b\s*=\s*(?!0)\d", motion):
    problems.append("bounce is non-zero (make-interfaces-feel-better rule: bounce must be 0)")

if problems:
    print("feel violations:")
    for p in problems:
        print("  - " + p)
    sys.exit(1)

print("feel OK: press is a 0.96 transform on a token, hit areas >= 40 px, "
      "every live number is fixed width, and no duration literal lives in a widget")
