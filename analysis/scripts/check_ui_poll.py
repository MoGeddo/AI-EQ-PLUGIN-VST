#!/usr/bin/env python3
"""The window polls the solver; it must not re-copy the world when nothing changed.

The editor runs a timer.  Every tick used to copy the whole published state, the
measurement vectors, the band list and up to 24 problem texts, run a z-scan over 86
bands, rebuild the report list and repaint the entire window - three times for every
update that actually existed, because the solver publishes at 4 Hz and the timer ran
at 12 Hz.  That is what makes a plugin window feel sticky while the audio plays.

`RestoreCore::version()` is the fix: it is bumped only when the published state
changes, and `timerCallback()` returns on the first line if it has not.

This gate is textual on purpose - the editor needs JUCE and a display, and this has
to run in three seconds on any machine - but it checks the two things that can
silently regress:

  1. timerCallback() compares version() and returns before doing any work;
  2. the timer does not run faster than the solver can publish.

    python3 analysis/scripts/check_ui_poll.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EDITOR = ROOT / "plugin" / "Source" / "PluginEditor.cpp"
CORE = ROOT / "plugin" / "Source" / "dsp" / "RestoreCore.h"

MAX_HZ = 10          # the solver publishes at 4 Hz; 20 Hz of polling is pure waste
problems = []

text = EDITOR.read_text(encoding="utf-8")
core = CORE.read_text(encoding="utf-8")

# 1. the version exists at all
if "long version() const" not in core:
    problems.append("RestoreCore has no version() - the editor cannot tell whether "
                    "anything changed, so it will copy everything on every tick")

# 2. the early return, before the copies
m = re.search(r"void AeqAudioProcessorEditor::timerCallback\(\)\s*\{(.*?)\n\}", text, re.S)
if not m:
    problems.append("PluginEditor.cpp: timerCallback() not found - fix the script before trusting it")
else:
    body = m.group(1)
    first_copy = body.find("proc_.core().state()")
    guard = body.find("version()")
    if first_copy < 0:
        problems.append("timerCallback() no longer reads core().state() - fix the script")
    elif guard < 0 or guard > first_copy:
        problems.append("timerCallback() copies the state before checking version(): every "
                        "tick still duplicates the measurements, the bands and every "
                        "problem string, then repaints")
    elif not re.search(r"return\s*;", body[guard:first_copy]):
        problems.append("timerCallback() checks version() but does not return on it")

# 3. the timer rate
hz = re.search(r"startTimerHz\s*\(\s*(\d+)\s*\)", text)
if not hz:
    problems.append("PluginEditor.cpp: no startTimerHz() call found")
elif int(hz.group(1)) > MAX_HZ:
    problems.append(f"the editor polls at {hz.group(1)} Hz; the solver publishes at 4 Hz, "
                    f"so anything above {MAX_HZ} re-renders the same data for nothing")

if problems:
    print("UI polling violations:")
    for p in problems:
        print("  - " + p)
    sys.exit(1)

print(f"the window polls at {hz.group(1)} Hz and does no work when nothing changed")
