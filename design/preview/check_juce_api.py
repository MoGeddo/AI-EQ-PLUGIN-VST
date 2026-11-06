#!/usr/bin/env python3
"""check_juce_api.py - the UI headers compile against the JUCE we actually pin.

This is context7's idea taken literally.  Its whole premise is that a model writes
API calls from memory, the memory is a version behind, and the build breaks six
minutes later on a Windows runner.  The fix is not "be careful", it is *pin the
version and compile the thing*.  So:

  1. the version is read out of plugin/CMakeLists.txt (the GIT_TAG that FetchContent
     uses) - not hard-coded here, so bumping JUCE cannot leave this gate checking an
     older one;
  2. the JUCE checkout on disk must be at that exact tag, otherwise the result means
     nothing ("it compiled against something else" is the bug this gate exists for);
  3. every UI header is compiled with -fsyntax-only against it.  Syntax-only is the
     right depth: it catches a signature that moved between versions (which is the
     failure mode), without spending six minutes linking modules the test does not
     use.

The two stubs it writes (JuceHeader.h and BinaryData.h) are what CMake generates at
configure time; compiling a header without them would fail for reasons that have
nothing to do with the API.

    JUCE_PATH=/tmp/JUCE-8.0.4 /tmp/venv/bin/python design/preview/check_juce_api.py
"""
from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CMAKE = ROOT / "plugin" / "CMakeLists.txt"
SRC = ROOT / "plugin" / "Source"

problems: list[str] = []
notes: list[str] = []

# ------------------------------------------------------------------ 1. the pin
cmake = CMAKE.read_text()
m = re.search(r"FetchContent_Declare\(JUCE.*?GIT_TAG\s+([\w.\-]+)", cmake, re.S)
if not m:
    print("FAIL plugin/CMakeLists.txt does not pin a JUCE tag")
    sys.exit(1)
PINNED = m.group(1)

# ------------------------------------------------------------------ 2. the checkout
JUCE = pathlib.Path(os.environ.get("JUCE_PATH", "/tmp/JUCE-8.0.4"))
if not (JUCE / "modules" / "juce_gui_basics").exists():
    print(f"FAIL no JUCE checkout at {JUCE} - fetch it with\n"
          f"  git clone --depth 1 --branch {PINNED} "
          f"https://github.com/juce-framework/JUCE.git {JUCE}\n"
          "(-fsyntax-only needs the module headers, not a build)")
    sys.exit(1)

head = (JUCE / "modules" / "juce_core" / "juce_core.h").read_text(errors="ignore")[:4000]
vm = re.search(r"version:\s*([\d.]+)", head)
on_disk = vm.group(1) if vm else "unknown"
if on_disk != PINNED:
    problems.append(f"the checkout at {JUCE} is JUCE {on_disk}, CMake pins {PINNED} - "
                    "a compile against the wrong version proves nothing")
else:
    notes.append(f"JUCE {PINNED} pinned in CMake and present on disk")

# ------------------------------------------------------------------ 3. compile
cxx = shutil.which("g++") or shutil.which("clang++")
if not cxx:
    print("FAIL no C++ compiler available")
    sys.exit(1)

HEADERS = ["ui/Theme.h", "ui/MotionSpec.h", "ui/ArabicText.h", "ui/CurveView.h",
           "ui/ProblemList.h"]

# ...and the editor itself.  The UI headers compiled for four versions while the
# window they were supposed to describe still drew the old layout; a gate that stops
# at the headers cannot see that.  PluginEditor.cpp is the translation unit that
# actually paints the panel, so it is compiled here too.
EDITOR = "PluginEditor.cpp"

# and the screenshot test, which is the second translation unit that paints.  It broke
# the Windows build on 1.2.9 by using ProblemList through someone else's include.
SHOT = "Tests/ui_shot.cpp"
TESTS = ROOT / "plugin" / "Tests"
missing = [h for h in HEADERS if not (SRC / h).exists()]
if missing:
    problems.append("headers named in this gate no longer exist: " + ", ".join(missing))

STUB = {
    "JuceHeader.h": "#pragma once\n#include <juce_gui_basics/juce_gui_basics.h>\n",
    "BinaryData.h": (
        "#pragma once\n#include <juce_core/juce_core.h>\n"
        "// stub for the syntax check: juce_add_binary_data() generates the real one\n"
        "namespace BinaryData {\n"
        '    static const char* CairoRegular_ttf = "";\n'
        "    static const int CairoRegular_ttfSize = 0;\n"
        '    static const char* CairoSemiBold_ttf = "";\n'
        "    static const int CairoSemiBold_ttfSize = 0;\n"
        "}\n"),
}

