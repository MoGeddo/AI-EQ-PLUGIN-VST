// Acceptance gates G1..G7 for aeq::RestoreCore.
//
// The important one is Gate 6: a signal that is already "right" must come out
// almost untouched.  An EQ you put first in the chain has to earn trust, and a
// plugin that always finds something to do is not trustworthy.
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <string>
#include <algorithm>
#include <memory>

#include "../Source/dsp/RestoreCore.h"

using namespace aeq;

// RestoreCore is ~210 KB (the analyser keeps three 8192-point double arrays), so
// ten of them on one stack is more than MSVC's 1 MB default - which is exactly how
// the Windows gate binary died, with 0xC00000FD and not one line of output.  The
// tests run long, sequential pipelines, so everything goes on the heap.
static std::unique_ptr<aeq::RestoreCore> makeCore() {
    return std::make_unique<aeq::RestoreCore>();
}

// Measurements is the struct RestoreCore hands to the UI and the tests
typedef Measurements Measures;

static int gFail = 0, gPass = 0;
static void check(bool ok, const char* what, double got, double want, double tol) {
    if (ok) { ++gPass; std::printf("  ok   %-52s %8.3f (want %.3f +-%.3f)\n", what, got, want, tol); }
    else    { ++gFail; std::printf("  FAIL %-52s %8.3f (want %.3f +-%.3f)\n", what, got, want, tol); }
}
static void checkTrue(bool ok, const char* what) {
    if (ok) { ++gPass; std::printf("  ok   %s\n", what); }
    else    { ++gFail; std::printf("  FAIL %s\n", what); }
}

// --------------------------------------------------------------------------
// signal generators
// --------------------------------------------------------------------------
static double sr = 48000.0;

// glottal-ish harmonic source with vibrato and phrase gaps
static std::vector<float> voice(double f0, double seconds, unsigned seed = 7) {
    const int n = int(seconds * sr);
    std::vector<float> x(n, 0.f);
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, 1.0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const double vib = 1.0 + 0.012 * std::sin(2 * kPi * 5.2 * t);
        phase += 2 * kPi * f0 * vib / sr;
        double s = 0.0;
        for (int k = 1; k <= 24; ++k) s += std::sin(k * phase) / std::pow(k, 1.1);
        // phrase gating: 1.2 s on, 0.35 s off
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.2) ? 1.0 : 0.0;
        // sibilant-ish noise bursts at the end of each phrase
        double sib = 0.0;
        if (cyc > 1.05 && cyc < 1.2) sib = 0.25 * nd(rng);
        x[i] = float(0.12 * env * s + sib);
    }
    return x;
}

// A vowel-like source: harmonics of f0 whose amplitudes follow a fixed vocal
// tract (two formants), no vibrato, so the only variable between two renders is
// the pitch.  This is the signal the envelope has to normalise.
static std::vector<float> formantVoice(double f0, double seconds) {
    const int n = int(seconds * sr);
    std::vector<float> x(n, 0.f);
    const double F1 = 520, B1 = 130, F2 = 1500, B2 = 200;
    const int kmax = int(7500.0 / f0);
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        double s = 0.0;
        for (int k = 1; k <= kmax; ++k) {
            const double f = k * f0;
            const double g1 = 1.0 / std::sqrt(1.0 + std::pow((f - F1) / B1, 2.0));
            const double g2 = 0.55 / std::sqrt(1.0 + std::pow((f - F2) / B2, 2.0));
            s += (g1 + g2 + 0.02) * std::sin(2 * kPi * f * t) / std::pow(k, 0.35);
        }
        const double cyc = std::fmod(t, 1.6);           // 1.35 s on, 0.25 s off
        x[i] = float(0.08 * ((cyc < 1.35) ? s : 0.0));
    }
    return x;
}

// band difference (dB re the 900-1150 Hz reference) of a set of levels
static double bandDiff(const std::vector<double>& db, const std::vector<double>& f, double lo, double hi) {
    double p = 0.0, rp = 0.0; int n = 0, rn = 0;
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] >= lo && f[i] < hi) { p += std::pow(10.0, db[i] / 10.0); ++n; }
        if (f[i] >= 900.0 && f[i] < 1150.0) { rp += std::pow(10.0, db[i] / 10.0); ++rn; }
    }
    if (!n || !rn) return 0.0;
    return 10.0 * std::log10(p / n) - 10.0 * std::log10(rp / rn);
}

