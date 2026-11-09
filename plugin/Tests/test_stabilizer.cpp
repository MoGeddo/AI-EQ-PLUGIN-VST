// test_stabilizer.cpp - the bank of moving cuts, measured.
//
// The claim this file has to earn: the stabiliser holds a take on its own learned
// curve, continuously, and it does it without touching what is already on the curve.
// Every assertion below is a measurement of the output signal, not a check that a
// number was stored.
//
//   1. a signal sitting ON the target passes untouched
//   2. a signal ABOVE the target is pulled back toward it, by the amount it says
//   3. it never boosts: a signal BELOW the target is not lifted
//   4. it is bounded per band (the ceiling is a ceiling)
//   5. it lets go: after the loud part stops, the cut returns to nothing
//   6. it is selective: a tone one octave away from a band is not cut by that band
//   7. silence is silence: no NaN, no self-oscillation, no drift
#include "../Source/dsp/Stabilizer.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int gPass = 0, gFail = 0;
static constexpr double kPi = 3.14159265358979323846;

static void ok(bool pass, const char* what, double a = 0.0, double b = 0.0) {
    std::printf("  %s   ", pass ? "ok  " : "FAIL");
    std::printf(what, a, b);
    std::printf("\n");
    pass ? ++gPass : ++gFail;
}

static double rmsDb(const std::vector<float>& v, double fromSec, double toSec, double sr) {
    const long a = long(fromSec * sr), b = long(std::min<double>(toSec, v.size() / sr) * sr);
    double s = 0.0;
    for (long i = a; i < b; ++i) s += double(v[size_t(i)]) * v[size_t(i)];
    const double n = double(std::max(1L, b - a));
    return 10.0 * std::log10(std::max(s / n, 1e-20));
}

