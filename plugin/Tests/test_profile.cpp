// test_profile.cpp - the reference profile: measured, blended, saved, and used.
//
// The question this file answers is the one the panel cannot: when the plugin says "aim
// at this reference", is the thing it aims at the thing that was measured - in the same
// frame as the take - or is it another number from another program?  Match EQ and
// smart:EQ both answer it by measuring both ends with one analyser; this proves the same
// here, arithmetically, with the numbers printed.
//
//   1. a profile subtracted from itself is zero, everywhere
//   2. a shelf in the reference comes back as a shelf of the same height
//   3. blending two references is a power mean, and 100/0 is exactly the first
//   4. a profile survives being written to a file and read back, bit for bit
//   5. the solver, given a target, puts its bands where the target differs
//   6. the plugin, given a reference, solves *toward that reference* - and the curve it
//      produces is measurably different from the corpus one
#include "../Source/dsp/Profile.h"
#include "../Source/dsp/EqSolver.h"
#include "../Source/dsp/RestoreCore.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static void checkF(bool ok, const char* fmt, double a = 0.0, double b = 0.0) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, a, b);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

// a profile on the analyser's grid, without an analyser (1/12 octave, 40 Hz - 16 kHz)
static aeq::Profile makeProfile(const char* name, double (*shape)(double), double offsetDb = 0.0) {
    aeq::Profile p;
    p.name = name;
    for (int k = 0;; ++k) {
        const double f = 40.0 * std::pow(2.0, k / 12.0);
        if (f > 16000.0) break;
        p.freqs.push_back(f);
        p.db.push_back(shape ? shape(f) + offsetDb : offsetDb);
    }
    p.frames = 400;
    return p;
}

static double flat(double)            { return 0.0; }
static double shelf6(double f)        { return f > 4000.0 ? 6.0 : 0.0; }
static double dipAt1k5(double f)      { return (f > 1000.0 && f < 2200.0) ? -6.0 : 0.0; }

static double atFreq(const std::vector<double>& xs, const std::vector<double>& ys, double f) {
    return aeq::SolveInput::interpLog(xs, ys, f);
}

static aeq::Profile capturedFrom(const std::vector<float>& take, const char* name) {
    aeq::RestoreCore core;
    core.prepare(kSr, 256);
    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = 0.0;
    core.setSettings(s);
    std::vector<float> l(256), r(256);
    size_t pos = 0; int blk = 0;
    while (pos < take.size()) {
        for (int i = 0; i < 256; ++i) {
            l[size_t(i)] = (pos + size_t(i) < take.size()) ? take[pos + size_t(i)] : 0.0f;
            r[size_t(i)] = l[size_t(i)];
        }
        core.processBlock(l.data(), r.data(), 256);
        pos += 256;
        if (++blk % 38 == 0) core.refresh();
    }
    core.refresh();
    return core.captureProfile(name);
}

// a sung note with harmonics and phrase envelopes - the same generator the other gates use
static std::vector<float> voice(double f0, double seconds, int tiltDbPerOct) {
    const int n = int(seconds * kSr);
    std::vector<float> x(size_t(n), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int k = 1; k <= 30; ++k) {
            const double oct = std::log2(double(k));
            s += std::sin(k * phase) / std::pow(k, 1.05)
               * std::pow(10.0, tiltDbPerOct * oct / 20.0);
        }
        const double cyc = std::fmod(t, 1.55);
        x[size_t(i)] = float(0.10 * ((cyc < 1.25) ? 1.0 : 0.0) * s);
    }
    return x;
}

