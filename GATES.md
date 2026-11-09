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
  EVIDENCE: automatic-evidence=v1; definition-sha256=6c0535fbf53014890cd6475d35f6ae5115147ceb0a57393612cf02bdd4b3322d; exit=0; EXPECT=matched; output-sha256=20da56d581a816e21b51a944640dff04b4fe6811ee7b3f59d02047ca2bbabbda; output-bytes=3251; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries

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
  EVIDENCE: automatic-evidence=v1; definition-sha256=6d87f7d81dfef6659c55bf4b8438f09c3f4e1ddecfe33f67362fe8d25c67b7bd; exit=0; EXPECT=matched; output-sha256=20da56d581a816e21b51a944640dff04b4fe6811ee7b3f59d02047ca2bbabbda; output-bytes=3251; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
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
  EVIDENCE: automatic-evidence=v1; definition-sha256=98fbb45b2c0fee5ef7ca4b35552796b58a590ef66fc0dfe7876d880f7025da1a; exit=0; EXPECT=matched; output-sha256=20da56d581a816e21b51a944640dff04b4fe6811ee7b3f59d02047ca2bbabbda; output-bytes=3251; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      numbers and the formatter only had cases for three, so every low-end card in the
      window read "القاع أثقل من الجسم بـ % .1f dB".  G16 missed it because its take
      never produced that line; this one renders a tilted take on purpose
  CHECK: sh -c "ulimit -s 512; build/aeq_tests"
  EXPECT: no leftover %% in any report line of the tilted take

- [x] G22: the audio thread and the editor never touch the same object unprotected - the
  EVIDENCE: automatic-evidence=v1; definition-sha256=6138485228bb4fcff38fbcf9ab777ff1b9d184b0c996d74a3595d063bc4e8b62; exit=0; EXPECT=matched; output-sha256=6037ea7a48043b0ead4a83b7d8b422dd716ae20d9b10395eadae089e54482bc2; output-bytes=181; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      crash in FL Studio was setBandTrim() rebuilding the biquad cascade under the audio
      thread.  600 blocks of audio, 40 analyses and 200 handle drags on three threads,
      built with ThreadSanitizer where the compiler has it
  CHECK: ./build/aeq_threads
  EXPECT: 2 passed, 0 failed

- [x] G23: the Arabic reaches the binary as Arabic - juce::String(const char*) reads its
  EVIDENCE: automatic-evidence=v1; definition-sha256=abb83d1626f6b8c7198bd63a6c8fa1bb31fd8666824677aa1a89939dc4eaee9b; exit=0; EXPECT=matched; output-sha256=7fd39dfde0cfc86d3a1872d301abdcadec2fa0e6a430842e0f45b38588276afd; output-bytes=128; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      bytes as ASCII, so a UTF-8 literal that goes through it becomes one character per
      byte ("تخنة" arrived as U+00D8 U+00AA U+00D8 U+00AE and painted as .notdef boxes while
      the Latin in the same sentence rendered).  Every string now goes through ui::u8();
      this checks the compiled binary on the runner that builds it, and fails if the
      literal and a \uXXXX escape disagree, or if the embedded typeface has no glyph for
      U+062A
  CHECK: python3 analysis/scripts/check_utf8_literals.py
  EXPECT: 0 non-ASCII literal(s) bypassing the UTF-8 funnel

- [x] G24: the window lays out its controls at the size it opens at - resized() runs
  EVIDENCE: automatic-evidence=v1; definition-sha256=2b2d8f4b9377d9d1e1806862d074056be987af48fecbe27cdc3f43030eb098ca; exit=0; EXPECT=matched; output-sha256=53ea1caea4022e5d15f31c2afdbfd1093cc47ae994ec7bdfd967f7a5a0042a25; output-bytes=109; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      inside setSize(), the constructor called setSize() before buildControls(), so the
      only layout pass ran over an empty control list and every knob, label and value
      stayed at 0x0.  The default size also came from nowhere: on a laptop the bottom row
      fell outside the host's window.  The gate asserts the knobs have a real height and
      the window fits inside 6/7 of the display it is opened on
  CHECK: python3 analysis/scripts/check_layout.py
  EXPECT: layout OK

- [x] G25: a host may use a block size it never announced - `scratch_` was sized from
  EVIDENCE: automatic-evidence=v1; definition-sha256=c0453e38c4c85bc965fd73fd9ddac443040700b367093fd9766ebc6373c018ba; exit=0; EXPECT=matched; output-sha256=ee3801b365ace52117bfb846b265d4b034d19c831e2b5008a7dc6dbb548c4fd3; output-bytes=921; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      the block size prepareToPlay() was given, and FL Studio hands over bigger blocks,
      so the audio thread wrote past the end of a heap vector.  ASan in 1.2.2:
      "heap-buffer-overflow WRITE of size 4 ... RestoreCore.h:108"; in FL the heap came
      apart a second or two later and the whole host exited.  The buffer is now a fixed
      member std::array fed in <=2048-sample chunks, and this gate performs the things
      hosts do: an 8192-sample block after prepare(64), a block size that changes on
      every one of 270 callbacks, mono with a null right channel, four sample-rate
      changes mid-playback, reset() between blocks, 0/negative sample counts, a 4 Hz
      analysis thread running throughout, and a check that every delivered sample
      reached the analyser
  CHECK: ./build/aeq_bounds
  EXPECT: 8 passed, 0 failed