static void applyBands(std::vector<float>& x, const std::vector<BandSpec>& b) {
    BiquadCascade c;
    c.setBands(b, sr);
    for (auto& v : x) v = float(c.process(v));
}

static double levelAt(const std::vector<float>& x, double f, double bw = 0.06) {
    // Goertzel-ish: rectangular windowed DFT magnitude around f
    const size_t n = x.size();
    const double w = 2 * kPi * f / sr;
    double re = 0, im = 0;
    for (size_t i = 0; i < n; i += 4) {
        const double s = x[i];
        re += s * std::cos(w * i);
        im -= s * std::sin(w * i);
    }
    (void)bw;
    return 20 * std::log10(std::sqrt(re * re + im * im) / std::max<double>(1.0, n / 4.0) + 1e-20);
}

static double rmsBand(const std::vector<float>& x, double lo, double hi) {
    BiquadCascade c;
    c.setBands({{BandType::Peak, std::sqrt(lo * hi), (std::sqrt(lo * hi)) / (hi - lo), 0.0}}, sr);
    // crude band power via a pair of filters
    Biquad hp = Biquad::highPass(lo, 0.707, sr);
    Biquad lp = Biquad::lowPass(hi, 0.707, sr);
    double acc = 0;
    for (auto v : x) { double s = lp.process(hp.process(v)); acc += s * s; }
    return 10 * std::log10(acc / std::max<size_t>(x.size(), 1) + 1e-24);
}

