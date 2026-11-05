#!/usr/bin/env python3
"""The plugin must be automatic out of the box - not after the user finds a control.

The product is an automatic vocal EQ: load it on a dry vocal, press play, and it
listens, decides and applies on its own.  Every way that promise can be quietly
broken lives in defaults or in the timer, so this gate reads them:

  * learning starts on (otherwise nothing is analysed until somebody clicks);
  * bypass starts off;
  * the safety cap exists and is finite: the user stops the pass themselves, but a
    forgotten Stop button may not leave the tool listening forever (the cap used to
    be "the settle time" at 8 s; the workflow is now learn -> stop, so the 8 s became
    a 45 s ceiling);
  * the settling, solving and applying happen on the *processor's* timer, so the
    plugin works with its editor closed - a timer in the editor would make the
    whole thing stop the moment the window is hidden;
  * the editor's "Analyze" is a readout, not the thing that starts the analysis.

    python3 analysis/scripts/check_auto_defaults.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROC = ROOT / "plugin" / "Source" / "PluginProcessor.cpp"
PROC_H = ROOT / "plugin" / "Source" / "PluginProcessor.h"
EDITOR = ROOT / "plugin" / "Source" / "PluginEditor.cpp"

MAX_SETTLE_S = 300.0      # a ceiling, not a schedule: it only fires if nobody stops it
MIN_SETTLE_S = 1.0        # 0 would mean "commit with nothing heard"
problems = []

proc = PROC.read_text(encoding="utf-8")
proc_h = PROC_H.read_text(encoding="utf-8")
editor = EDITOR.read_text(encoding="utf-8")


def default_of(param_const):
    """The default value passed to the AudioParameter* for `kSettle` etc."""
    m = re.search(rf'\b{param_const}\b[\s\S]{{0,300}}?NormalisableRange<float>\([^)]*\),\s*([\d.]+)f', proc)
    return float(m.group(1)) if m else None


settle = default_of("kSettle")
if settle is None:
    problems.append("PluginProcessor.cpp: cannot find the default of the settle time")
elif settle > MAX_SETTLE_S:
    problems.append(f"the safety cap is {settle:g} s - too long for a forgotten Stop button")
elif settle < MIN_SETTLE_S:
    problems.append(f"the safety cap is {settle:g} s, i.e. the plugin would commit after "
                    f"hearing nothing - the user's own stop is the normal path")

# the cap has to actually commit from the analysis thread, not just be a number
core = (ROOT / "plugin" / "Source" / "dsp" / "RestoreCore.h").read_text(encoding="utf-8")
if "settings_.settleSeconds > 0.0 && state_.settled" not in core or \
        "Phase::Committed" not in core:
    problems.append("RestoreCore.h: the safety cap does not commit anything - a forgotten "
                    "Stop button would leave the plugin listening forever")

# and the editor must not be what starts the analysis
if not re.search(r"learnBtn_\.onClick", editor):
    problems.append("PluginEditor.cpp: the learn control has no click handler at all")

if not re.search(r"std::atomic<bool>\s+learning_\{true\}", proc_h):
    problems.append("PluginProcessor.h: learning does not start enabled - nothing is analysed "
                    "until the user finds a control")

if not re.search(r'AudioParameterBool>\(kBypass,\s*"Bypass",\s*false\)', proc):
    problems.append("Bypass does not default to off")

# the work has to be on the processor's timer, not the editor's
if "core_.refresh()" not in proc:
    problems.append("PluginProcessor.cpp: the processor never refreshes the solver - "
                    "the plugin would only work while its window is open")
if not re.search(r"void AeqAudioProcessor::timerCallback\(\)", proc):
    problems.append("PluginProcessor.cpp: no timerCallback() in the processor")
if re.search(r"startTimerHz", editor) and "core_.refresh()" not in editor:
    pass   # the editor's timer is for redraws; the analysis timer is the processor's

# the button in the editor may not be what starts the analysis
if re.search(r"learnBtn_\.onClick[^;]*setLearning\s*\(\s*learnBtn_\.getToggleState", editor):
    problems.append("PluginEditor.cpp: the analyse button still gates whether the plugin "
                    "listens - the tool decides that, not the user")

if problems:
    print("automatic-by-default violations:")
    for p in problems:
        print("  - " + p)
    sys.exit(1)

print(f"automatic: learning on load, stop when the user says, safety cap {settle:g} s, "
      f"solving on the processor timer")
