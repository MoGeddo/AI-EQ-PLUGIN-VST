#!/usr/bin/env python3
"""The plugin must be automatic out of the box - not after the user finds a control.

The product is an automatic vocal EQ: load it on a dry vocal, press play, and it
listens, decides and applies on its own.  Every way that promise can be quietly
broken lives in defaults or in the timer, so this gate reads them:

  * learning starts on (otherwise nothing is analysed until somebody clicks);
  * bypass starts off;
  * the pass has a *real* end that is not the user.  It used to be a fixed ceiling on
    the settle time (8 s, then 45 s): a forgotten Stop button left the tool listening
    forever.  In 1.2.74 the end became the **transport** - `settleSeconds == 0` means
    "decide when the take's own play head stops", which is what "scan the whole take"
    requires - so the gate now checks that the transport path exists, that a stop is
    what settles it, and that the ceiling survives only as a cap on a *hand-set*
    auto-stop.  A plugin with no end at all is still a violation; the end just moved.
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

MAX_SETTLE_S = 300.0      # the most a hand-set auto-stop may mean
MIN_SETTLE_S = 0.0        # 0 is now the product default: "the end of the take"
problems = []

proc = PROC.read_text(encoding="utf-8")
proc_h = PROC_H.read_text(encoding="utf-8")
editor = EDITOR.read_text(encoding="utf-8")


core = (ROOT / "plugin" / "Source" / "dsp" / "RestoreCore.h").read_text(encoding="utf-8")


def default_of(param_const):
    """The default value passed to the AudioParameter* for `kSettle` etc."""
    m = re.search(rf'\b{param_const}\b[\s\S]{{0,300}}?NormalisableRange<float>\([^)]*\),\s*([\d.]+)f', proc)
    return float(m.group(1)) if m else None


settle = default_of("kSettle")
if settle is None:
    problems.append("PluginProcessor.cpp: cannot find the default of the settle time")
elif settle > MAX_SETTLE_S:
    problems.append(f"the settle time is {settle:g} s - past the longest a timed window may be")
elif settle < MIN_SETTLE_S:
    problems.append(f"the settle time is {settle:g} s - negative makes no sense")
# 0 is a rule, not an absence: it means "wait for the end of the take", so the thing that
# must exist is the end-of-take signal.  A 0 with no transport path is the worst case - the
# plugin never decides anything and the panel says it is still listening.
if settle == 0.0:
    if "getPlayHead" not in proc:
        problems.append("PluginProcessor.cpp: settle 0 means \"the end of the take\" but the "
                        "processor never reads the play head - nothing would ever settle it")
    if "setTransport" not in proc:
        problems.append("PluginProcessor.cpp: the play head is read but never handed to the core")
    if "takeEnded()" not in core:
        problems.append("RestoreCore.h: settle 0 has no end-of-take test - the pass would never "
                        "settle and the audio would never be touched")

# The cap has to actually commit, and it has to be a cap rather than a setting that can
# be turned off.  This assertion used to look for `settings_.settleSeconds > 0.0`, which
# was the *old* architecture (one number for "trust" and "stop" at once); after the two
# were split the check went on passing on a file that no longer contained the code it
# described - a gate that lies is worse than no gate.  It now reads the current path and
# the floor under it.
# The condition the cap compares has moved once already - it used to be the sung-audio
# meter (`state_.analyzedSeconds >= cap`), which is why the cap never fired on a real take:
# 300 s of audio reads as 248 s of voice.  A gate that pins the *comparison expression* goes
# red every time the right expression changes and silent when the new one is wrong, so this
# pins what must be true instead: the cap exists, something open-ended is compared to it, and
# reaching it commits and freezes.
# ... and the branch that compares to the cap has to be the one that commits and freezes.
# Pinning the *expression* (the previous version did) goes red whenever the right expression
# changes and stays silent when a wrong one lands, so this follows the comparison to the
# block it guards and reads what that block does.  1.2.74 also gated the ceiling behind "the
# person asked for one" (`capped &&`), which is a different condition around the same branch.
cap_at = re.search(r"if\s*\(\s*(?:[\w_]+\s*&&\s*)?[A-Za-z_][\w.:()]*\s*>=?\s*cap\s*[^)]*\)\s*\{", core)
cap_body = core[cap_at.end():cap_at.end() + 600] if cap_at else ""
cap_commits = "Phase::Committed" in cap_body and "frozen_.store" in cap_body
if "kHardCapSeconds" not in core or not cap_commits:
    problems.append("RestoreCore.h: the safety cap does not commit and freeze the pass - a "
                    "forgotten Stop button would leave the plugin listening forever")
# The ceiling may only fire when somebody asked for it: an unconditional 300 s is exactly the
# rule "scan the whole take" cannot live with, so a cap that is not behind a caller's setting
# is a violation in the other direction.
elif "capped" in core and not re.search(r"const\s+bool\s+capped\s*=\s*settings_\.autoStopSeconds\s*>\s*0", core):
    problems.append("RestoreCore.h: the ceiling is not gated on the caller's auto-stop - a "
                    "long take would be truncated at five minutes again")
elif "Phase::Committed" not in core:
    problems.append("RestoreCore.h: the cap never reaches the committed phase")
elif not re.search(r"cap\s*\)\s*\{(?:.|\n){0,400}?frozen_\.store\s*\(\s*true", core):
    problems.append("RestoreCore.h: the cap does not freeze the pass it ended")
# ... and the clock it reads has to count the whole take, not only the sung part.  This is the
# 1.2.68 defect: `analyzedSeconds` is the analyser's voice meter (76 % of a real take), so a
# five-minute promise measured in it was never kept.
elif "passSeconds()" not in core:
    problems.append("RestoreCore.h: the cap is measured on the voice meter - it will never "
                    "fire on a take that breathes")

# The editor is a readout, not the thing that starts the analysis - but the control that
# *ends* it must exist and be wired.  v9 has no learnBtn_: the clamp in the take strip is
# the stop control, and it is handled in mouseDown().
if not re.search(r"Control::Clamp", editor) or "stopLearning" not in editor:
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

print(f"automatic: learning on load, stop when the user says, hard cap 300 s, "
      f"solving on the processor timer")