with tempfile.TemporaryDirectory() as td:
    tdp = pathlib.Path(td)
    for name, body in STUB.items():
        (tdp / name).write_text(body)
    tu = tdp / "tu.cpp"
    tu.write_text("".join(f'#include "{h}"\n' for h in HEADERS if (SRC / h).exists()))
    cmd = [cxx, "--std=c++17", "-fsyntax-only", "-Wall", "-Wextra",
           "-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1", "-DJUCE_WEB_BROWSER=0",
           "-DJUCE_USE_CURL=0", f"-I{JUCE / 'modules'}", f"-I{tdp}", f"-I{SRC}",
           str(tu)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        problems.append("the UI headers do not compile against JUCE "
                        f"{on_disk}:\n{(r.stderr or r.stdout).strip()[:800]}")
    else:
        warnings = [ln for ln in (r.stderr or "").splitlines() if "warning:" in ln]
        notes.append(f"{len(HEADERS)} UI headers syntax-check clean against JUCE {on_disk}"
                     + (f" ({len(warnings)} warnings)" if warnings else " with -Wall -Wextra"))

    editor = SRC / EDITOR
    if not editor.exists():
        problems.append(f"{EDITOR} is missing - there is nothing to draw the window")
    else:
        cmd2 = [cxx, "--std=c++17", "-fsyntax-only", "-Wall", "-Wextra",
                "-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1", "-DJUCE_WEB_BROWSER=0",
                "-DJUCE_USE_CURL=0", f"-I{JUCE / 'modules'}", f"-I{tdp}", f"-I{SRC}",
                str(editor)]
        r2 = subprocess.run(cmd2, capture_output=True, text=True)
        if r2.returncode != 0:
            problems.append(f"{EDITOR} does not compile against JUCE {on_disk}:\n"
                            f"{(r2.stderr or r2.stdout).strip()[:900]}")
        else:
            w2 = [ln for ln in (r2.stderr or "").splitlines() if "warning:" in ln]
            notes.append(f"the editor ({EDITOR}) syntax-check clean"
                         + (f" with {len(w2)} warnings" if w2 else " with -Wall -Wextra"))

    # The dial rule, as a gate rather than as a good intention.  A knob's angle is a
    # *proportion*; a parameter value is not.  In 1.2.12 drawRotarySlider was handed
    # s.getValue() directly, so 100 % of "strength" became 26 865 degrees and both knobs
    # span like a slot machine on the way to their value.  Two checks, both cheap:
    # (1) the value must go through valueToProportionOfLength;
    # (2) the box the child is given must be big enough for a ring drawn at 1.19 x the
    #     body - at 42 px the outer arc was clipped by the child's own bounds.
    if editor.exists():
        src = editor.read_text()
        call = re.search(r"void drawRotarySlider.*?override\s*\{(.*?)\n    \}", src, re.S)
        # comments out: this gate reads code, and a comment that names the fix is not the fix
        body = "\n".join(ln.split("//")[0] for ln in (call.group(1) if call else "").splitlines())
        if not body.strip():
            problems.append("there is no drawRotarySlider to check - the dial rule cannot "
                            "be verified without it")
        elif (re.search(r"\.getValue\s*\(\s*\)", body)
              and not re.search(r"valueToProportionOfLength\s*\(", body)):
            problems.append("drawRotarySlider hands the dial a parameter value instead of "
                            "a proportion (valueToProportionOfLength): the needle spins")
        box = re.search(r"const int box = roundToInt \(([0-9.]+)f \* sy\)", src)
        if box and float(box.group(1)) < 50.0:
            problems.append(f"the knob box is {box.group(1)} px - the value ring is drawn "
                            "at 1.19 x the body and gets clipped by the child's bounds")
        notes.append("the dials take proportions, not values, and their boxes hold the ring")

    shot = SRC.parent / "Tests" / "ui_shot.cpp"
    if shot.exists():
        # JUCE_MODAL_LOOPS_PERMITTED: the shot runs the message loop while it analyses,
        # which is how it renders a settled panel instead of an empty one.
        cmd3 = [cxx, "--std=c++17", "-fsyntax-only", "-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1",
                "-DJUCE_MODAL_LOOPS_PERMITTED=1", "-DJUCE_WEB_BROWSER=0",
                "-DJUCE_USE_CURL=0", f"-I{JUCE / 'modules'}",
                f"-I{tdp}", f"-I{SRC}", str(shot)]
        r3 = subprocess.run(cmd3, capture_output=True, text=True)
        if r3.returncode != 0:
            problems.append(f"{SHOT} does not compile against JUCE {on_disk}:\n"
                            f"{(r3.stderr or r3.stdout).strip()[:900]}")
        else:
            notes.append(f"the screenshot test ({SHOT}) syntax-check clean")
    else:
        problems.append(f"{SHOT} is missing")

# ------------------------------------------------------------------ verdict
if problems:
    for p in problems:
        print("FAIL " + p)
    print(f"{len(problems)} problem(s)")
    sys.exit(1)
print("juce ok: " + "; ".join(notes))