// Levels in this file are RMS dBFS, because that is what the detector and the learned
// target are: an "on the target" tone has to be generated at the same convention the
// band measures, otherwise the test is measuring its own arithmetic.
static std::vector<float> tone(double hz, double sec, double rmsDb, double sr) {
    const double amp = std::pow(10.0, rmsDb / 20.0) * 1.4142135623730951;
    std::vector<float> v(size_t(sec * sr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = float(amp * std::sin(2.0 * kPi * hz * double(i) / sr));
    return v;
}

// a tone that is quiet for 0.5 s, loud for 0.5 s, quiet again - the shape of a note
// that pushes and then settles
static std::vector<float> swell(double hz, double sr, double quietDb, double loudDb) {
    auto v = tone(hz, 1.6, quietDb, sr);
    const size_t a = size_t(0.5 * sr), b = size_t(1.0 * sr);
    const double g = std::pow(10.0, (loudDb - quietDb) / 20.0);
    for (size_t i = a; i < b && i < v.size(); ++i) v[i] = float(double(v[i]) * g);
    return v;
}

static aeq::Stabilizer::Band bandAt(double hz, double targetDb, double maxGr = 3.0) {
    aeq::Stabilizer::Band b;
    b.hz = hz; b.targetDb = targetDb; b.maxGrDb = maxGr; b.q = 2.5;
    return b;
}

int main() {
    std::printf("\n===== AEQ RESTORE - the stabiliser =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0;

    // ---------------------------------------------------- 1. on the target: untouched
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        auto b = bandAt(1000.0, -26.0);        // a take whose 1 kHz sits at -26 dBFS
        s.setBands(&b, 1);

        auto v = tone(1000.0, 1.2, -26.0, sr);
        const double in = rmsDb(v, 0.6, 1.1, sr);
        auto out = v;
        s.process(out.data(), nullptr, int(out.size()));
        const double d = rmsDb(out, 0.6, 1.1, sr) - in;
        ok(std::abs(d) < 0.30, "a signal on the learned level passes untouched (%.2f dB)", d);

        // and the band is not silently cutting while it reports zero
        ok(std::abs(s.gainDb(0)) < 0.30, "and it reports no reduction (%.2f dB)", s.gainDb(0));
    }

    // -------------------------------------------------- 2. above the target: cut back
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        auto b = bandAt(1000.0, -26.0, 6.0);        // room to move, so the ratio decides
        s.setBands(&b, 1);

        // 5 dB over the take, a 2 dB dead band, a 0.6 ratio: the band should cut 1.8 dB.
        // The number matters - it is the difference between a stabiliser and a compressor
        // with a spectrum, and asserting only "it went down" would accept both.
        auto v = swell(1000.0, sr, -26.0, -21.0);
        auto out = v;
        s.process(out.data(), nullptr, int(out.size()));

        const double quiet = rmsDb(out, 0.20, 0.45, sr) - rmsDb(v, 0.20, 0.45, sr);
        const double loud  = rmsDb(out, 0.80, 0.95, sr) - rmsDb(v, 0.80, 0.95, sr);
        ok(std::abs(quiet) < 1.0, "the quiet part of the same take stays put (%.2f dB)", quiet);
        ok(loud < -1.2, "the loud part is pulled back (%.2f dB)", loud);
        ok(loud > -2.6, "by the ratio, not by the ceiling (%.2f dB, expect -1.8)", loud);
    }

    // ------------------------------------------------------------ 3. never a boost
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        auto b = bandAt(1000.0, -26.0);
        s.setBands(&b, 1);
        auto v = tone(1000.0, 1.0, -40.0, sr);       // 14 dB *below* the target
        auto out = v;
        s.process(out.data(), nullptr, int(out.size()));
        const double d = rmsDb(out, 0.4, 0.95, sr) - rmsDb(v, 0.4, 0.95, sr);
        ok(d > -0.15 && d < 0.15, "a quiet signal is not lifted (%.2f dB)", d);
    }

    // -------------------------------------------------------------- 4. the ceiling
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        auto b = bandAt(1000.0, -40.0, 3.0);         // 3 dB is all this band may do
        s.setBands(&b, 1);
        auto v = tone(1000.0, 1.2, -6.0, sr);        // 34 dB over the target
        auto out = v;
        s.process(out.data(), nullptr, int(out.size()));
        const double d = rmsDb(out, 0.8, 1.15, sr) - rmsDb(v, 0.8, 1.15, sr);
        ok(d >= -3.4 && d <= -2.6, "the per-band ceiling holds (%.2f dB of 3.0)", d);
        ok(s.totalGainDb() >= -6.0, "the total stays inside 6 dB (%.2f dB)", s.totalGainDb());
    }

    // ------------------------------------------------------------- 5. it lets go
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        auto b = bandAt(1000.0, -26.0);
        s.setBands(&b, 1);
        auto v = swell(1000.0, sr, -26.0, -8.0);
        auto out = v;
        s.process(out.data(), nullptr, int(out.size()));
        const double late = rmsDb(out, 1.25, 1.55, sr) - rmsDb(v, 1.25, 1.55, sr);
        ok(std::abs(late) < 0.6, "after the loud part it returns to nothing (%.2f dB)", late);
        ok(std::abs(s.gainDb(0)) < 0.6, "and the band reports it (%.2f dB)", s.gainDb(0));
    }

    // ------------------------------------------------------------ 6. it is selective
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        aeq::Stabilizer::Band bands[2] = { bandAt(1000.0, -26.0), bandAt(2000.0, -26.0) };
        s.setBands(bands, 2);
        auto v = tone(1000.0, 1.2, -8.0, sr);        // loud at 1 kHz only
        auto out = v;
        s.process(out.data(), nullptr, int(out.size()));
        ok(s.gainDb(0) < -2.0, "the band at the tone engages (%.2f dB)", s.gainDb(0));
        ok(s.gainDb(1) > -0.8, "the band an octave away does not (%.2f dB)", s.gainDb(1));
        const double d = rmsDb(out, 0.8, 1.15, sr) - rmsDb(v, 0.8, 1.15, sr);
        ok(d < -2.0 && d > -4.0, "and the cut is where the tone is (%.2f dB)", d);
    }

    // ---------------------------------------------------------------- 7. silence
    {
        aeq::Stabilizer s;
        s.prepare(sr);
        auto b = bandAt(600.0, -26.0);
        s.setBands(&b, 1);
        std::vector<float> z(size_t(sr * 2), 0.0f);
        s.process(z.data(), nullptr, int(z.size()));
        double worst = 0.0;
        bool finite = true;
        for (float x : z) {
            worst = std::max(worst, std::abs(double(x)));
            if (! std::isfinite(x)) finite = false;
        }
        ok(finite && worst == 0.0, "silence stays silence, exactly (%.2e)", worst);
        ok(std::abs(s.gainDb(0)) < 0.01, "and nothing self-oscillates (%.2f dB)", s.gainDb(0));
    }

    // --------------------------------------------------- 8. the grid is the product's
    {
        double hz[aeq::Stabilizer::kMaxBands] = {};
        const int n = aeq::Stabilizer::grid(hz, aeq::Stabilizer::kMaxBands);
        bool rising = true;
        for (int i = 1; i < n; ++i) if (hz[i] <= hz[i - 1]) rising = false;
        ok(n == 10 && rising && std::abs(hz[0] - 150.0) < 1.0,
           "the grid is 10 rising bands from %.0f Hz", hz[0], hz[n - 1]);
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
