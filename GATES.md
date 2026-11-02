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