- [x] G26: an Arabic paragraph is laid out right-to-left even when it starts with a
  EVIDENCE: automatic-evidence=v1; definition-sha256=b9b3653caf4a7807ab7707481e7cdce20eeacab67065d3261e9d248b6142c749; exit=0; EXPECT=matched; output-sha256=9933eae3a7f9e67b05e5cf6873ba69dacf1b45f3b08b7dd1f475d042444e0ab3; output-bytes=59; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      number - the base direction comes from the first strong character, so a card
      reading "202 Hz ثابت على مدى الأداء ..." was built as a left-to-right paragraph
      with Arabic runs inside it.  On the Windows runner that layout took the process
      down: rendering the cards' lines one at a time (probe-text-*.png) showed every
      paragraph beginning with a number dying and every one beginning with Arabic
      surviving.  `withRtlBase()` now puts a RIGHT-TO-LEFT MARK in front of such a
      string, in the three entry points of the text funnel, and this gate fails if a
      call disappears or if anything outside the funnel builds its own TextLayout
  CHECK: python3 analysis/scripts/check_bidi.py
  EXPECT: every paragraph goes through the RTL-base funnel

- [x] G27: the audio callback is smooth - "مش سلس بيقطع" turned out to be three things
  EVIDENCE: automatic-evidence=v1; definition-sha256=a234bc601c8b04b1fdd2e40be253352525eac2cdd9ba12b4f8d41881bf37c09c; exit=0; EXPECT=matched; output-sha256=924b0535611c0781140f62f98a42e899f94ef29ea3aab79843cbe957dfef47c2; output-bytes=521; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      in the callback, none of which a test that only checks "the output is finite"
      could see.  (a) The whole analysis ran inside processBlock: FFT-8192 + cepstrum +
      pitch tracker every 2048 samples, under a lock - 3.95 ms in a 10.7 ms budget in
      the released build.  (b) It allocated on the first frame (power_, frameBand_ and
      two thread_local vectors were sized lazily).  (c) setBands() cleared and
      re-filled a std::vector and replaced every biquad four times a second, so the
      filters' memory was zeroed at 4 Hz: a click, and an HPF that punched a hole in
      the low end every time it was retuned.  Analysis now runs off the audio thread
      through a lock-free ring, the callback only copies samples and runs biquads, the
      cascade is a fixed array crossfaded over 150 ms, and retuning keeps filter state
  CHECK: ./build/aeq_smooth
  EXPECT: 5 passed, 0 failed
  EVIDENCE_NOTE: measured, same test, same machine - released 1.2.4: 4 allocations
      inside processBlock, worst block 3.946 ms; this build: 0 allocations, worst block
      0.055 ms, worst 10 ms drop -1.34 dB, no sample-step discontinuity

- [x] G28: the window does no work when nothing changed - the editor polled at 12 Hz
  EVIDENCE: automatic-evidence=v1; definition-sha256=0d6368aa79d33bf472c18553f81f4cd1b253899ada6478c2ae285800162ec683; exit=0; EXPECT=matched; output-sha256=64df72a2d1d4fe76c5bfc71e1011459117deac1224f857e8ce40d89fa49a7133; output-bytes=63; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      while the solver publishes at 4 Hz, and every tick copied the published state, the
      measurement vectors, the band list and up to 24 problem texts, ran a z-scan over
      86 bands, rebuilt the report list and repainted the whole window: three quarters
      of the UI thread's work thrown away, which is what a sticky window feels like.
      RestoreCore::version() is bumped only when the published state changes and
      timerCallback() returns on its first line when it has not; the timer runs at 8 Hz
  CHECK: python3 analysis/scripts/check_ui_poll.py
  EXPECT: does no work when nothing changed

- [x] G29: it does the job on its own - the product is an automatic vocal EQ, so the
  EVIDENCE: automatic-evidence=v1; definition-sha256=f87f81ab65b7bb5927ea3e709fb1eee95fc594f2d13be23dd32d4df12510c405; exit=0; EXPECT=matched; output-sha256=6eb808abe73f5f9703c23c7310e186c17bb69dd3d16acff71b455f86c0d4b3a2; output-bytes=965; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      test that matters is not "a button works" but "nobody touched it and the take is
      fixed".  With no editor open and no clicks, the core is driven the way the
      processor's 4 Hz timer drives it: it settles by itself (8 s of sung audio), puts
      bands in the signal path, chooses a low cut, and keeps adapting after it has
      decided instead of freezing on its first guess.  Two voices an octave apart get
      two different low cuts, so it is not a preset in disguise
  CHECK: ./build/aeq_auto
  EXPECT: 12 passed, 0 failed

- [x] G30: the automatic behaviour cannot be quietly turned back into a manual one -
  EVIDENCE: automatic-evidence=v1; definition-sha256=1f5c48aef649ce9903aaa9de72c47ce84c6cc5bf5830b1ca9a8b8c15d30612a3; exit=0; EXPECT=matched; output-sha256=14bbf28c4b186c72479d523d1f6a84b8fabe3d384520a03a9e64448917e7c491; output-bytes=72; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
      learning starts enabled, bypass starts off, the settle time is a phrase and not
      half a minute (30 s was the first default and it read as "it does nothing"), the
      solving happens on the processor's timer so the plugin works with its window
      closed, and the editor's button is a readout rather than the thing that starts
      the analysis
  CHECK: python3 analysis/scripts/check_auto_defaults.py
  EXPECT: automatic: learning on load

- [x] G31: the plugin may not damage the voice - "بوّظ الصوت وأكل حاجات كتير".  A
  EVIDENCE: automatic-evidence=v1; definition-sha256=bf9b6deedd44fdbcf75b261e63e7d23bb2a0353c53327cca041b6d75dfcffb8b; exit=0; EXPECT=matched; output-sha256=49ef1499b79982e655509450bc0eddcc80dca222d635c93ddbdc70f1b4b187af; output-bytes=615; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  restore EQ that fixes the graph and thins the take has failed even when every
  individual band was inside its own limit.  On a synthetic 196 Hz vocal take with a
  known +6 dB sub rumble and a 240 Hz room bell, measured on the last four seconds
  (the correction in the signal path, settled):
  body 150-500 Hz still there (|change| <= 2.5 dB), level of the voice (90 Hz-12 kHz)
  unchanged (|change| <= 2 dB), air above 6 kHz untouched (|change| <= 2 dB), the low
  cut below the fundamental, no narrow cut on a harmonic of the sung pitch, and the
  rumble that was reported actually removed (>= 2 dB under 100 Hz).  The take's own
  RMS and peak are checked first: a test whose signal is broken measures the
  generator, not the plugin
  CHECK: ./build/aeq_damage
  EXPECT: 7 passed, 0 failed

