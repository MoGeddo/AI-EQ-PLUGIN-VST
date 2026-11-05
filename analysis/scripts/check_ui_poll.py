#!/usr/bin/env python3
"""The window polls the solver; it must not re-copy the world when nothing changed.

The editor runs a timer.  Every tick used to copy the whole published state, the
measurement vectors, the band list and up to 24 problem texts, run a z-scan over 86
bands, rebuild the report list and repaint the entire window - three times for every
update that actually existed, because the solver publishes at 4 Hz and the timer ran
at 12 Hz.  That is what makes a plugin window feel sticky while the audio plays.

`RestoreCore::version()` is the fix: it is bumped only when the published state
changes, and `timerCallback()` returns on the first line if it has not.

There is a second, much faster stream since then: the *display* spectrum
(`RestoreCore::live()`, attack 30 ms / release 120 ms) which the plugin's own timer
publishes ~27 times a second.  The editor may poll that fast, and must, because a
curve that updates four times a second is what the user described as "the analyser
is incredibly slow".  The rule is therefore not "poll slowly" but "the fast path
touches nothing but the spectrum":

  1. timerCallback() compares version() and returns before copying the state;
  2. the fast path is `setLive()`, which assigns two vectors and repaints one
     component - no state copy, no report rebuild, no full-window repaint;
  3. the timer is not faster than the display stream can justify (30 Hz).

    python3 analysis/scripts/check_ui_poll.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EDITOR = ROOT / "plugin" / "Source" / "PluginEditor.cpp"
CORE = ROOT / "plugin" / "Source" / "dsp" / "RestoreCore.h"

MAX_HZ = 32          # the display stream runs ~27 Hz; more than that is pure waste
LIVE_HZ = 20         # below this the curve visibly steps
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
    problems.append(f"the editor polls at {hz.group(1)} Hz; the display stream runs at "
                    f"~27 Hz, so anything above {MAX_HZ} re-renders the same data")
elif int(hz.group(1)) < LIVE_HZ:
    problems.append(f"the editor polls at {hz.group(1)} Hz, below the display stream - "
                    f"the analyser will step instead of move")

# 4. the fast path must exist and must be cheap
if "setLive" not in text:
    problems.append("timerCallback() has no fast path for the display spectrum: every "
                    "live frame would go through the full report rebuild")
if "setLive" not in (ROOT / "plugin" / "Source" / "ui" / "CurveView.h").read_text(encoding="utf-8"):
    problems.append("CurveView has no setLive(): the moving spectrum has nowhere to go")
if "pump()" not in (ROOT / "plugin" / "Source" / "PluginProcessor.cpp").read_text(encoding="utf-8"):
    problems.append("the processor never pumps the display stream, so live() would never change")

if problems:
    print("UI polling violations:")
    for p in problems:
        print("  - " + p)
    sys.exit(1)

print(f"the window polls at {hz.group(1)} Hz: "
      f"{'the spectrum moves every tick, everything else only when it changed' if int(hz.group(1)) > 10 else 'no work when nothing changed'}")
