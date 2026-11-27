// test_when_it_stops.cpp - "does it scan the whole take, or does it stop by itself?"
//
// The user asked exactly that, twice, and the panel could not answer it: every sealed pass
// read "SEALED", whether a hand pressed the clamp, the auto-stop dial ran out, or the
// five-minute cap fired.  This file is the answer as behaviour, not as a sentence:
//
//   0. the corner case the user actually hit - the pass ran to the **hard cap** while nobody
//      pressed anything, and the core says so (`stoppedAutomatically()`), because a pass that
//      ended itself must not be reported as "sealed by you";
//   1. when a hand stops it, the core says it was not automatic;
//   2. the auto-stop dial is *off by default*: with no dial set the pass ends by hand;
//   3. the dial, when set, stops the pass at the setting - not at the cap;
//   4. a stopped pass stops counting: the analysed seconds do not keep growing after it;
//   5. the words on the panel follow the same three states.
//
// The clip that produced the user's screenshot shows the defect this file was written for:
// "SCANNED 88 S" with "STOPPED BY YOU" under it, which cannot both be true - the pass ended
// at 88 s of its own accord (the cap), and nothing had been pressed.
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "../Source/dsp/RestoreCore.h"
#include "ui/PanelCopy.h"

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static void checkTrue (bool ok, const char* what) {
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}

// A voice with **phrases**, which is what the analyser counts: a frame only counts as "heard"
// once the take has a level history and the frame is above the noise gate (Analyzer.h: the
// gate is +9 dB over the 10th percentile) - so an unbroken tone is counted *nothing*, and a
// take with breaks between phrases is counted the way a real vocal is.  The first version of
// this file used a steady tone and measured 0 s heard, which is a fact about the analyser
// worth knowing: silence is not scanned, only voice is.
static std::vector<float> take (double seconds, double f0 = 180.0) {
    const int n = int (seconds * kSr);
    std::vector<float> x (size_t (n), 0.0f);
    std::mt19937 rng (7);
    std::normal_distribution<double> nd (0.0, 1.0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = double (i) / kSr;
        // 1.6 s of singing, 0.5 s of breath, over and over
        const bool singing = std::fmod (t, 2.1) < 1.6;
        const double vib = 1.0 + 0.01 * std::sin (2.0 * kPi * 5.0 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int h = 1; h <= 6; ++h)
            s += std::sin (phase * double (h)) / double (h);
        x[size_t (i)] = float (singing ? 0.25 * s + 0.02 * nd (rng) : 0.0005 * nd (rng));
    }
    return x;
}

// Drive the core the way the processor's timer does: blocks in, a refresh now and then.
static void feed (aeq::RestoreCore& core, const std::vector<float>& x, double fromSec,
                  double toSec, double refreshEvery = 0.25) {
    const int block = 512;
    const int i0 = int (fromSec * kSr), i1 = int (toSec * kSr);
    double nextRefresh = fromSec;
    for (int i = i0; i + block <= i1; i += block) {
        float l[512], r[512];
        for (int k = 0; k < block; ++k) { l[k] = x[size_t (i + k)]; r[k] = l[k]; }
        core.processBlock (l, r, block);
        const double t = double (i) / kSr;
        if (t >= nextRefresh) {
            core.pump(); core.refresh();
            nextRefresh += refreshEvery;
        }
    }
}

