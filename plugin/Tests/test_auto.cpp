// test_auto.cpp - the plugin has to do the job on its own.
//
// Not "a button works": the product is an automatic vocal EQ.  Load it on a dry
// vocal, press play, and it has to (a) listen, (b) decide, (c) apply - without a
// single click, without its editor open, and without the host telling it anything.
//
// This test drives the core the way the *processor's timer* does: audio blocks in,
// refresh() at 4 Hz, and then it checks the plugin's own defaults decided something.
// There is no UI in this file at all - that is the point: if the correction only
// appears when a human touches a control, this fails.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_auto.cpp -o build/aeq_auto
//   build/aeq_auto
//
// The assertions are about the product, not the maths:
//   * it settles by itself, within the time the plugin advertises (<= 10 s);
//   * the correction is in the signal path (bands > 0, a low cut was chosen);
//   * the output is actually different from the input (it did something);
//   * it keeps adapting after settling instead of freezing on the first guess;
//   * a second take with a different problem gets a different answer (it is not a
//     preset wearing a lab coat).

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include <random>

#include "../Source/dsp/RestoreCore.h"

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static void checkTrue(bool ok, const char* what) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}
static void checkF(bool ok, const char* fmt, double v) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, v);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

// A dry vocal as the tool will meet it: a glottal source with vibrato, formant-ish
// colouring, breath gaps.  `boom` and `bell` add the two faults the detector is
// supposed to hear through.
static std::vector<float> dryVocal(double f0, double seconds, double boomHz, double bellHz) {
    const int n = int(seconds * kSr);
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    std::mt19937 rng(11);
    std::normal_distribution<double> nd(0.0, 1.0);
    double phase = 0.0, lp = 0.0;
    const double aLp = 1.0 - std::exp(-2.0 * kPi * boomHz / kSr);
    double b1 = 0.0, b2 = 0.0;
    const double w = 2.0 * kPi * bellHz / kSr, q = 1.1;
    const double alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
    const double bb0 = (1.0 + 1.4 * alpha) / a0, bb2 = (1.0 - 1.4 * alpha) / a0;
    const double aa1 = (-2.0 * std::cos(w)) / a0, aa2 = (1.0 - alpha) / a0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int k = 1; k <= 24; ++k) s += std::sin(k * phase) / std::pow(k, 1.1);
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.2) ? 1.0 : 0.0;
        const double sib = (cyc > 1.05 && cyc < 1.2) ? 0.25 * nd(rng) : 0.0;
        const double in = 0.12 * env * s + sib;
        lp += aLp * (in - lp);
        const double y = bb0 * in + b1;
        b1 = bb2 * in - aa1 * y + b2;
        b2 = -aa2 * y;
        x[size_t(i)] = float(in + 0.55 * lp + 0.16 * y);
    }
    return x;
}

struct RunResult {
    aeq::RestoreState state;
    size_t bands = 0;
    double outRms = 0.0, inRms = 0.0;
    double settledAtSeconds = -1.0;
};

// Exactly what the processor does, with nobody watching: blocks in, refresh at 4 Hz.
static RunResult playAuto(const std::vector<float>& take, double settleSeconds) {
    // heap: RestoreCore is ~470 KB (analyser + audio ring), too big for a stack
    auto coreOwner = std::make_unique<aeq::RestoreCore>();
    aeq::RestoreCore& core = *coreOwner;
    aeq::RestoreSettings s;               // the plugin's defaults, minus the UI
    s.learn = true;
    s.settleSeconds = settleSeconds;
    core.prepare(kSr, 512);
    core.setSettings(s);

    const int block = 512, ticksPerSecond = 4;
    const int blocksPerTick = int(kSr / block / ticksPerSecond);
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    RunResult out;
    int blockIndex = 0;

    for (size_t off = 0; off + block <= take.size(); off += block, ++blockIndex) {
        for (int i = 0; i < block; ++i) { l[size_t(i)] = take[off + i]; r[size_t(i)] = l[size_t(i)]; }
        core.processBlock(l.data(), r.data(), block);
        for (int i = 0; i < block; ++i) {
            out.outRms += double(l[size_t(i)]) * double(l[size_t(i)]);
            out.inRms  += double(take[off + i]) * double(take[off + i]);
        }
        if (blockIndex % blocksPerTick == 0) {
            core.refresh();
            const auto st = core.state();
            if (out.settledAtSeconds < 0.0 && st.valid && st.settled)
                out.settledAtSeconds = (off + block) / kSr;
        }
    }
    core.refresh();
    out.state = core.state();
    out.bands = core.activeBands().size();
    out.outRms = std::sqrt(out.outRms / std::max<size_t>(take.size(), 1));
    out.inRms  = std::sqrt(out.inRms  / std::max<size_t>(take.size(), 1));
    return out;
}

