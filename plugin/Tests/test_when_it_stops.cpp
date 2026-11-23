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
    // ---- 0. the cap: nobody pressed anything ---------------------------------
    {
        auto x = take (430.0);
        aeq::RestoreCore core;
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 45.0;
        s.autoStopSeconds = 0.0;          // the default: it does not stop itself...
        core.prepare (kSr, 512);
        core.setSettings (s);
        feed (core, x, 0.0, 405.0);
        checkTrue (core.frozen(), "a pass nobody stops ends itself at the 5 minute cap");
        checkTrue (core.stoppedAutomatically(),
                   "... and says it ended itself - not \"stopped by you\"");
        // Two different seconds, on purpose, and the first version of this test confused
        // them: the *clock* is the input the pass was handed (300 s, gaps included), the
        // *meter* is sung audio only (about 76% of this take).  The cap is promised in
        // clock seconds - "in five minutes it stops itself" - so that is what is checked
        // here.  Measuring the cap in sung seconds meant it never fired on any take with
        // a breath in it, which is every take.
        const double clock = core.heardPassSeconds();
        const double heard = core.state().analyzedSeconds;
        std::printf ("        clock %.0f s of input, meter %.0f s of sung audio (cap 300 s)\n",
                     clock, heard);
        checkTrue (clock >= 299.0 && clock <= 302.0,
                   "the cap fires at the cap, not earlier and not never");
        checkTrue (heard < clock,
                   "the two seconds differ: the meter counts voice, the clock counts input");

        // 4. and a stopped pass stops counting - neither clock nor meter
        const double beforeClock = core.heardPassSeconds();
        const double before = core.state().analyzedSeconds;
        feed (core, x, 0.0, 2.0);         // more audio arrives after it was sealed
        const double afterClock = core.heardPassSeconds();
        const double after = core.state().analyzedSeconds;
        checkTrue (std::abs (after - before) < 0.5 && std::abs (afterClock - beforeClock) < 0.05,
                   "a sealed pass does not keep counting the audio that arrives after it");
        // ... and the same is true of the clock the cap runs on: it is only open while the
        // pass is open, so a sealed decision cannot be re-decided by a later cap.
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

    std::printf ("when_it_stops: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