- [x] G32: the analyser moves with the music - "كمية بطء في الأنالايزر رهيبة".  The
  EVIDENCE: automatic-evidence=v1; definition-sha256=53f0082c575e924f6d749e1ebac00a3d1f2e564f5bbd2a8a6c9aa565b3a17d98; exit=0; EXPECT=matched; output-sha256=41e10561b4083d2ca417c451df949b47c45e151ba5f4d14c07a8b4628e320e32; output-bytes=404; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  curve the window draws was the mean of every frame since load, so after a few
  seconds it was a constant; the display stream is now a separate 30 ms attack /
  120 ms release spectrum published by the processor's 30 Hz timer (>= 20 updates per
  second of audio), it falls at least 8 dB within 250 ms of a phrase ending, and it is
  back within 6 dB 100 ms after the next one starts
  CHECK: ./build/aeq_live
  EXPECT: 4 passed, 0 failed

- [x] G43: the panel is readable, not just dark - every text/background pair we
  EVIDENCE: automatic-evidence=v1; definition-sha256=b454fc1feb6e6d3086f02194730f684738d65987301f20eaa80deca27a6bb6f8; exit=0; EXPECT=matched; output-sha256=50c76e94a73bdff0860168ffcce8c0aa6c3024a13a3516af14dd9ef99740b985; output-bytes=421; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  actually draw is measured against its own surface, and the numbers come from the
  same file that renders the picture (design/preview/ui_v4.py), so the palette cannot
  drift away from the gate.  Body text >= 4.5:1, decorative text >= 3:1.  The first
  render failed it: the caption colour was 3.4:1 and the faint one 2.82:1.
  CHECK: python3 design/preview/check_contrast.py
  EXPECT: 0 failing

- [x] G44: learn the whole take, the user stops it, and it is frozen - "الأول يعمل ليرن
  EVIDENCE: automatic-evidence=v1; definition-sha256=cecc1dff4abc4005cf0c9550dd6965ba8d1e909b3be43cdb5bdece33bbb52068; exit=0; EXPECT=matched; output-sha256=52dffa7bba4898e83e464674d87f38e63151aff87e3dbc023b7f76ff5ffddc98; output-bytes=926; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  للصوت كله وبعدين أوقفه أنا".  Four promises, each measured on a 40 s synthetic vocal
  take: (1) while learning the output is **bit-identical** to the input (the chain is not
  executed at all); (2) pressing stop applies the decision inside 250 ms and the
  transition is a crossfade, not a step (worst adjacent-sample step no larger than the
  take's own); (3) ten more seconds of the same take change no band, no Q and not the low
  cut, while the meter keeps counting; (4) "learn again" returns to untouched audio and
  computes the next commit from the new take only.  Plus: the same take twice commits to
  the same decision.
  CHECK: ./build/aeq_commit
  EXPECT: 13 passed, 0 failed

- [x] G45: the band that locks onto the note and follows it - "باند يمشي مع النغمة
  EVIDENCE: automatic-evidence=v1; definition-sha256=6d61a6621c754db07a443b5b00d5cbec91c897ba2b34f9760cc9266fb5eb8ffa; exit=0; EXPECT=matched; output-sha256=54e52338281ee0b10fc4e0ec887e2bb2c983e36106d6439ce21daeb9d5a5a84d; output-bytes=1785; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  ويثبتها على مستوى" (the one the user called the most important of the four plugins).
  Measured on a synthetic sung note whose f0 glides 110 -> 220 Hz: the centre stays
  within 36 cents of the truth, the worst sample-to-sample step in the output is no
  larger than the signal's own (a tracked band that re-tunes on unchanged filter state
  is the classic "watery" sound), a one-frame octave report from the pitch tracker moves
  the centre by 0 cents (limit 60) while a real octave change is followed in 160 ms, a
  +12 dB note is held within 0.1 dB of where it was and settles in 16 ms, a 3 dB range
  corrects exactly 3.0 dB, and unvoiced audio returns the band to 0 dB.
  And the loudness link from the corpus: a band told 1.0 leaves a louder note alone (0.00 dB),
  a band told 0.0 holds it, and the difference between the corpus's 0.19 and a plain hold is
  the 1.9 dB the slope predicts over a +10 dB note.
  CHECK: ./build/aeq_tracked
  EXPECT: 27 passed, 0 failed
- [x] G46: the core's dynamic layers, end to end.  After the user stops the pass, every
  EVIDENCE: automatic-evidence=v1; definition-sha256=dd225695d0050559573f90c16d1bae50daf543346b8fea4d98cd46605c95dddb; exit=0; EXPECT=matched; output-sha256=be1507ff277df9907b117084297d648c0616aa6c6b10cdc621b58b3b7616be7a; output-bytes=1331; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  learned cut that sits on a harmonic of the measured f0 becomes a band that follows that
  harmonic (never more than two, never boosting), and it does the job a static curve
  cannot: the same performance sung 10 dB louder gets its 3rd harmonic pulled back to
  within 2 dB of where the quiet take sat (0.36 dB measured with the range to do it),
  while the static chain alone leaves it 10 dB out.  The take it was learned from is only
  touched on its loudest frames, the output stays finite and click-free, and a project
  saved without layers restores with none.
  CHECK: ./build/aeq_layers
  EXPECT: 13 passed, 0 failed