int main() {
    std::printf("\n===== AEQ RESTORE - reference profiles =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const auto flatP  = makeProfile("flat", flat);
    const auto shelfP = makeProfile("shelf", shelf6);
    const auto dipP   = makeProfile("dip", dipAt1k5);

    // ---- 1. a profile against itself ----------------------------------------
    {
        const auto d = aeq::deltaCurve(shelfP, shelfP);
        double worst = 0.0;
        for (double v : d) worst = std::max(worst, std::abs(v));
        checkF(d.size() == shelfP.freqs.size() && worst == 0.0,
               "1. a profile subtracted from itself is exactly zero (worst %.1e)", worst);
    }

    // ---- 2. a shape in the reference is the delta ---------------------------
    {
        const auto d = aeq::deltaCurve(flatP, shelfP);
        const double at2k  = atFreq(flatP.freqs, d, 2000.0);
        const double at6k  = atFreq(flatP.freqs, d, 6000.0);
        const double at12k = atFreq(flatP.freqs, d, 12000.0);
        checkF(std::abs(at2k) < 0.2, "2. below the shelf the delta is zero (%.2f dB)", at2k);
        checkF(std::abs(at6k - 6.0) < 0.4, "2. at the shelf it is the shelf (%.2f of 6.00 dB)", at6k);
        checkF(std::abs(at12k - 6.0) < 0.4, "2. and it stays there (%.2f of 6.00 dB)", at12k);
    }

    // ---- 3. blending: a power mean, and an exact 100/0 ---------------------
    {
        std::vector<const aeq::Profile*> both{&flatP, &shelfP};
        const auto half = aeq::blend(both, {0.5, 0.5});
        const double at8k = half.at(8000.0) - half.at(1000.0);
        // half of 1x plus half of 4x is 2.49x: 3.96 dB, not the 3 dB an average of dB gives
        checkF(std::abs(at8k - 3.96) < 0.25, "3. half and half is a power mean (%.2f dB, not 3.00)", at8k);

        const auto only = aeq::blend(both, {0.0, 1.0});
        double worst = 0.0;
        for (size_t i = 0; i < only.freqs.size(); ++i)
            worst = std::max(worst, std::abs((only.db[i] - only.at(1000.0))
                                           - (shelfP.db[i] - shelfP.at(1000.0))));
        checkF(worst < 1e-9, "3. 100%% of one reference is that profile exactly (worst %.1e dB)", worst);

        // level must not matter: the same shape 20 dB louder is the same target
        const auto quiet = makeProfile("quiet", shelf6, -20.0);
        std::vector<const aeq::Profile*> mix{&shelfP, &quiet};
        const auto same = aeq::blend(mix, {0.5, 0.5});
        const double dev = same.at(9000.0) - same.at(1000.0) - 6.0;
        checkF(std::abs(dev) < 1e-6, "3. level is divided out of a blend (%.1e dB off)", dev);
    }

    // ---- 4. a profile is a file --------------------------------------------
    {
        const std::string text = aeq::toText(shelfP);
        const auto back = aeq::fromText(text);
        checkF(back.freqs == shelfP.freqs && back.db == shelfP.db && back.name == shelfP.name,
               "4. a profile written and read back is identical (%.0f points)", double(back.db.size()));
        checkF(aeq::fromText("not a profile\n").valid() == false,
               "4. and a file that is not one is refused (%.0f)", 0.0);
    }

    // ---- 5. the solver aims at the supplied target -------------------------
    {
        aeq::SolveInput in;
        in.freqs = flatP.freqs;
        in.levelDb.assign(flatP.freqs.size(), -30.0);
        in.valid.assign(flatP.freqs.size(), 1.0);
        in.targetDb.assign(flatP.freqs.size(), 0.0);
        for (size_t i = 0; i < flatP.freqs.size(); ++i) {
            const double f = flatP.freqs[i];
            in.targetDb[i] = (f > 1000.0 && f < 2200.0) ? -6.0 : 0.0;
        }
        aeq::EqSolver solver;
        aeq::SolverConfig sc;
        sc.mode = aeq::MatchMode::Match;
        sc.maxBands = 6;
        sc.maxGainDb = 6.0;
        const auto bands = solver.solve(in, sc, kSr);

        bool near1k5 = false;
        double gainThere = 0.0;
        for (const auto& b : bands)
            if (b.f0 > 800.0 && b.f0 < 2600.0 && b.gainDb < -1.0) { near1k5 = true; gainThere = b.gainDb; }
        checkF(near1k5, "5. a dip in the target produces a cut where the dip is (%.2f dB)", gainThere);
        checkF(gainThere > -6.5, "5. and inside the product's ceiling (%.2f dB)", gainThere);

        const double before = aeq::EqSolver::distanceToTarget(in, {}, kSr);
        const double after  = aeq::EqSolver::distanceToTarget(in, bands, kSr);
        checkF(after < before, "5. the distance to that target falls (%.2f -> %.2f dB RMS)", before, after);
    }

    // ---- 6. the plugin solves toward the reference, not the corpus ----------
    {
        // two takes of the same voice: one dull, one bright
        const auto dull   = voice(196.0, 20.0, -12);
        const auto bright = voice(196.0, 20.0, +12);
        const auto ref = capturedFrom(bright, "bright-take");
        checkF(ref.valid() && ref.db.size() > 50,
               "6. a reference was measured by the plugin's own analyser (%.0f bands)",
               double(ref.db.size()));

        auto run = [&](bool withRef) {
            aeq::RestoreCore core;
            core.prepare(kSr, 256);
            aeq::RestoreSettings s;
            s.learn = true;
            s.settleSeconds = 3600.0;      // the person decides when to stop
            s.mode = 1;                    // Match
            s.maxGainDb = 6.0;
            core.setSettings(s);
            if (withRef) core.setReferences({ref}, {1.0});
            std::vector<float> l(256), r(256);
            size_t pos = 0; int blk = 0;
            while (pos < dull.size()) {
                for (int i = 0; i < 256; ++i) {
                    l[size_t(i)] = (pos + size_t(i) < dull.size()) ? dull[pos + size_t(i)] : 0.0f;
                    r[size_t(i)] = l[size_t(i)];
                }
                core.processBlock(l.data(), r.data(), 256);
                pos += 256;
                if (++blk % 38 == 0) core.refresh();
            }
            core.refresh();
            core.commit();
            return core.state().corrective;
        };

        const auto bandRef = run(true);
        const auto bandCorpus = run(false);

        auto topGain = [](const std::vector<aeq::BandSpec>& v) {
            double g = 0.0;
            for (const auto& b : v) if (b.f0 > 2500.0) g += b.gainDb;
            return g;
        };
        const double gRef = topGain(bandRef), gCorpus = topGain(bandCorpus);
        std::printf("  top end: %+.2f dB with the reference, %+.2f dB against the corpus\n", gRef, gCorpus);
        checkF(bandRef.size() > 0 && bandCorpus.size() > 0,
               "6. both runs produced a curve (%.0f bands, %.0f bands)",
               double(bandRef.size()), double(bandCorpus.size()));
        checkF(gRef > gCorpus + 1.0,
               "6. the reference lifts the dull take toward the bright one (%.2f dB more)", gRef - gCorpus);
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