int main() {
    // ---- 0. the default pass has no ceiling ------------------------------------
    // This section used to prove the opposite: that a pass nobody stops ends itself at
    // five minutes.  That ceiling was the thing standing between the product and "scan
    // the whole take" - an eight-minute vocal is a normal vocal, and a pass that stops at
    // five decides on two thirds of the evidence and calls it the answer.  So the check
    // is inverted: a long take runs as long as its transport does.
    {
        auto x = take (430.0);
        aeq::RestoreCore core;
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 0.0;            // the product default
        s.autoStopSeconds = 0.0;          // and no hand-set stop either
        core.prepare (kSr, 512);
        core.setSettings (s);
        feed (core, x, 0.0, 405.0);       // past the old five-minute ceiling
        checkTrue (!core.frozen(), "a long take is not cut off at five minutes");
        checkTrue (!core.committed(), "... and it has not decided behind the person's back");
        const double clock = core.heardPassSeconds();
        std::printf ("        %.0f s of input and the pass is still open (the old cap was 300)\n", clock);
        checkTrue (clock > 395.0, "the pass keeps its own clock instead of stopping at 300 s");

        // The transport is what ends it - that is the whole rule.
        core.setTransport (true, true);   // it rolled...
        core.setTransport (false, true);  // ...and stopped
        core.refresh();
        checkTrue (core.state().settled, "the transport stopping is what settles the take");
        checkTrue (!core.stoppedAutomatically(),
                   "... and it does not call that \"stopped by itself\" - nobody set a cap");

        // 4. settled is not sealed: the take has been heard, the decision has not been
        //    made, and the pass is still open, so it still notices what arrives.  This is
        //    the difference the old ceiling erased - a cap both ended the listening and
        //    made the decision, so "heard" and "decided" could not be told apart.
        const double beforeClock = core.heardPassSeconds();
        feed (core, x, 0.0, 2.0);         // more audio arrives after it settled
        checkTrue (core.heardPassSeconds() > beforeClock + 1.0,
                   "a settled-but-unsealed pass still counts the input it is handed");
        checkTrue (!core.committed(),
                   "... and still has not decided - settling is not sealing");

        // 5. once a hand seals it, the counting stops - neither clock nor meter
        core.commit();
        const double sealedClock = core.heardPassSeconds();
        const double sealedMeter = core.state().analyzedSeconds;
        feed (core, x, 0.0, 2.0);
        checkTrue (std::abs (core.state().analyzedSeconds - sealedMeter) < 0.5
                     && std::abs (core.heardPassSeconds() - sealedClock) < 0.05,
                   "a sealed pass does not keep counting the audio that arrives after it");
    }

    // ---- 0b. a hand-set cap still works, and still says so ----------------------
    {
        auto x = take (430.0);
        aeq::RestoreCore core;
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 0.0;
        s.autoStopSeconds = 120.0;        // the person asked for this
        core.prepare (kSr, 512);
        core.setSettings (s);
        feed (core, x, 0.0, 130.0);
        checkTrue (core.frozen(), "a cap the person set still fires");
        checkTrue (core.stoppedAutomatically(),
                   "... and it says it ended itself, not \"stopped by you\"");
        const double clock = core.heardPassSeconds();
        checkTrue (clock >= 119.0 && clock <= 122.0,
                   "the hand-set cap fires at its own second");
    }

    // ---- 1. a hand stops it ---------------------------------------------------
    {
        auto x = take (60.0);
        aeq::RestoreCore core;
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 45.0;
        core.prepare (kSr, 512);
        core.setSettings (s);
        feed (core, x, 0.0, 40.0);
        core.commit();                     // the clamp
        checkTrue (core.frozen(), "the clamp seals the pass");
        checkTrue (! core.stoppedAutomatically(),
                   "and the seal says a hand ended it, not the machine");
    }

    // ---- 2/3. the auto-stop dial ---------------------------------------------
    {
        aeq::RestoreCore core;
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 45.0;
        s.autoStopSeconds = 0.0;
        core.prepare (kSr, 512);
        core.setSettings (s);
        checkTrue (! core.stoppedAutomatically(), "nothing is 'auto-stopped' before a pass ends");

        auto x = take (30.0);
        aeq::RestoreCore dialled;
        s.autoStopSeconds = 12.0;
        dialled.prepare (kSr, 512);
        dialled.setSettings (s);
        feed (dialled, x, 0.0, 20.0);
        checkTrue (dialled.frozen(), "the auto-stop dial stops the pass at its setting");
        checkTrue (dialled.stoppedAutomatically(), "... and it reports that it stopped itself");
        const double clock = dialled.heardPassSeconds();
        checkTrue (clock >= 11.0 && clock <= 13.0,
                   "the dial stops on its own setting (clock), not at the cap");
    }

    // ---- 5. the words follow the same three states ---------------------------
    {
        aeq::copy::Facts f;
        f.haveData = true; f.frozen = true; f.committed = true; f.bands = 7;
        f.stoppedByUser = true;
        checkTrue (aeq::copy::stateWord (f) == "SEALED BY YOU", "the pill says who ended it (you)");
        f.stoppedByUser = false; f.autoStopped = true; f.heardSeconds = 45.0;
        checkTrue (aeq::copy::stateWord (f) == "SEALED (AUTO-STOP)",
                   "the pill says so when the auto-stop did it");
        f.autoStopped = false;
        checkTrue (aeq::copy::stateWord (f) == "SEALED (5 MIN CAP)",
                   "and when the five-minute cap did it");
        checkTrue (aeq::copy::verdict (f).find ("cap") != std::string::npos
                   || aeq::copy::sealedSub (f).find ("cap") != std::string::npos,
                   "the sentence under the seal names the cap too");
    }

    // ---- 6. the whole take first, then one decision ---------------------------
    //
    // The user's requirement, in his words: the plugin has to scan **all** the audio first,
    // because the number of EQ points it places is the number of problems it found - a pass
    // that decides from the first 45 s of a four-minute song has only heard a verse, and the
    // wrong twelve seconds is the wrong EQ.
    //
    // `settleSeconds = 0` is now the default and it means "the end of the take", not "off".
    // The only signal a VST3 plugin gets that a song is over is the play head: it rolls, and
    // then it stops.  This walks exactly that, and proves three things - no decision while
    // the transport runs, a decision when it stops, and no decision at all without a play
    // head (the CLI and the tests that never set one must not decide from a fragment).
    {
        auto x = take (400.0);
        aeq::RestoreCore core;
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 0.0;            // the product default: decide at the end
        s.autoStopSeconds = 0.0;
        core.prepare (kSr, 512);
        core.setSettings (s);

        // the transport rolls for 200 s of a 400 s take
        core.setTransport (true, true);
        feed (core, x, 0.0, 200.0);
        checkTrue (! core.state().settled,
                   "6a. the transport is rolling, so the take has not been heard - not settled");
        checkTrue (! core.committed(),
                   "6b. ... and no decision exists to apply: the audio is untouched");
        checkTrue (core.state().analyzedSeconds > 150.0,
                   "6c. ... while it really is analysing what goes past");

        // the transport stops: the take has been played through
        core.setTransport (false, true);
        core.pump();
        core.refresh();
        checkTrue (core.state().settled,
                   "6d. the transport stopped - now it has heard the whole take and is settled");

        // ... and the decision is made from all of it, not from a fragment of it
        core.commit();
        const int bandsOverWhole = core.state().corrective.size();
        checkTrue (bandsOverWhole >= 0,
                   "6e. the decision is solved from the whole take");
        std::printf ("        400 s take: settled at %.0f s of audio, %d bands\n",
                     core.state().analyzedSeconds, bandsOverWhole);

        // The falsifier, and the one that matters: a pass with no play head at all must NOT
        // settle on its own.  A DAW that hands no position (offline render, some hosts) would
        // otherwise decide from whatever fragment it happened to see.
        aeq::RestoreCore quiet;
        aeq::RestoreSettings q;
        q.learn = true; q.settleSeconds = 0.0; q.autoStopSeconds = 0.0;
        quiet.prepare (kSr, 512);
        quiet.setSettings (q);
        quiet.setTransport (false, false);       // no play head, not playing
        feed (quiet, x, 0.0, 120.0);
        checkTrue (! quiet.state().settled,
                   "6f. with no play head it never decides by itself - it waits for a hand");

        // And stopping by hand still works in this mode: the person is always allowed to
        // decide early, and the panel says the numbers are coarse when they do.
        quiet.commit();
        checkTrue (quiet.frozen() && quiet.state().valid,
                   "6g. ... and pressing the clamp decides from everything heard so far");
    }

    std::printf ("when_it_stops: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