- [x] G47: the corpus is in the DSP, and the DSP did not type it.  The 15 dry stems (49.0
  EVIDENCE: automatic-evidence=v1; definition-sha256=6a8f7f5eb0d4141a6884f4f10d2e044fced978a7c15188cfce73963bb6f60c15; exit=0; EXPECT=matched; output-sha256=b526f29c6774646a42a68f0ca61b4c936b0606c59b15e07792ebc580c688c723; output-bytes=71; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  minutes, 4084 sung notes) were measured for the one thing that repeats between them:
  not the level (0 of 24 bands agree within 3 dB across songs, while a band moves 3.4-6.0x
  more within a single take) and not the shape of an attack (0.79 similarity against a
  0.75 chance floor), but the profile of how many dB each band moves per dB the voice gets
  louder - 0.15-0.34 below 160 Hz, 1.0-1.4 in the mids, 0.6-0.8 above 5 kHz.  Fifteen songs
  agree on it with a leave-one-out correlation of 0.92 against 0.00 for each song's band
  axis shuffled on its own (300 draws).  The plugin reads that table from
  plugin/Source/dsp/DynamicProfile.h, which is generated from the measurement, and this
  gate fails if the file and the measurement ever disagree.
  CHECK: /tmp/venv/bin/python analysis/scripts/emit_dynamic_profile.py --check
  EXPECT: DynamicProfile.h matches the corpus (24 bands, slope correlation 0.92)

- [x] G48: the panel is In The Mix's grammar, and it holds together.  The v4 render was
  EVIDENCE: automatic-evidence=v1; definition-sha256=9dc3b3eda2eeda8b96e34bdc88fd8dbc515037726d59fb2ce6042f57a239e142; exit=0; EXPECT=matched; output-sha256=481dfda22fd0d597c1fdfeb0ec36042587a01bfdacd3a77361244c34eee06515; output-bytes=109; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  rejected ("مش عاجبني") because it was one engraved plate with six knobs in a row, not
  what the store actually ships.  v5 copied the reference's *structure* - header bar with a
  centred state pill and round icon buttons, one glass well with the frequency ruler along
  its top edge and a spiky FFT behind both an amber correction curve and a blue analyser,
  four section cards (LEARN / CURVE / TRACK / OUTPUT) whose readouts are tinted LCDs, a
  bottom strip holding the low cut, the report line and the version - and v6 keeps that
  structure and replaces the parts.  Three things about the layout are checked rather than
  eyeballed: every Arabic string the module actually draws is fully covered by the bundled
  Cairo (the per-glyph advance error is what made the words collide, and one glyph loop is
  why every right-anchored line drifted off its card), the contrast floor still holds, and
  no two blocks of the layout overlap.  The strings are read out of ui_v6.ARABIC_STRINGS so
  the gate cannot pass on copy that has since changed.
  CHECK: /tmp/venv/bin/python design/preview/check_panel.py --claim=blocks
  EXPECT: panel ok: 21 Arabic strings fully covered by the bundled font

- [x] G49: the panel is made of real parts, and the pointer answers the value.  v3, v4 and
  EVIDENCE: automatic-evidence=v1; definition-sha256=4ec8669e3d11580d8a7be98de61b5b07f9d0cdb968a3e7e2db5fe54ede19043b; exit=0; EXPECT=matched; output-sha256=5505d0eb9b32798206c9e6d62ea5d1414206cf1cc8132be85ce7f84a116ae33b; output-bytes=120; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  v5 were all drawn with PIL primitives, and the verdict on all three was the same: flat.
  A machined knob is a specular surface - a knurled ring catching light, an anodised bevel,
  radial brushing - and no amount of rounded rectangles produces it.  So the parts are now
  rendered images (design/assets/src: six 800 px material renders) cut into sprites by
  design/assets_prep.py (alpha from luminance, tight bbox, 1.6 px edge blur) and composited:
  the knob body carries its own highlights, the button has two real states, and the plate is
  the graded carbon.  Only the things that must change with the DSP are drawn in code - the
  needle, the lit glow, the curve, the spectrum.  The claim is measured, not asserted: each
  sprite must be a real cut-out holding >= 200 distinct colours, the knob face in the
  shipped PNG must hold >= 300 (a drawn disc has a dozen), and the needle's bright pixels
  are located for five values and compared against -135 deg at 0 and +135 deg at 1: the
  worst error is 2.8 deg over 267 deg of travel.
  CHECK: /tmp/venv/bin/python design/preview/check_panel.py --claim=material
  EXPECT: material ok: 3 material sprites in the composite
