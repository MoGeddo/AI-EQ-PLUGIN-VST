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