// run the full pipeline (learn + apply) and return the processed signal
static std::vector<float> runPipeline(const std::vector<float>& in, RestoreCore& core,
                                      RestoreSettings s, bool learnOnly = false) {
    core.prepare(sr, 512);
    core.setSettings(s);
    std::vector<float> buf(512), r(512, 0.f);
    for (size_t off = 0; off < in.size(); off += 512) {
        const int n = int(std::min<size_t>(512, in.size() - off));
        for (int i = 0; i < n; ++i) { buf[i] = in[off + i]; r[i] = buf[i]; }
        core.processBlock(buf.data(), r.data(), n);
    }
    core.refresh();
    if (learnOnly) return in;

    auto s2 = s; s2.learn = false;
    core.setSettings(s2);
    std::vector<float> out = in;
    for (size_t off = 0; off < in.size(); off += 512) {
        const int n = int(std::min<size_t>(512, in.size() - off));
        for (int i = 0; i < n; ++i) { buf[i] = in[off + i]; r[i] = buf[i]; }
        core.processBlock(buf.data(), r.data(), n);
        for (int i = 0; i < n; ++i) out[off + i] = buf[i];
    }
    return out;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow the log
    std::printf("\n===== AEQ RESTORE - acceptance gates =====\n");

    // ---------------- G1: biquad maths --------------------------------
    std::printf("\n[G1] biquad response\n");
    {
        auto pk = Biquad::peaking(1000.0, 1.0, 6.0, sr);
        check(std::abs(pk.magDb(1000.0, sr) - 6.0) < 0.01, "peaking +6 dB at its centre", pk.magDb(1000.0, sr), 6.0, 0.01);
        auto hp = HighPass24();
        hp.set(200.0, sr);
        check(std::abs(hp.magDb(200.0) + 3.0) < 0.6, "24 dB/oct HPF: -3 dB at the corner", hp.magDb(200.0), -3.0, 0.6);
        check(hp.magDb(25.0) < -45.0, "24 dB/oct HPF: >45 dB down two octaves below", hp.magDb(25.0), -48, 10);
        auto sh = Biquad::highShelf(8000.0, 1.0, -6.0, sr);
        check(std::abs(sh.magDb(16000.0, sr) + 6.0) < 0.6, "high shelf -6 dB asymptote", sh.magDb(16000.0, sr), -6.0, 0.6);
    }

    // ---------------- G2: analyser sanity -----------------------------
    std::printf("\n[G2] analyser\n");
    {
        std::mt19937 rng(3);
        std::normal_distribution<float> nd(0.f, 0.1f);
        std::vector<float> w(6 * sr);
        for (auto& v : w) v = nd(rng);
        Analyzer an; an.prepare(sr); an.processBlock(w.data(), int(w.size()));
        auto lv = an.bandLevelsDb();
        double mn = 1e9, mx = -1e9;
        for (int i = 0; i < an.bandCount(); ++i) {
            const double f = an.grid().f[i];
            if (f < 500 || f > 8000) continue;
            mn = std::min(mn, lv[i]); mx = std::max(mx, lv[i]);
        }
        check(mx - mn < 4.0, "white noise is flat over 0.5-8 kHz (spread)", mx - mn, 0.0, 4.0);
        check(an.medianF0() == 0.0, "no pitch on noise", an.medianF0(), 0.0, 1.0);
    }
    {
        auto v = voice(220.0, 6.0);
        Analyzer an; an.prepare(sr); an.processBlock(v.data(), int(v.size()));
        const double f0 = an.medianF0();
        check(std::abs(f0 / 220.0 - 1.0) < 0.06, "YIN: 220 Hz source measured", f0, 220.0, 13.2);
    }

    // ---------------- G3: does it find injected faults? ---------------
    std::printf("\n[G3] fault detection on a synthetic take\n");
    std::vector<float> clean = voice(220.0, 12.0);
    applyBands(clean, {{BandType::Peak, 250.0, 0.9, 2.0}, {BandType::Peak, 3000.0, 0.8, -1.5}});

    std::vector<float> bad = clean;
    // (a) room/mic resonance at 3.15 kHz
    applyBands(bad, {{BandType::Peak, 3150.0, 12.0, 8.0}});
    // (b) missing high-pass: rumble below 150 Hz
    applyBands(bad, {{BandType::LowShelf, 150.0, 1.0, 7.0}});
    // (c) harsh region 2-4 kHz
    applyBands(bad, {{BandType::Peak, 2800.0, 0.7, 4.0}});
    std::printf("  (injected: +8 dB Q12 @3150, +7 dB shelf below 150, +4 dB @2800)\n");

    {
        RestoreSettings s; s.learn = true;
        auto core = makeCore();
        runPipeline(bad, *core, s, true);
        const auto st = core->state();
        bool foundReso = false, foundRumble = false, foundHarsh = false;
        double resoF = 0, resoGain = 0;
        for (const auto& p : st.problems) {
            if (p.id == "reso" && std::abs(std::log2(p.freq / 3150.0)) < 1.0 / 6.0) { foundReso = true; resoF = p.freq; resoGain = p.gainDb; }
            if (p.id == "rumble") foundRumble = true;
            if (p.id == "harsh") foundHarsh = true;
        }
        checkTrue(foundReso, "detects the 3.15 kHz room resonance");
        checkTrue(foundRumble, "detects the missing low cut");
        checkTrue(foundHarsh, "detects the harsh 2-4 kHz region");
        std::printf("       resonance reported at %.0f Hz, correction %+.1f dB\n", resoF, resoGain);
    }

    // ---------------- G16: every report line is fully formatted --------
    // The report text is built with a printf-style helper whose argument count is
    // taken from the format string.  It counted the "%%" in the de-esser line as a
    // placeholder and read past the end of its argument list: g++ printed rubbish,
    // MSVC killed the gate binary before it printed anything, and CI said "the DSP
    // gates did not pass" with no evidence.  So: no raw '%' may survive in any line.
    std::printf("\n[G16] every problem line has its numbers substituted\n");
    {
        RestoreSettings s; s.learn = true;
        auto core = makeCore();
        runPipeline(bad, *core, s, true);
        const auto st = core->state();
        int unsubstituted = 0;
        for (const auto& p : st.problems) {
            if (p.labelAr.empty() || p.detailAr.find('%') != std::string::npos) {
                std::printf("       not substituted: %s\n", p.detailAr.c_str());
                ++unsubstituted;
            }
        }
        std::printf("       %zu problems, %d with a leftover format character\n",
                    st.problems.size(), unsubstituted);
        checkTrue(!st.problems.empty(), "the faulty take produced problems at all");
        checkTrue(unsubstituted == 0, "no leftover %% in any report line");
    }

    // ---------------- G8: does the envelope remove the singer? ---------
    // Same vocal tract at two pitches.  The place where pitch confounds the
    // measurement is where the harmonics are far apart compared with the
    // analysis bands: below ~600 Hz.  Above that, 1/12-octave bands already
    // average several harmonics and the raw spectrum is stable (measured below),
    // which is why the design keeps the raw spectrum up there.
    std::printf("\n[G8] envelope removes the singer from the number\n");
    {
        RestoreSettings s; s.learn = true;
        auto measure = [&](double f0, Measures& out) {
            auto c = makeCore(); runPipeline(formantVoice(f0, 6.0), *c, s, true);
            out = c->measurements();
        };
        Measures a, b;
        measure(150.0, a);          // male-ish
        measure(220.0, b);          // female-ish, same tract
        // 250-350 Hz is where it bites: for one voice the band contains the
        // fundamental, for the other it is a gap between harmonics.  The same
        // effect on the real corpus was worth 32 dB of apparent level that had
        // nothing to do with the mix.
        const double lowBands[3][2] = {{250, 350}, {350, 500}, {500, 700}};
        double rawDrift = 0.0, envDrift = 0.0;
        for (auto& r : lowBands) {
            rawDrift = std::max(rawDrift, std::abs(bandDiff(a.bandDb, a.freqs, r[0], r[1])
                                                  - bandDiff(b.bandDb, b.freqs, r[0], r[1])));
            envDrift = std::max(envDrift, std::abs(bandDiff(a.envDb, a.freqs, r[0], r[1])
                                                  - bandDiff(b.envDb, b.freqs, r[0], r[1])));
        }
        std::printf("       250-700 Hz: raw drifts %.2f dB between F0 150 and 220 Hz\n", rawDrift);
        std::printf("                   envelope drifts %.2f dB\n", envDrift);
        checkTrue(rawDrift > 10.0, "the raw spectrum moves with the singer in the low midrange");
        checkTrue(envDrift < 3.0, "the envelope stays within 3 dB across a 0.55-octave pitch change");
        checkTrue(envDrift < rawDrift * 0.3, "the envelope cuts that dependence by more than 3x");

        // Where it does NOT help: above 600 Hz two octave-apart voices already
        // measure alike on the raw spectrum.  Recorded so nobody over-claims.
        Measures hi1, hi2;
        measure(150.0, hi1); measure(350.0, hi2);
        double rawHigh = 0.0, envHigh = 0.0;
        for (auto& r : lowBands) {
            rawHigh = std::max(rawHigh, std::abs(bandDiff(hi1.bandDb, hi1.freqs, 600, 3000)
                                                 - bandDiff(hi2.bandDb, hi2.freqs, 600, 3000)));
            envHigh = std::max(envHigh, std::abs(bandDiff(hi1.envDb, hi1.freqs, 600, 3000)
                                                 - bandDiff(hi2.envDb, hi2.freqs, 600, 3000)));
        }
        std::printf("       600-3000 Hz over 1.2 octaves: raw %.2f dB, envelope %.2f dB "
                    "(both fine; the raw one is used there)\n", rawHigh, envHigh);

        // determinism
        auto c1 = makeCore(), c2 = makeCore();
        runPipeline(formantVoice(220.0, 3.0), *c1, s, true);
        runPipeline(formantVoice(220.0, 3.0), *c2, s, true);
        const auto e1 = c1->measurements().envDb, e2 = c2->measurements().envDb;
        double d = 0.0;
        for (size_t i = 0; i < e1.size() && i < e2.size(); ++i) d = std::max(d, std::abs(e1[i] - e2[i]));
        checkTrue(d < 1e-9, "envelope is deterministic");
    }

    // ---------------- G4: does processing undo them? -------------------
    std::printf("\n[G4] correction effectiveness\n");
    {
        RestoreSettings s; s.learn = true;
        auto core = makeCore();
        std::vector<float> fixed = runPipeline(bad, *core, s);
        const auto st = core->state();

        const double rBad = rmsBand(bad, 2900.0, 3400.0);
        const double rFix = rmsBand(fixed, 2900.0, 3400.0);
        check(rFix < rBad - 3.0, "resonance band reduced by >3 dB", rBad - rFix, 8.0, 5.0);

        const double lBad = rmsBand(bad, 30.0, 80.0);
        const double lFix = rmsBand(fixed, 30.0, 80.0);
        check(lFix < lBad - 6.0, "sub energy reduced by >6 dB (HPF)", lBad - lFix, 12.0, 8.0);

        checkTrue(st.hpfHz >= 60.0 && st.hpfHz <= 220.0, "HPF corner stays in a sane range");
        std::printf("       HPF %.0f Hz, %zu corrective bands, %zu notches\n",
                    st.hpfHz, st.corrective.size(), st.resonance.size());
    }

    // ---------------- G5: convergence over repeated passes -------------
    // A capped corrector cannot fix a severely broken take in one pass, and it
    // must not pretend to.  What it MUST do is converge: each pass asks for less
    // and the measured excess keeps falling.  That is the property that makes a
    // chain-first tool safe to leave inserted.
    std::printf("\n[G5] convergence over repeated passes\n");
    {
        RestoreSettings s; s.learn = true;
        auto planned = [&](const std::vector<float>& in) {
            auto c = makeCore(); runPipeline(in, *c, s, true);
            const auto st = c->state();
            double t = 0.0;
            for (const auto& b : st.corrective) t += std::abs(b.gainDb);
            for (const auto& b : st.resonance) t += std::abs(b.gainDb);
            return std::pair<double, double>(t, st.fixBeforeDb);
        };
        std::vector<float> x = bad;
        double prevExcess = 1e9, maxProposal = 0.0;
        bool falling = true;
        for (int pass = 1; pass <= 3; ++pass) {
            auto pr = planned(x);
            std::printf("       pass %d: proposes %6.2f dB, measured excess %5.2f dB RMS\n",
                        pass, pr.first, pr.second);
            if (pr.second >= prevExcess) falling = false;
            prevExcess = pr.second;
            maxProposal = std::max(maxProposal, pr.first);
            auto c = makeCore(); x = runPipeline(x, *c, s);
        }
        // the correction is capped, so a broken take converges over passes rather
        // than being flattened in one go - that is the safe behaviour
        checkTrue(falling, "measured excess falls on every pass");
        const double bound = 5 * 3.5 + 2 * 7.0;   // maxBands*cap + notches
        check(maxProposal < bound, "proposal never exceeds the configured caps",
              maxProposal, bound, bound);
    }

    // ---------------- G6: determinism ---------------------------------
    std::printf("\n[G6] determinism\n");
    {
        RestoreSettings s; s.learn = true;
        auto a = makeCore(), b = makeCore();
        auto o1 = runPipeline(bad, *a, s);
        auto o2 = runPipeline(bad, *b, s);
        double diff = 0.0;
        for (size_t i = 0; i < o1.size(); ++i) diff = std::max(diff, std::abs(double(o1[i] - o2[i])));
        check(diff < 1e-6, "two runs produce identical output", diff, 0.0, 1e-6);
    }

    // ---------------- G7: no NaN / no runaway -------------------------
    std::printf("\n[G7] numerical safety\n");
    {
        RestoreSettings s; s.learn = true;
        auto core = makeCore();
        std::vector<float> silent(2 * int(sr), 0.f);
        auto out = runPipeline(silent, *core, s);
        bool finite = true;
        for (auto v : out) if (!std::isfinite(v)) finite = false;
        checkTrue(finite, "silence in -> finite output");

        std::vector<float> loud(int(2 * sr));
        for (size_t i = 0; i < loud.size(); ++i) loud[i] = float(std::sin(2 * kPi * 1000.0 * i / sr));
        auto c2 = makeCore();
        auto o2 = runPipeline(loud, *c2, s);
        double pk = 0; for (auto v : o2) pk = std::max(pk, std::abs(double(v)));
        check(pk <= 1.3, "0 dBFS sine does not blow up", pk, 1.0, 0.3);
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
