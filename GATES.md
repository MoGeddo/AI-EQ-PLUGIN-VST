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
  CHECK: build/aeq_tests
  EXPECT: envelope cuts that dependence by more than 3x
  EVIDENCE: automatic-evidence=v1; definition-sha256=42ff9fc93b23768f5a3116cbc49ba2c3eb379fab7be297f5658feb6a6239c99d; exit=0; EXPECT=matched; output-sha256=197e6a51e42f7890dc1dd0bc997fcf1f5b40ee64e4b7c4a29697c06471c29c9f; output-bytes=2451; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G3: nothing else regressed - the 18 original gates still pass, and G8 adds 4 asserts
  CHECK: build/aeq_tests
  EXPECT: 22 passed, 0 failed
  EVIDENCE: automatic-evidence=v1; definition-sha256=1f0ef911fae8c3e5abbdcbd30f194d01f53298760112e68fb257c6a8e36f95d7; exit=0; EXPECT=matched; output-sha256=197e6a51e42f7890dc1dd0bc997fcf1f5b40ee64e4b7c4a29697c06471c29c9f; output-bytes=2451; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

- [x] G4: determinism survives the new accumulator
  CHECK: build/aeq_tests
  EXPECT: [G6] determinism
  EVIDENCE: automatic-evidence=v1; definition-sha256=4c697ac049ec7ea12f5c69d8f200b5da3596ba60cb1863490a7852edc618dc3c; exit=0; EXPECT=matched; output-sha256=197e6a51e42f7890dc1dd0bc997fcf1f5b40ee64e4b7c4a29697c06471c29c9f; output-bytes=2451; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

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