- [x] G50: the panel is *this* product's, not the reference store's.  v5 and v6 were a
  EVIDENCE: automatic-evidence=v1; definition-sha256=b7bb13a1cd9854f3eb52b7380760c1e9dea8ad0d78abee86aa6ce9c741109852; exit=0; EXPECT=matched; output-sha256=e88ab89ee3ecb1d177decdce9fc734c403d51f1d12912f966d69bf7611408477; output-bytes=488; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  copy and the user said so in one line: "كنت اقصد الهوية البصرية و التحضر في التصميم مش
  اقصد نسخة" - the identity and the craft, not a reproduction.  A copy is measurable.
  Three of the reference's cues are pixel tests: a **centred pill** (nothing may be drawn
  within 60 px of the panel's horizontal centre in the header band), an **equal card grid**
  (no two layout blocks may share a height - the identity's blocks are 42/100/254/134/76),
  and a **third accent** (the saturated pixels must cluster into exactly two hues; this
  world is brass ~30 deg = a decision and patina ~150 deg = a measurement, where the
  reference is blue + amber).  The same two pixel measures are also run against the
  previous render (`--against-v6`) and are required to FAIL there - 237 px drawn in the
  header centre, a third accent at 225 deg - so the gate is shown to discriminate instead
  of agreeing with whatever is on screen.  What the panel must carry is also measured: a
  take bar sealed somewhere inside it with the machined clamp on the seal, the learned
  mass at 64 % patina and 2455 brass px of curve over the spectrum, and a ledger holding
  static + dynamic + tracked bands at once (2/1/1).  The world itself is written down in
  design/IDENTITY_AR.md - brass = a decision, patina = a measurement - and the copy is
  read out of the module, so this cannot drift.
  CHECK: /tmp/venv/bin/python design/preview/check_identity.py --against-v6
  EXPECT: identity ok: 21 Arabic strings covered

- [x] G51: the craft floor is measured, not admired.  Two skill sets were read for the UI
  EVIDENCE: automatic-evidence=v1; definition-sha256=3420749ff424423175ab8281cea0a23abd14d7f4571cdd7723837d29f1350763; exit=0; EXPECT=matched; output-sha256=30a5f2c329806309302878b84377a1add7834061016ecba1f3f9d3f690147c82; output-bytes=269; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  and both are checklists, so neither is worth anything unless something runs it.  The
  gate reads the real panel and fails on: a target under 40x40 logical px (or a row under
  30 px tall), two targets overlapping, a corner radius outside the declared set (18 for
  the frame, 12 for the well recessed into it), a knob seat that is not concentric
  (1.08x), a live number drawn proportionally instead of through the tracked readout, a
  text size outside the declared scale, a product motion off its token or on a bounce
  curve, animation of something the hand touches, and a control with no declared
  hover/press/disabled state.  The first version of this gate passed three of those
  checks by matching nothing at all - a regex that found no type sizes "agrees" with the
  scale - so the panel now records every call it makes and the gate asks the render
  (`ui_v7.report()`): 14 tracked readouts, sizes 8/10/12/14, 7 targets, 7 parts x 4
  states.  It also caught a real one: the panel had six type sizes (8,9,10,11,12,13),
  which is what "assembled" looks like.
  CHECK: /tmp/venv/bin/python design/preview/check_craft.py
  EXPECT: craft ok: 7 targets >= 40 px and none overlapping

- [x] G52: every motion is on a token, and the spec compiles.  transitions.dev supplies a
  EVIDENCE: automatic-evidence=v1; definition-sha256=1b426ecb769f46a55a0bc187361e5a1a0502cdca43c78ba81ae24006df642d0b; exit=0; EXPECT=matched; output-sha256=a67757ceab71de8d8a47f699fc2cc673b199001d2afa803b1d5b8ca93b2f8a28; output-bytes=199; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  motion token scale and gsap-skills supplies the rule underneath it (animate transform
  and opacity, never layout, never a thing being dragged).  Both are now a header the
  plugin owns: plugin/Source/ui/MotionSpec.h declares 7 durations, 6 easings and 4
  distances that must equal the recorded table to the last decimal, the product's three
  motions (clamp-drop 250 ms smooth-out 8 px, seal-word 150 ms in-place, seal-led colour
  only) may only name tokens and may never use a bounce curve, and the header compiles
  standalone under -Wall -Wextra -Werror with a program that proves the curve eases:
  at(0)=0, at(1)=1, monotonic, at(.5)=0.961.  Bounce is banned in product motion
  (make-interfaces-feel-better rule 7) and the ban is a flag, not a comment.
  CHECK: /tmp/venv/bin/python design/preview/check_motion.py
  EXPECT: motion ok: 7 durations + 6 easings + 4 distances match the token table

- [x] G53: the dependency arrows only point down.  Graphify's premise - see the graph
  EVIDENCE: automatic-evidence=v1; definition-sha256=8bf652752cc8033a00d9487b2082637c2162bbf11e9240db033531bbda346402; exit=0; EXPECT=matched; output-sha256=ad1cac9037ec67b4a3e4ccbaf956235c57623e799ad4f7c0f2aa110327c8439b; output-bytes=110; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  before adding to it - applied to 21 files instead of a knowledge graph: the include
  graph of plugin/Source is built from its own `#include "..."` lines and must have zero
  cycles, zero upward edges (model <- dsp <- ui <- the JUCE wiring) and zero orphan
  headers.  This found a real disagreement between the documentation and the code: dsp/
  includes model/ in five places, and the docs claimed the audio layer was the bottom of
  the stack.  The code was right - model/ is the measurement data, pure and JUCE-free -
  so the declared stack was corrected rather than the code: model 0, dsp 1, ui 2.  The
  result is 21 files, 29 edges, 0 cycles, 0 upward edges, by layer {0:3, 1:8, 2:6, 3:4}.
  CHECK: /tmp/venv/bin/python analysis/scripts/check_layers.py
  EXPECT: layers ok: 21 files, 29 include edges, 0 cycles

- [x] G54: the two attack surfaces are attacked.  Strix's method is the first paragraph of
  EVIDENCE: automatic-evidence=v1; definition-sha256=9e47622065e90a4018f9a953428a5bbce551a3efa471ae0b3f3bf1827524a1d7; exit=0; EXPECT=matched; output-sha256=cb5d0cff34e6bddeac4ba9688841190a08f19329706cc3112e94a1f90e16ce8b; output-bytes=249; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  the security skill it ships with: name the trust boundaries, then prove the controls.
  This project has two.  (A) The preview server serves the *repository root* - it must,
  because the pages reference the embedded font by root-absolute URL - so the boundary is
  the URL path, and the gate starts the real server on an ephemeral port and sends six
  attacks (plain and encoded traversal, /.git/config, /.git/, CI config, the audio
  corpus).  Every deny target that actually exists on disk must answer 403, not 404: a
  404 would be luck, not a control.  The three preview URLs must still be 200 with
  nosniff.  (B) The release scripts: 46 Python files with no shell=True and no string
  commands, the installer's -Version validated against a semver regex before it is
  interpolated into a path, the uninstaller checked for wildcards, and the code-signing
  timestamp now fetched over TLS - an RFC3161 reply from an on-path attacker is a
  timestamp that attests to nothing.  Two real defects were fixed for this gate and one
  is still open and named in docs/21.
  CHECK: /tmp/venv/bin/python design/preview/check_hardening.py
  EXPECT: hardening ok: 50 Python files

- [x] G55: the UI compiles against the JUCE we pin, not the JUCE in my memory.  This is
  EVIDENCE: automatic-evidence=v1; definition-sha256=799b1f162222d5b617140ca7d278c096ba26ecd572bfe070e2d2ede02afc44dc; exit=0; EXPECT=matched; output-sha256=7b54df4b521ce3304cf6db06f9bc5bfa35b95447228e9eb85d75baa5dd0ecf29; output-bytes=127; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  context7's premise made mechanical: a model writes API calls from memory, the memory
  lags a version, and the build fails six minutes later on a Windows runner.  The gate
  reads the GIT_TAG out of plugin/CMakeLists.txt (so bumping JUCE cannot leave it
  checking an older one), refuses to run unless the checkout on disk is at that exact
  version, and then compiles every UI header with -fsyntax-only -Wall -Wextra against it
  with the two stubs CMake would have generated.  Before this gate, "the UI compiles" was
  a claim from a previous session with no checkout on disk to support it; now it is
  JUCE 8.0.4, five headers, clean.
  CHECK: /tmp/venv/bin/python design/preview/check_juce_api.py
  EXPECT: juce ok: JUCE 8.0.4 pinned in CMake and present on disk

- [x] G56: the panel is contemporary, and "modern" is four measurements instead of an opinion
  EVIDENCE: automatic-evidence=v1; definition-sha256=4b921c60db6c14045107036dbaabbc1e774eac8c64bf4d1c3e7fd248aa22acda; exit=0; EXPECT=matched; output-sha256=e5300dd83917aee8d39b74979e9bce1777ff3359a1a2ca8a35b202daf5d49507; output-bytes=216; shell=/bin/sh; cwd=/home/user/AI-EQ-PLUGIN-VST; path=b51a5b4db45a/5 entries
  the rejection was "التصميم حاسه بدائي اوي لسة مش عصري" - primitive, not modern.  Forty
  shipping panels were collected and four read closely (Morph EQ, Smooth Operator Pro,
  soothe 3, Ozone 12); they agree on four structural things, and each one is a number.
  (1) The graph is the instrument: v7 gave it 36 % of the height and stacked four
  equal-weight bands under it, v8 gives it 69 % and it is the largest block on the panel
  at 67 % of the area.  (2) Depth is light, not texture: v7 composited a carbon-fabric
  plate and its bare margins carry 420-807 distinct colours at 1.9-3.2 units of
  high-frequency energy; v8's carry 42-109 colours at 0.21-0.36, so the gate puts the
  ceiling at 220 colours and 0.8 hf and both sides clear it by 2x.  (3) The ledger is a
  row, not a table: four blocks, no two sharing a height, and a header that cannot grow
  into a toolbar.  (4) A plate is refused by name in the code that draws, so a revert is caught in the
  source as well as in the pixels.  (5) THE METAL IS THE WORLD'S METAL: the knob sprites
  were brass, and brass in a magenta panel is a third accent arriving through a material
  rather than through a colour choice - so the knob face is measured and has to sit
  within 30 deg of the declared accent (it reads 335 deg against 332).  The palette itself
  is four worlds in one module (neon is the default, plus sunrise, dawn and citrus), and
  the accent-hue assertions in check_identity now read ACCENTS from the module instead of
  two hues that happened to ship, so changing the mood cannot silently disable the test.  `--against-v7` runs the pixel measures
  against the panel this one replaced and fails every margin, which is what makes the
  pass mean something.
  CHECK: /tmp/venv/bin/python design/preview/check_modern.py
  EXPECT: modern ok: 4 blocks, no shared height

- [x] G57: the field was lifted, and the two accent tests moved with it instead of
  being switched off.  "ألوان مبهجة أكتر من كدة حاسه قديم اوي" is a complaint about
  value, not about hue: the neon world's hues were already right, the ground was too
  dark, and a black inset well in a violet panel read as a hole.  The ink went from
  `#181224`/`#08070E` to `#34214A`/`#110C1E`, the well from a neutral `#0A0A0C` to the
  world's own dark `#1D132D`, and the ambient blooms from 28/28 to 44/40.  Body contrast
  drops from 7.7:1 to 5.9:1 and clears the 4.5:1 floor.
  Three gate corrections came out of doing it honestly, and each is a rule about what an
  accent *is* rather than a ceiling moved out of the way:
  (1) the hue count runs on the panel minus a ground-only render, so the room's light
  cannot vote; (2) an accent must be bright ink, so the value floor is 150 - the plate
  and the well are saturated too; (3) an accent must be an area, so the floor is 5 % of
  the drawn ink, where 2 % was letting violet fringes between plate, glow and curve pass
  as a third colour.  The margin colour bound moves 600 -> 900 because a brighter field
  legitimately holds more distinct colours; the texture ceiling stays at 0.8 hf, and the
  field measures 0.32-0.68 against v7's fabric at 1.9-3.2.
  CHECK: /tmp/venv/bin/python design/preview/check_identity.py
  EXPECT: identity ok: 21 Arabic strings covered

- [x] G58: the picture that was approved is the picture that runs.  The panel was
  approved as a render drawn in Python while the window is drawn in JUCE, and for four
  versions the two had nothing to do with each other (the window was still v6 under a
  v8 render).  `check_match.py` compares the CI screenshot of the real editor with the
  render, and it compares the things that are *decisions* rather than pixels: the block
  edges are found the same way in both images (strong horizontal gradient across the
  middle of the width, so rounded corners cannot vote), the six full-width edges have to
  land within 3 px of each other, the graph has to own at least 55 % of the panel, and
  the colour has to be the declared palette in *both* (a v8 brass render shares v9's
  geometry and would otherwise pass).  On the shipped pair: `match ok: 6 edges agree
  within 2 px (graph 70-556, strip 496-522, chips 572-636); graph 69 % of the panel;
  22108 px in the declared hues`.  `--against-v8` fails the palette, and comparing with
  v7 fails six edges, which is what makes the pass mean something.
  CHECK: /tmp/venv/bin/python design/preview/check_match.py
  EXPECT: match ok: 6 edges agree within 2 px

- [x] G59: all English, and a type scale that is actually readable.  Two asks in one
  round: "خليه كله english مش عربي" and "خليه اكبر ب كتير".  The panel's copy is now
  Latin end to end - wordmark, tagline, seal state and its one-line claim, the five
  verdict sentences, `STATIC`/`DYNAMIC`/`TRACKED`, `measured`/`decision`,
  `RESTORE`/`MATCH`, `strength`/`listen`, the heard line (`HEARD 24.9 S · 96 BANDS ·
  15 REFERENCE TAKES`) and the empty state (`NO MEASUREMENT YET - PLAY THE TAKE FROM
  THE TOP`).  Arabic survives in the docs and nowhere on the panel; `PANEL_STRINGS` is
  the inventory the font gates cover, and `ui_v9.py` asserts that `DRAWN["arabic"]` is
  empty, so a stray Arabic string fails the render rather than shipping.
  The scale went `8/10/12/15 -> 11/13/16/21` (micro/cap/body/value), which is x1.375 on
  the caps rows and x1.4 on the readouts, in both the Python render and the JUCE editor.
  Bigger type alone would have broken the panel, so the containers moved instead: the
  band chip keeps its 64 px row but drops from three stacked rows to two - the number
  owns the left of the second row, its spec (`118 Hz · Q 0.82`) owns the right, `dB`
  rides the number's baseline, and the tracked chip's level meter sits on the floor of
  the chip; the stage pill grows 66 -> 90 px per segment for a 13 px word; the footer
  knob cluster goes 104 -> 128 px of pitch so an 11 px `strength` clears the knob beside
  it; the graph keys put a dot in front of each word instead of behind it; and
  `STOPPED BY YOU` gets a backing plate because it is now 11 px of tracked caps sitting
  over whatever the plot draws.
  The six matched edges are unchanged (graph 71-556, strip 496-522, chips 572-636), so
  the render still matches the shipped window - the type grew, the frame did not.
  CHECK: /tmp/venv/bin/python design/preview/check_identity.py && /tmp/venv/bin/python design/preview/check_match.py
  EXPECT: identity ok: 20 panel strings covered ... / match ok: 6 edges agree within 2 px

- [x] G60: the controls are a state machine, not a row of buttons.  Seven targets is
  right for a tool whose thesis is "it decides, you stop it" - what was wrong was the
  logic behind three of them and the drawing behind two.
  *The dials.*  `drawRotarySlider` was handed `s.getValue()` - the value in parameter
  units - where a turn in 0..1 was expected, so 100 % of "strength" became
  -135 + 270 x 100 = 26 865 degrees and both knobs span like a slot machine on the way
  to their value.  They took proportions now (`valueToProportionOfLength`, which is also
  the only version that respects a skewed range).  The value ring is drawn at 1.19 x the
  body radius and a child is clipped to its own bounds, so at a 42 px box the ends of
  every ring were cut off; the box is 54 px and the body 42 inside it.
  *The stop.*  The plugin stopped itself: `settle_s` (default 45) froze the decision
  without anybody pressing anything, and the panel then said "STOPPED BY YOU" and
  "nothing moves on its own" over a decision the user had not made.  Readiness and
  stopping are two questions now: `settleSeconds` only sets `state().settled`, and a new
  `autoStopSeconds` - 0 by default, and what the second knob drives - is the only thing
  that can end a pass by itself.  When it does, the seal says `STOPPED AT THE CAP`.
  *The reversibility.*  The clamp called `relearn()` when sealed: one accidental click
  after a good take threw the whole pass away.  It calls `reopen()` now - every sample
  and every band stay, the correction keeps playing, the analysis keeps running, and the
  next stop solves from the longer take.  `relearn()` is the reset icon's job.
  *The veto.*  Right-click a band chip and it hands that band back (`trim = -gain`, the
  same offset a drag writes); double click still restores the solver's answer.
  *The number.*  The level match runs by itself and was invisible; the caption now
  carries `AUTO GAIN -1.8 DB` when a decision is in the path.
  Measured, not asserted: `check_knobs.py` reads the shipped screenshot and asks how much
  of each value ring is lit.  On the 1.2.12 window it said `strength 47 % of the ring
  (expected 100 %)` - the bug, from one number.  What that run did *not* prove is that the
  gate was reading the dial: see G61, where the ring was found 22 px from where the layout
  puts it and the gate was passing its own mistake along.  `check_juce_api.py` also refuses `getValue()` in
  `drawRotarySlider` and a knob box under 50 px, and the rule was falsified before it was
  trusted: putting the old line back turns it red.
  CHECK: /tmp/venv/bin/python design/preview/check_knobs.py && /tmp/venv/bin/python design/preview/check_juce_api.py
  EXPECT: knobs ok: strength 100% of the ring

- [x] G61: the ring was drawn backwards, and the second dial was driving the wrong
  parameter.
  *The arc.*  `addCentredArc (..., a, 0.0f)` runs from the value to 12 o'clock - the wrong
  way round and with a length that has nothing to do with the number.  The lit part is
  `-135` to `a = -135 + 270 p` now.  The specular cap sat at `1.12 pi .. 1.88 pi`, which is
  the *bottom* of every knob; it is centred on `+-0.34 pi`, the side the light is on.
  *The binding.*  The dial labelled "auto-stop" was still attached to `settle_s`, so the
  shot read `45 s` under a knob whose label says `auto-stop`, and its arc measured 17 % of
  the travel where the parameter says 0.  The attachment is `auto_stop`.
  *The gate.*  `check_knobs.py` was measuring `W - 158` - the design width without the
  22 px margin - so on a correct 100 % ring it read 47 % and called it a drawing bug.  It
  now reads the two centres from the editor's own layout numbers (`footer.right - 158` and
  `- 30`, the footer row), and when a ring is missing it searches +-16 px and reports where
  the ring actually is instead of guessing at a cause.  It also self-tests first: it draws
  rings at 0 %, 25 % and 100 % and refuses to judge a screenshot until it can read its own
  drawing.  The 25 % case is the one that catches an angle-convention error - at 0 % and
  100 % a 90 degree offset still reads correctly, which is exactly how the first version of
  this gate passed while the bug was in the build.  Falsified twice: drawn from PIL's 225
  (the end of the travel) it reads `58 %`, and on the 1.2.15 shot it reads
  `auto-stop 17 %` and names the binding.
  CHECK: /tmp/venv/bin/python design/preview/check_knobs.py
  EXPECT: knobs ok: strength 100% of the ring (expected 100%); auto-stop 0% of the ring
  (expected 0%)