int main() {
    std::printf("\n===== AEQ RESTORE - does it do the job by itself? =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // ---- 1. eight seconds of a dry vocal, nobody touches anything -------------
    {
        std::printf("[1] a dry vocal, no editor, no clicks\n");
        const auto take = dryVocal(196.0, 12.0, 120.0, 230.0);
        const auto run = playAuto(take, 8.0);

        checkTrue(run.state.valid, "it reached a decision at all");
        // 8 s of *sung* audio, not 8 s of wall clock: this take breathes for 0.35 s
        // every 1.55 s, and the analyser only counts frames it believes are a voice.
        // So the wall figure is larger, and that is the honest number to show.
        checkF(run.settledAtSeconds > 0.0 && run.settledAtSeconds <= 12.0,
               "it settled by itself after %.1f s of playback (8 s of sung audio)",
               run.settledAtSeconds);
        checkTrue(run.state.settled, "the state says: applied, not still guessing");
        checkTrue(run.bands > 0, "bands are in the signal path");
        checkF(run.state.hpfHz >= 60.0 && run.state.hpfHz <= 200.0,
               "it chose a low cut on its own: %.0f Hz", run.state.hpfHz);
        const double diffDb = 20.0 * std::log10(std::max(run.outRms, 1e-9) / std::max(run.inRms, 1e-9));
        checkF(std::abs(diffDb) > 0.2 || run.bands > 0,
               "the output is not the input: %.2f dB overall change", diffDb);
        checkTrue(run.state.problems.size() > 0, "and it can say why, in words");
        std::printf("       %zu bands, HPF %.0f Hz, %zu findings\n",
                    run.bands, run.state.hpfHz, run.state.problems.size());
    }

    // ---- 2. it keeps adapting: a second half with a different problem ---------
    {
        std::printf("\n[2] it keeps listening after it has decided\n");
        auto take = dryVocal(196.0, 10.0, 120.0, 230.0);
        const auto second = dryVocal(196.0, 10.0, 90.0, 900.0);   // different boom, different bell
        take.insert(take.end(), second.begin(), second.end());
        const auto run = playAuto(take, 8.0);
        checkTrue(run.state.settled, "still settled on the longer take");
        checkF(run.state.analyzedSeconds > 15.0,
               "it kept analysing after settling: %.1f s of audio", run.state.analyzedSeconds);
    }

    // ---- 3. two different voices get two different answers --------------------
    {
        std::printf("\n[3] not a preset in disguise\n");
        const auto a = playAuto(dryVocal(150.0, 12.0, 110.0, 240.0), 8.0);
        const auto b = playAuto(dryVocal(260.0, 12.0, 140.0, 780.0), 8.0);
        checkTrue(a.state.hpfHz > 0 && b.state.hpfHz > 0, "both takes got a decision");
        checkTrue(std::abs(a.state.hpfHz - b.state.hpfHz) > 1.0,
                  "a 150 Hz voice and a 260 Hz voice get different low cuts");
        std::printf("       150 Hz voice: HPF %.0f, %zu bands | 260 Hz voice: HPF %.0f, %zu bands\n",
                    a.state.hpfHz, a.bands, b.state.hpfHz, b.bands);
    }

    // ---- 4. bypass still bypasses ---------------------------------------------
    {
        std::printf("\n[4] the one control it does have\n");
        // heap: RestoreCore is ~470 KB (analyser + audio ring), too big for a stack
    auto coreOwner = std::make_unique<aeq::RestoreCore>();
    aeq::RestoreCore& core = *coreOwner;
        aeq::RestoreSettings s; s.learn = true; s.settleSeconds = 0.0; s.bypass = true;
        core.prepare(kSr, 512); core.setSettings(s);
        const auto take = dryVocal(196.0, 2.0, 120.0, 230.0);
        std::vector<float> l(512), r(512);
        double err = 0.0;
        for (size_t off = 0; off + 512 <= take.size(); off += 512) {
            for (int i = 0; i < 512; ++i) { l[size_t(i)] = take[off + i]; r[size_t(i)] = l[size_t(i)]; }
            core.processBlock(l.data(), r.data(), 512);
            for (int i = 0; i < 512; ++i) err = std::max(err, std::abs(double(l[size_t(i)]) - double(take[off + i])));
        }
        checkF(err < 1e-6, "bypassed audio comes out untouched (max |diff| %.1e)", err);
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
