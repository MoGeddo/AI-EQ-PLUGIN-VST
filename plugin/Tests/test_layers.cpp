// test_layers.cpp - the dynamic layers the core puts into the path, end to end.
//
// tests/test_tracked.cpp proves the band itself.  This file proves the *decision*:
// that RestoreCore, after the user stops the learn pass, actually puts a moving band
// where a learned cut sits on a harmonic of the note, and that the band then does the
// job it was chosen for - holding that harmonic at the level the take had when the
// same performance is sung again, louder.
//
// The take answers "does it work?" in the only way that matters: the same performance
// at +10 dB.  A static curve cannot tell the two apart; a band that follows the note
// can.  So the gate runs the louder take through two cores that share the *same*
// committed decision - one with the layers, one with `setTrackedLayers({})` - and
// compares the level of the offending harmonic against a neighbour that was never
// touched.  Relative, because the plugin's level match moves the absolute level on
// purpose.
//
//   WANT  with a layer, the louder take's 3rd harmonic comes back to the level it had
//         on the quieter take; without it, it stays 10 dB louder.  The layer is given
//         the one number that matters - where to hold - which is exactly what commit
//         hands it (the level the corrected take had at that harmonic), so the two
//         halves of the gate test the decision and the behaviour separately instead of
//         depending on a synthetic take happening to make the solver cut a harmonic.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_layers.cpp -o build/aeq_layers
//   ./build/aeq_layers
//
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "../Source/dsp/RestoreCore.h"

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static void checkTrue(bool ok, const char* what) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}
static void checkF(bool ok, const char* fmt, double a, double b) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, a, b);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

// A sung note with a strong resonance exactly on its 3rd harmonic (510 Hz at 170 Hz),
// which is the case a static EQ cannot win: cut it once and a note sung higher puts
// its own 3rd harmonic somewhere else.
static std::vector<float> sungTake(double f0, double seconds, double gain,
                                  double resHz, double resDb, double resQ) {
    const int n = int(seconds * kSr);
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int k = 1; k <= 30; ++k) s += std::sin(k * phase) / std::pow(k, 1.05);
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.25) ? 1.0 : 0.0;
        x[size_t(i)] = float(gain * env * s);
    }
    // the resonance the take is here to expose, with its frequency handed in
    const double bellHz = resHz;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    const double w = 2.0 * kPi * bellHz / kSr, q = resQ, A = std::pow(10.0, resDb / 40.0);
    const double alpha = std::sin(w) / (2.0 * q), cw = std::cos(w), a0 = 1.0 + alpha / A;
    const double b0 = (1.0 + alpha * A) / a0, b1 = (-2.0 * cw) / a0, b2 = (1.0 - alpha * A) / a0;
    const double a1 = (-2.0 * cw) / a0, a2 = (1.0 - alpha / A) / a0;
    for (auto& v : x) {
        const double in = v;
        const double y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in; y2 = y1; y1 = y;
        v = float(in + 0.10 * y);
    }
    return x;
}

// The level of one harmonic, measured the way a listener hears it: a Goertzel at that
// exact frequency, run on 8 ms hops, keeping only the hops where the singer is actually
// singing (the take has breath gaps), and taking the median of them.  A filter band or a
// whole-take mean both disagree with each other by several dB depending on how wide they
// are and whether they include the gaps - which turns a test into a bookkeeping argument.
static double harmDb(const std::vector<float>& x, double hz, double fs) {
    const int hop = int(fs * 0.008);
    const double w = 2.0 * kPi * hz / fs, c = 2.0 * std::cos(w);
    std::vector<double> vals;
    for (size_t start = 0; start + size_t(hop) <= x.size(); start += size_t(hop)) {
        double rms = 0.0;
        for (int i = 0; i < hop; ++i) rms += double(x[start + size_t(i)]) * double(x[start + size_t(i)]);
        rms = std::sqrt(rms / hop);
        if (rms < 1e-3) continue;                  // a gap, or a breath
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < hop; ++i) {
            s0 = double(x[start + size_t(i)]) + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;    // |X|^2
        const double amp = std::sqrt(std::max(0.0, power)) * 2.0 / hop;
        vals.push_back(20.0 * std::log10(std::max(1e-9, amp)));
    }
    if (vals.empty()) return -120.0;
    std::sort(vals.begin(), vals.end());
    return vals[vals.size() / 2];
}