- [x] G62: the panel may tell one story at a time.
  The 1.2.16 window shipped three on one frame: `SEALED` in the header, `STOPPED BY YOU` on
  the take strip, and "Measuring every second against the 15 reference takes. The verdict
  lands when it settles." in the footer.  The cause was ordering - the verdict asked "has
  it settled?" before "is it sealed?", and `settled` means "enough audio to trust the
  numbers", not "the pass is closed", so a take stopped at 21 s of a 45 s pass was sealed
  *and* still described as measuring.
  The sentences moved to `plugin/Source/ui/PanelCopy.h`: a `Facts` struct (have data,
  frozen, committed, settled, bands, heard s, trust s) and three functions - `verdict`,
  `stateWord`, `stateSub` - so the pill, its sub-line and the footer cannot answer the same
  question differently.  Sealed beats measured, and a take sealed before the trust
  threshold says so with both numbers: "Sealed at 3 bands on 24 s of a 45 s pass - the
  numbers are coarse, the decision is not."
  The file is plain C++ (no JUCE) so the gate that builds without a GUI links it:
  `test_commit` 6a-6f, 6 assertions, including the invariant "every one of the 8 states
  says exactly one thing and the pill agrees with it".  Falsified: putting the old ordering
  back turns 6b, 6c and 6f red.
  CHECK: build/test_commit
  EXPECT: 24 passed, 0 failed

