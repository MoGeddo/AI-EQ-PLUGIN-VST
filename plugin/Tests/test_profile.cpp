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
//   7. the built-in target is Amir's own curve, and Restore never chases it
//   8. comparing a curve to *itself* gives zero at every comparison width - the invariant
//      that catches asymmetric smoothing (a target normalised before smoothing and a
//      measurement normalised after it are not the same quantity: measured 2.97 dB became
//      13.20 dB, which no EQ could close)
#include "../Source/dsp/Profile.h"
#include "../Source/dsp/EqSolver.h"
#include "../Source/dsp/RestoreCore.h"
#include "../Source/dsp/Biquad.h"

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

// A "record" shape: the same voice with a gentle low shelf down and a broad lift in the
// part of the spectrum a sung note actually occupies, which is what the difference between
// two engineers' vocals looks like - a few dB of tilt, not 24 dB/octave.  (The first
// version of this test used a 24 dB/octave tilt and its numbers were about the fixture,
// not the plugin: 31.7 dB apart, and no bounded EQ can or should close that.)
//
// The lift moved from 4 kHz to 900 Hz when Match gained its occupancy mask: a voice at
// 196 Hz carries almost nothing up there, so the 4 kHz shelf sat outside the bands the
// mask allows, and the measured distance collapsed to 1.36 dB - the fixture had stopped
// being far from the target, and the "closes a quarter of the gap" assertion would have
// been testing a gap of under 2 dB however the solver behaved.
static std::vector<float> withShape(const std::vector<float>& dry) {
    auto low  = aeq::Biquad::lowShelf(220.0, 0.71, -3.5, kSr);
    auto peak = aeq::Biquad::peaking(900.0, 0.8, +4.0, kSr);
    std::vector<float> y = dry;
    for (auto& v : y) v = float(peak.process(low.process(double(v))));
    return y;
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

        // The parser is a trust boundary: these are the ways a hostile or broken file
        // gets in, and every one of them has to be refused whole rather than half-read.
        auto header = [] (int points) {
            return std::string("AEQPROFILE 1\nname t\nloud 0\nframes 1\npoints ")
                 + std::to_string(points) + "\n";
        };
        auto line = [] (const char* a, const char* b) {
            return std::string(a) + " " + std::string(b) + "\n";
        };
        checkF(! aeq::fromText(header(2) + line("100", "-30") + line("nan", "-30")).valid(),
               "4. a NaN frequency is refused (%.0f)", 0.0);
        checkF(! aeq::fromText(header(2) + line("100", "-30") + line("inf", "-30")).valid(),
               "4. an infinite frequency is refused (%.0f)", 0.0);
        checkF(! aeq::fromText(header(2) + line("900", "-30") + line("100", "-30")).valid(),
               "4. descending frequencies are refused (%.0f)", 0.0);
        checkF(! aeq::fromText(header(1) + line("0", "-30")).valid(),
               "4. a zero frequency is refused (%.0f)", 0.0);
        checkF(! aeq::fromText(header(1) + line("100", "1e9")).valid(),
               "4. an absurd level is refused (%.0f)", 0.0);
        {
            std::string huge = header(20000);
            for (int i = 0; i < 20000; ++i)
                huge += line(std::to_string(20.0 + i).c_str(), "-30");
            checkF(! aeq::fromText(huge).valid(),
                   "4. a file with more points than the cap is refused (%.0f)", 0.0);
        }
        checkF(aeq::fromText(std::string(aeq::kMaxProfileBytes + 1, 'x')).valid() == false,
               "4. a file over the size cap is refused (%.0f)", 0.0);
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
        const auto dull   = voice(196.0, 20.0, 0);
        const auto bright = withShape(dull);          // the record's shape, same voice
        const auto ref = capturedFrom(bright, "record");
        checkF(ref.valid() && ref.db.size() > 50,
               "6. a reference was measured by the plugin's own analyser (%.0f bands)",
               double(ref.db.size()));

        auto run = [&](bool withRef, double* gapBefore = nullptr, double* gapAfter = nullptr) {
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
            if (gapBefore) *gapBefore = core.state().gapBeforeDb;
            if (gapAfter)  *gapAfter  = core.state().gapAfterDb;
            return core.state().corrective;
        };

        double before = 0.0, after = 0.0;
        const auto bandRef = run(true, &before, &after);
        const auto bandCorpus = run(false);

        std::printf("  the dull take against the bright reference: %.2f dB RMS before the "
                    "curve, %.2f dB after it (%.0f%% closer)\n",
                    before, after, before > 0.0 ? 100.0 * (1.0 - after / before) : 0.0);

        checkF(bandRef.size() > 0 && bandCorpus.size() > 0,
               "6. both runs produced a curve (%.0f bands with the reference, %.0f against the corpus)",
               double(bandRef.size()), double(bandCorpus.size()));
        checkF(before > 2.0, "6. the take really is far from the reference to begin with (%.2f dB)", before);
        checkF(after < before * 0.75,
               "6. the matched curve closes at least a quarter of the gap (%.2f -> %.2f dB)", before, after);
        checkF(after < 2.0, "6. and what is left is small enough to hear as 'the same voice' (%.2f dB)", after);

        // the curve is not the corpus answer wearing a different label
        bool sameAsCorpus = bandRef.size() == bandCorpus.size();
        if (sameAsCorpus)
            for (size_t i = 0; i < bandRef.size(); ++i)
                if (std::abs(bandRef[i].f0 - bandCorpus[i].f0) > 1.0
                    || std::abs(bandRef[i].gainDb - bandCorpus[i].gainDb) > 0.05)
                    sameAsCorpus = false;
        checkF(! sameAsCorpus, "6. the reference curve is its own answer, not the corpus one (%.0f)", 0.0);
    }

    // ---- 7. Match with nothing loaded aims at Amir's own curve -------------
    {
        const auto take = voice(196.0, 20.0, -6);      // a take that is a little dull
        auto runMatch = [&](int mode, double* before, double* after, std::string* name) {
            aeq::RestoreCore core;
            core.prepare(kSr, 256);
            aeq::RestoreSettings s;
            s.learn = true;
            s.settleSeconds = 3600.0;
            s.mode = mode;
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
            core.commit();
            *before = core.state().gapBeforeDb;
            *after  = core.state().gapAfterDb;
            *name   = core.targetName();
            return core.state().corrective;
        };

        double b = 0.0, a = 0.0; std::string name;
        const auto bands = runMatch(1, &b, &a, &name);
        std::printf("  Match on this take: target '%s', %.2f dB -> %.2f dB\n",
                    name.c_str(), b, a);
        checkF(name == "AMIR 15", "7. Match with nothing loaded names Amir's curve (%.0f)", 0.0);
        checkF(b > 0.5, "7. the take is measurably off his curve to begin with (%.2f dB)", b);
        checkF(a < b, "7. the matched curve moves it toward his curve (%.2f -> %.2f dB)", b, a);
        checkF(bands.size() > 0, "7. and produced a curve (%.0f bands)", double(bands.size()));

        // Restore must NOT start chasing his curve: a restore pass is not a match
        double rb = 0.0, ra = 0.0; std::string rname;
        runMatch(0, &rb, &ra, &rname);
        checkF(rname == "13-TAKE CONSENSUS",
               "7. Restore still aims at the family average, unnamed by the panel (%.0f)", 0.0);
    }

    // ---- 8. a curve compared with itself is zero, at every width -----------------
    // The invariant behind the comparison width: whatever smoothing the engine applies, it
    // must apply the same thing to both sides.  When the target was normalised to 1 kHz
    // *before* smoothing and the measurement *after* it, a take matched against its own
    // profile came out 13.20 dB apart - a number the solver then tried to close with EQ.
    {
        const auto take = voice(196.0, 12.0, 0);
        const auto self = capturedFrom(take, "same take");
        for (double width : {0.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0}) {
            aeq::RestoreCore core;
            core.prepare(kSr, 256);
            aeq::RestoreSettings s;
            s.learn = true;
            s.settleSeconds = 3600.0;
            s.mode = 1;                       // Match
            s.compareWidthOct = width;
            core.setSettings(s);
            core.setReferences({self}, {1.0});
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
            const double gap = core.state().gapBeforeDb;
            checkF(gap < 0.6,
                   "8. the same take against its own profile is %.2f dB apart at width %.3f",
                   gap, width);
        }
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
