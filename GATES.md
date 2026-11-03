# Gates: harmonic envelope in the DSP core

OWNS: plugin/Source/dsp/**, plugin/Tests/**, plugin/cli/**, analysis/scripts/envelope_demo.py, docs/13_MARKET_TEARDOWN_AR.md, docs/14_HARMONIC_ENVELOPE_AR.md

Scope: measure the vocal in two coordinate systems - the raw spectrum (what we have) and
the harmonic envelope (what the next step adds) - and prove the envelope removes the
singer's register from the number without going blind on real faults.

Method: `github.com/Leonxlnx/unlazy` (GATES before work, one coherent deliverable per
leaf, four passes: implement -> reread as expert -> hunt defects -> polish). Structural
check, with that repo cloned next to this one:

```text
node ../unlazy/scripts/gate-check.mjs --status GATES.md      # then --approve
```

Reproducing: the CHECK lines build into `build/` and need a g++ with C++17. G5 needs a
vocal wav that is **not in this repo** (the corpus is copyrighted), so point `AEQ_WAV`
at any vocal stem first; the other gates need nothing but the source tree.

- [x] G1: the envelope estimator sees a formant shape, not a comb
  CHECK: mkdir -p build && g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_core.cpp -o build/aeq_tests && build/aeq_tests
  EXPECT: [G8] envelope removes the singer from the number
  EVIDENCE: automatic-evidence=v1; definition-sha256=b662c1350c3ff6010187889caa09401792cddbe83ac11d60880bfe5d311cbc37; exit=0; EXPECT=matched; output-sha256=197e6a51e42f7890dc1dd0bc997fcf1f5b40ee64e4b7c4a29697c06471c29c9f; output-bytes=2451; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G2: the same signal at two pitches gives the same envelope within 1.5 dB,
      while the raw spectrum differs by more than 3 dB
  CHECK: sh -c "ulimit -s 512; build/aeq_tests"
  EXPECT: envelope cuts that dependence by more than 3x
  EVIDENCE: automatic-evidence=v1; definition-sha256=682e03ead91e5f3a4e0e0ae1218429435aec149fb4eb4be1d4b82aa1216a0103; exit=0; EXPECT=matched; output-sha256=a7cb0cad94d80a498c94f27dc689d358212be83d5856354a2a8a295b7cad27a0; output-bytes=3048; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G3: nothing else regressed - the original gates still pass, and G8/G16 add asserts
  CHECK: sh -c "ulimit -s 512; build/aeq_tests"
  EXPECT: 28 passed, 0 failed
  EVIDENCE: automatic-evidence=v1; definition-sha256=22ae5270ed89573d9a11e2e666fa852662075523c417355197d0013ebbfa9003; exit=0; EXPECT=matched; output-sha256=a7cb0cad94d80a498c94f27dc689d358212be83d5856354a2a8a295b7cad27a0; output-bytes=3048; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G4: determinism survives the new accumulator
  CHECK: sh -c "ulimit -s 512; build/aeq_tests"
  EXPECT: [G6] determinism
  EVIDENCE: automatic-evidence=v1; definition-sha256=c6d6e14ab2537e9a8c33114ab2f8554cd2454556a40f234038504c21e743f073; exit=0; EXPECT=matched; output-sha256=a7cb0cad94d80a498c94f27dc689d358212be83d5856354a2a8a295b7cad27a0; output-bytes=3048; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G5: the shipped CLI exposes the envelope for every analysis, so any claim can be
      rechecked on real material
  CHECK: mkdir -p build && g++ -std=c++17 -O2 -Iplugin/Source plugin/cli/main.cpp -o build/aeq_cli && build/aeq_cli analyze "$AEQ_WAV"
  EXPECT: envelope
  EVIDENCE: automatic-evidence=v1; definition-sha256=cb726016d65e876855f0618204cfd2223df5717e49a1f288a655f3939975f3fb; exit=0; EXPECT=matched; output-sha256=29452e13c85fc2066df8d9edcfea6e19df7536072e1a7b277ef946804dc18f6e; output-bytes=3781; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G6: on the 15 reference mixes the envelope's spread in 110-260 Hz is materially
      smaller than the raw spread (the reason the whole step exists)
  CHECK: .venv/bin/python analysis/scripts/envelope_demo.py
  EXPECT: tolerance shrinks by
  EVIDENCE: automatic-evidence=v1; definition-sha256=32193dd26116e4eef07cba7a613320249aab3621bfe20229fa0f1bd439d689b0; exit=0; EXPECT=matched; output-sha256=7cf9498a8d8465cd373f5569741b16e9695b7758af00b1870eedf95491b1eef6; output-bytes=2599; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G9: the envelope is used for the decision, and the bars derived from it do not
  EVIDENCE: automatic-evidence=v1; definition-sha256=b625895bef0cf455a8a2614a7d8012e1c658dc8b28cae93079fa944f97d0703d; exit=0; EXPECT=matched; output-sha256=cac5fddcbf56d652ee7879f0e6e3b6a9076a70a21f52609e155c402f13f1a4d4; output-bytes=1363; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      flag the reference material - a fire on his own 15 mixes would be a false alarm
      by definition
  CHECK: .venv/bin/python analysis/scripts/audit_decisions.py
  EXPECT: GATE PASS: no envelope-scored region fires

- [x] G10: where the envelope scores the decision it is at least 1.5x closer to catching
  EVIDENCE: automatic-evidence=v1; definition-sha256=2cdfd3c588b1c6bdba567dcfb98b040e9278df3dc1b179063e1e6ce7b759fa46; exit=0; EXPECT=matched; output-sha256=2011c3ddff56875ff14a9a1123168e7704fd1ca9ec53db926376512206175100; output-bytes=1329; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      a known +3 dB fault than the raw measurement is - otherwise the swap is decoration
  CHECK: .venv/bin/python analysis/scripts/calibrate_env.py
  EXPECT: GATE PASS: every envelope-scored region is at least 1.5x closer

- [x] G11: the threshold constants come from the shipped binary, not from the Python
  EVIDENCE: automatic-evidence=v1; definition-sha256=e567e139f183e9c2280aabddd24c00a2793c8a385daff5b75b7a8bb6a0788c5b; exit=0; EXPECT=matched; output-sha256=68981ceec28243e279a9b4bf48daabcd333e211c4abbfd9dbc1f6eedba314f9c; output-bytes=1098; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      reference and not from a hand-typed number (the two disagree by 0.9 dB on the
      same band, which is the difference between 0 and 3 false alarms)
  CHECK: head -30 plugin/Source/model/EnvThresholds.h
  EXPECT: Generated by analysis/scripts/calibrate_env.py

- [x] G12: the bottom weight of the voice is measurable on every reference take - the
  EVIDENCE: automatic-evidence=v1; definition-sha256=00dba793a6cc7c1075ef60d27da29371cd7fae7bbdcd4d60957692c47b044cbc; exit=0; EXPECT=matched; output-sha256=fe39cab72673f19e38c861adc5f58ca3184712a320d393657b26b148fd8eff10; output-bytes=1641; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      fixed 110-260 Hz band it replaces was measurable on 0 of 15
  CHECK: .venv/bin/python analysis/scripts/audit_decisions.py
  EXPECT: measurable on 15/15 takes

- [x] G13: the sigma readout against the 15 reference mixes never reaches alarm level on
  EVIDENCE: automatic-evidence=v1; definition-sha256=2471217a706a6ea77e5224d88a4521e89ced7d8820d9f163d09257ebb9bbce28; exit=0; EXPECT=matched; output-sha256=fe39cab72673f19e38c861adc5f58ca3184712a320d393657b26b148fd8eff10; output-bytes=1641; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      the reference material itself (<= 3.2 sigma), so it can be shown as evidence
  CHECK: .venv/bin/python analysis/scripts/audit_decisions.py
  EXPECT: largest smoothed z on the reference material

- [x] G14: the target and the measurement are the same statistic - no member of the 15
  EVIDENCE: automatic-evidence=v1; definition-sha256=f4fd6f5a463985d90e5f6661360098ec43659fae52912a56714e62dce96ab319; exit=0; EXPECT=matched; output-sha256=d4b51dbea3df31e53548fe419605842156492790c45960d8b2503d2ac85f1fb7; output-bytes=1522; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      may sit further than sqrt(14) sigma from its own average; a Python-built target
      put one at 4.6, which is what that check is for
  CHECK: .venv/bin/python analysis/scripts/env_target.py
  EXPECT: largest member deviation

- [x] G15: a known broad fault is actually caught: +4 dB Q1.5 at 665 Hz on at least 2 of
  EVIDENCE: automatic-evidence=v1; definition-sha256=59a84f525a145df908bbd69b1f358e2f41e8407309b6772c15fbccce43d6f52d; exit=0; EXPECT=matched; output-sha256=629e48f57a52df2c03dac5f8fc8e4a677b1ae8cfc89e06c75663b7647b8c9ac6; output-bytes=296; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      3 real takes (the raw-spectrum detector caught 0 of 3)
  CHECK: .venv/bin/python analysis/scripts/bench_faults.py --freq 665 --q 1.5 --db 4 --expect 2
  EXPECT: GATE PASS: fault caught

- [x] G16: every report line is fully formatted - no leftover '%' from a format whose
  EVIDENCE: automatic-evidence=v1; definition-sha256=28b2089bc2796347080f86a6b66ab66eb0cdee6c7d3d9155f93beda5cb1de917; exit=0; EXPECT=matched; output-sha256=a7cb0cad94d80a498c94f27dc689d358212be83d5856354a2a8a295b7cad27a0; output-bytes=3048; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      argument count did not match.  A '%%' in the de-esser line was counted as a
      placeholder, read past its argument list, and killed the Windows gate binary
      before it printed a line (found with -fsanitize=address)
  CHECK: sh -c "ulimit -s 512; build/aeq_tests"
  EXPECT: no leftover %% in any report line

- [x] G17: the gate binary runs inside 512 KB of stack, because Windows gives 1 MB and
  EVIDENCE: automatic-evidence=v1; definition-sha256=3f4af598502f43fcc3378b49920b2dac382efed817e0617fd2629d6eb0817f50; exit=0; EXPECT=matched; output-sha256=a7cb0cad94d80a498c94f27dc689d358212be83d5856354a2a8a295b7cad27a0; output-bytes=3048; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      a 210 KB RestoreCore on the stack (three 8192-point double arrays in the analyser)
      plus ten of them in one function killed the MSVC build with 0xC00000FD before it
      printed a line
  CHECK: sh -c 'ulimit -s 512; build/aeq_tests'
  EXPECT: 28 passed, 0 failed

- [x] G19: the editor compiles against a pristine JUCE 8 tree before CI spends six
  EVIDENCE: automatic-evidence=v1; definition-sha256=be7c107a665d3fa2b44e488c5dfcdecd273d6621ff63a5e85fdcbcf159673239; exit=0; EXPECT=matched; output-sha256=5273b6773ddfb701faae4d7c6e463fd89f26efb12017bda393875398f4dcb487; output-bytes=36; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      minutes on the Windows build (a changed JUCE API or a missing include is a
      seconds-long check, not a build-and-wait)
  CHECK: JUCE_DIR=/tmp/JUCE-8.0.4 ./plugin/check_ui_compile.sh
  EXPECT: UI compiles against

- [x] G20: the design preview is generated from the table the plugin actually compiles
  EVIDENCE: automatic-evidence=v1; definition-sha256=3a0f2d81017b1332216fbe336b879372f639ff874a3b933a3d2fc61d93406465; exit=0; EXPECT=matched; output-sha256=068bbd40a33fa7582fb0a825b83c4fc5c14f6a8d1a20aa4af63eb321d9d0ba5c; output-bytes=65; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      in, not from numbers typed into the drawing (it reads EnvTargetCurve.h and
      fails if the table cannot be found)
  CHECK: /tmp/v/bin/python analysis/scripts/ui_preview.py
  EXPECT: wrote

- [x] G21: the report prints its numbers, not its format string - a tilt card carries four
      numbers and the formatter only had cases for three, so every low-end card in the
      window read "القاع أثقل من الجسم بـ % .1f dB".  G16 missed it because its take
      never produced that line; this one renders a tilted take on purpose
  CHECK: sh -c "ulimit -s 512; build/aeq_tests"
  EXPECT: no leftover %% in any report line of the tilted take

- [x] G22: the audio thread and the editor never touch the same object unprotected - the
      crash in FL Studio was setBandTrim() rebuilding the biquad cascade under the audio
      thread.  600 blocks of audio, 40 analyses and 200 handle drags on three threads,
      built with ThreadSanitizer where the compiler has it
  CHECK: ./build/aeq_threads
  EXPECT: 2 passed, 0 failed

- [x] G23: the Arabic reaches the binary as Arabic - juce::String(const char*) reads its
      bytes as ASCII, so a UTF-8 literal that goes through it becomes one character per
      byte ("تخنة" arrived as U+00D8 U+00AA U+00D8 U+00AE and painted as .notdef boxes while
      the Latin in the same sentence rendered).  Every string now goes through ui::u8();
      this checks the compiled binary on the runner that builds it, and fails if the
      literal and a \uXXXX escape disagree, or if the embedded typeface has no glyph for
      U+062A
  CHECK: python3 analysis/scripts/check_utf8_literals.py
  EXPECT: 0 non-ASCII literal(s) bypassing the UTF-8 funnel

- [x] G24: the window lays out its controls at the size it opens at - resized() runs
      inside setSize(), the constructor called setSize() before buildControls(), so the
      only layout pass ran over an empty control list and every knob, label and value
      stayed at 0x0.  The default size also came from nowhere: on a laptop the bottom row
      fell outside the host's window.  The gate asserts the knobs have a real height and
      the window fits inside 6/7 of the display it is opened on
  CHECK: python3 analysis/scripts/check_layout.py
  EXPECT: layout OK