- [x] G63: the editor may not write to a filter, and ThreadSanitizer is what says so.
  `RestoreCore::reset()` rebuilt the whole cascade - `eqLeft_.reset()`, `hpf_.reset()`,
  `analyzer_.reset()`, `state_` - and the Reset icon calls it from the message thread while
  the audio thread is inside `processBlock`.  The same for `restoreDecision()`, i.e. every
  project load.  This is the shape of the 1.2.2 crash that took the plugin out of FL
  Studio, and the file even had a comment forbidding it for trims.
  Measured first: a ThreadSanitizer probe (audio thread processing, host thread calling
  `relearn()` and `restoreDecision()`) reported
  `data race ... reset() ... processBlock()`.  After the fix: zero warnings, three runs.
  The fix is not a mutex around the cascade - that would block the audio thread.  It is a
  request: `reset()` sets two flags, `processBlock` performs the DSP half (`resetDspNow`),
  `refresh` performs the analysis half (`resetAnalysisNow`), and both locks inside are
  `try_to_lock` with a retry on the next block.  `restoreDecision`, `setSettings` and
  `prepare` route through the same `bandsDirty_` flag; `applyGain_` is no longer written
  from the message thread at all.
  `test_threads` now runs the two panel actions inside its editor loop, and the
  falsification is recorded: putting `resetDspNow()` back inline turns TSAN red at
  `BiquadCascade::reset()`.
  CHECK: g++ -fsanitize=thread plugin/Tests/test_threads.cpp && build/test_threads
  EXPECT: 3 passed, 0 failed