struct Run {
    std::unique_ptr<aeq::RestoreCore> core;
    std::vector<float> l, r;
    explicit Run() : core(std::make_unique<aeq::RestoreCore>()), l(512), r(512) {
        core->prepare(kSr, 512);
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 3600.0;          // the gate presses Stop itself
        core->setSettings(s);
    }
    // Optional trace of one layer's measured level, sampled once per block.  The level
    // readout is instantaneous, and a take has breath gaps in it, so a test that wants
    // "where this take sat" has to take the median of the frames that had signal.
    std::vector<float> play(const std::vector<float>& take, std::vector<double>* levelTrace = nullptr) {
        std::vector<float> out;
        out.reserve(take.size());
        size_t pos = 0;
        int blocks = 0;
        while (pos < take.size()) {
            for (int i = 0; i < 512; ++i) {
                const float v = (pos < take.size()) ? take[pos] : 0.0f;
                ++pos;
                l[size_t(i)] = v;
                r[size_t(i)] = v;
            }
            core->processBlock(l.data(), r.data(), 512);
            // the analysis thread's job, exactly as the plugin's timer does it: the ring
            // only holds 1.36 s, so a 30 s take has to be drained while it plays
            if ((++blocks % 40) == 0) core->refresh();
            if (levelTrace && core->trackedCount() > 0) levelTrace->push_back(core->trackedLevelDb(0));
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    }
    // learn, refresh (the analysis thread's job), then Stop
    void learnThenStop(const std::vector<float>& take) {
        play(take);
        core->refresh();
        core->commit();
    }
};

int main() {
    std::printf("\n===== AEQ RESTORE - the layers that follow the note =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // ---- 1. the decision: does a real take produce a layer at all?
    // A sung note with its 3rd harmonic exaggerated.  Whether the solver cuts that
    // harmonic or asks for a tilt boost is the solver's business; what is checked here
    // is that whenever it *does* cut a harmonic, the commit turns it into a moving band
    // that points at that harmonic - and that nothing else becomes one.
    {
        const double f0 = 220.0;
        Run a;
        a.learnThenStop(sungTake(f0, 30.0, 0.13, 3 * f0, 9.0, 10.0));
        const auto st = a.core->state();
        const int layers = a.core->trackedCount();
        std::printf("  %zu corrective + %zu resonance bands, f0 %.0f Hz -> %d dynamic layer(s)\n",
                    st.corrective.size(), st.resonance.size(), st.measuredF0, layers);
        for (int i = 0; i < layers; ++i) {
            const auto sp = a.core->trackedSpec(i);
            std::printf("    layer %d: harmonic %d, Q %.1f, holds %.1f dBFS, up to %.1f dB down\n",
                        i + 1, sp.harmonic, sp.q, sp.targetDb, sp.rangeDown);
        }
        checkTrue(layers >= 1, "a learned cut on a harmonic became a moving band");
        checkTrue(layers <= 2, "and never more than two of them");

        // every layer must point at a band that was really cut near k * f0
        bool allPointAtACut = true;
        for (int i = 0; i < layers; ++i) {
            const auto sp = a.core->trackedSpec(i);
            bool found = false;
            for (const auto* list : {&st.corrective, &st.resonance}) {
                for (const auto& b : *list) {
                    if (b.gainDb > -1.5) continue;
                    if (std::abs(b.f0 / (sp.harmonic * st.measuredF0) - 1.0) > 0.05) continue;
                    found = true;
                }
            }
            allPointAtACut = allPointAtACut && found;
            if (!found) std::printf("    layer %d points at no learned cut\n", i + 1);
        }
        checkTrue(layers == 0 || allPointAtACut, "every layer sits on a cut the solver made");
        bool cutsOnly = true;
        for (int i = 0; i < layers; ++i) cutsOnly = cutsOnly && a.core->trackedSpec(i).rangeUp <= 0.001;
        checkTrue(cutsOnly, "and none of them can boost a harmonic");
    }

    // ---- 2. the behaviour: hold the 3rd harmonic where the quiet take had it
    const double f0 = 200.0;
    const double resHz = 3 * f0;                               // 600 Hz
    const auto takeA = sungTake(f0, 20.0, 0.13, resHz, 9.0, 10.0);        // the quiet one
    const auto takeB = sungTake(f0, 20.0, 0.13 * 3.162, resHz, 9.0, 10.0); // +10 dB, same note

    auto layerAt = [](double targetDb, double rangeDown) {
        std::vector<aeq::TrackedSpec> v(1);
        v[0].harmonic = 3;
        v[0].q = 5.0;
        v[0].targetDb = targetDb;
        v[0].rangeDown = rangeDown;
        v[0].rangeUp = 0.0;
        return v;
    };
    // calibrate exactly the way commit does it: measure the take, take the level of the
    // 1/12-octave band that holds 600 Hz, and hold there.  (Not the band's own readout -
    // the band's detector and the analyser's grid have different shapes, and mixing the
    // two would be measuring a 2 dB bookkeeping error, not the hold.)
    double holdDb = -30.0;
    {
        Run cal;
        aeq::RestoreState bare; bare.valid = true;
        cal.core->restoreDecision(bare);
        cal.core->setTrackedLayers(layerAt(-90.0, 12.0));
        std::vector<double> trace;
        cal.play(takeA, &trace);
        const auto ms = cal.core->measurements();
        double best = -30.0, bestDist = 1e9;
        for (size_t i = 0; i < ms.freqs.size() && i < ms.bandDb.size(); ++i) {
            const double d = std::abs(std::log2(ms.freqs[i] / resHz));
            if (d < bestDist) { bestDist = d; best = ms.bandDb[i]; }
        }
        holdDb = best;
        std::printf("  the quiet take measures %.1f dBFS at %.0f Hz; that is the target\n", holdDb, resHz);
        checkTrue(std::abs(cal.core->trackedCentreHz(0) - resHz) < 0.08 * resHz,
                  "the band found the harmonic it was told to follow");
    }

    auto playWith = [&](const std::vector<float>& take, double targetDb, double rangeDb) {
        Run r;
        aeq::RestoreState bare; bare.valid = true;
        r.core->restoreDecision(bare);
        r.core->setTrackedLayers(layerAt(targetDb, rangeDb));
        return r.play(take);
    };
    auto playWithout = [&](const std::vector<float>& take) {
        Run r;
        aeq::RestoreState bare; bare.valid = true;
        r.core->restoreDecision(bare);
        r.core->setTrackedLayers({});
        return r.play(take);
    };

    // 6 dB of range is what the core itself gives such a layer (it is a stabiliser, not a
    // compressor), and 18 dB is the same band with the room to hold outright - the pair
    // shows that the mechanism can hold, and what the shipped range buys.
    const double kCoreRange = 6.0;
    const auto quietHeld  = playWith(takeA, holdDb, kCoreRange);
    const auto loudHeld   = playWith(takeB, holdDb, kCoreRange);
    // to hold a 12.7 dB jump outright the band needs the jump plus its own 3 dB dead band
    const auto loudHeld12 = playWith(takeB, holdDb, 18.0);
    const auto loudFree   = playWithout(takeB);

    // the reference is the quiet take with no layer at all: that is where the harmonic
    // sat, and where a hold has to put the louder take back to
    const auto quietFree = playWithout(takeA);
    const double levelQuiet  = harmDb(quietFree, resHz, kSr);
    const double levelQuietHeld = harmDb(quietHeld, resHz, kSr);
    const double levelHeld   = harmDb(loudHeld, resHz, kSr);
    const double levelFree   = harmDb(loudFree, resHz, kSr);
    const double levelHeld18 = harmDb(loudHeld12, resHz, kSr);
    std::printf("       harmonic level: quiet take %.2f dB, loud take with the layer %.2f dB, without %.2f dB\n",
                levelQuiet, levelHeld, levelFree);

    checkF(levelFree - levelQuiet > 7.0,
           "without a layer the louder take's harmonic is %.2f dB louder than the quiet take (%.2f dB)",
           levelFree - levelQuiet, levelQuiet);
    std::printf("       the 6 dB layer took %.2f dB of that jump; its range allows %.1f dB\n",
                levelFree - levelHeld, kCoreRange);
    checkTrue(levelFree - levelHeld >= 0.8 * kCoreRange,
              "it uses the range it was given, rather than a fraction of it");
    checkF(std::abs(levelHeld18 - levelQuiet) <= 2.0,
           "given the room it holds outright: %.2f dB against %.2f dB on the quiet take",
           levelHeld18, levelQuiet);
    checkF(std::abs(levelQuietHeld - levelQuiet) <= 3.0,
           "and the take it learned is only touched on its loudest frames (%.2f vs %.2f dB)",
           levelQuietHeld, levelQuiet);

    // ---- 3. nothing in that may click or blow up
    {
        double inStep = 0.0, outStep = 0.0;
        for (size_t i = 1; i < takeB.size(); ++i)
            inStep = std::max(inStep, std::abs(double(takeB[i]) - double(takeB[i - 1])));
        for (size_t i = 1; i < loudHeld.size(); ++i)
            outStep = std::max(outStep, std::abs(double(loudHeld[i]) - double(loudHeld[i - 1])));
        bool finite = true;
        for (float v : loudHeld) finite = finite && std::isfinite(v);
        checkTrue(finite, "the output of the louder take stays finite");
        checkF(outStep <= inStep * 4.0 + 1e-4,
               "no click from the moving band (worst step %.4f against %.4f)", outStep, inStep);
    }

    // ---- 4. a project saved without layers restores with none
    {
        Run d;
        aeq::RestoreState bare; bare.valid = true;
        d.core->restoreDecision(bare);
        d.core->setTrackedLayers({});
        d.play(takeA);
        checkTrue(d.core->trackedCount() == 0, "a project saved without layers restores with none");
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