- [x] G64: a control that answers without saying so is a control that does nothing.
  "المفاتيح مش بتعمل حاجة و مش بتديني حاجة تاني" - and the panel had exactly that: Escape
  opened a sealed take and changed no pixel, and nothing in the build pressed it.  Space was
  tested (1.2.20), Escape and the wheel were not, and neither was the graph itself, which is
  two thirds of the window.
  The control stage now drives the real editor and asserts, in this order: every control is
  reachable through `controlAt()`; the clamp seals and re-opens; a re-open starts the
  analysis again; bypass round-trips; reset after a stop returns to listening and clears the
  counter the panel reads; both dials follow their own parameters; a chip trims its band and
  gives it back; Space stops the pass; the project survives `getStateInformation` /
  `setStateInformation` with the same number of bands on the same frequencies; Escape is
  handled, opens the take, is idempotent, says "Listening" when it does it, and leaves an
  unclaimed key alone; the wheel moves the dial under it, upward is more, and the same wheel
  over the bare field changes nothing; a drag on a curve handle lifts that band, a
  double-click gives the band back to the solver, and a drag on the bare field moves no band.
  Two real defects came out of it on the first two runs - the counter survived a reset, and
  the resonances were never written to the project, so a reopened session came back with
  fewer bands than the one that was closed.
  CHECK: aeq_shot controls (Windows CI, `ci/ui:controls.log.txt`)
  EXPECT: 39 passed, 0 failed

- [x] G65: the smallest type on the panel is a number, and it is 14 px.
  "الخطوط مش واضحة اللي على الأزرار صغيرة و مش باينة" was answered in 1.2.18 with an
  11 px -> 12 px floor.  It was repeated ("لسة في مشاكل"), and 12 was still the floor.
  The scale moved a step - 15/17/20/26 px - and the floor moved with it to 14, so the
  smallest label in an 800 px-high window is 17 px on screen.  The header had to be rebuilt
  to survive that: its four lines hung off the header's centre, and at the new sizes the
  tagline landed on the wordmark.  Both rows are now placed from the header's top edge, and
  the header is 58 px with a 60 px ceiling instead of a 56 px one that was measured when the
  type was 18/15.
  CHECK: design/preview/check_juce_api.py
  EXPECT: juce ok: ... type 15/17/20/26 px, smallest at the floor
