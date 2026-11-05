// test_tracked.cpp - the band that locks onto the note and holds it at a level.
//
// "مهم جدا بالنسبالي اوي" - the one from the four reference plugins that matters most:
// a band that finds its frequency in the voice, follows it while the note moves, and
// keeps that part of the voice at a level instead of at a fixed dB.  Antares does the
// following, Ozone's stabiliser does the holding; nobody's take on it is free of the
// two failure modes below, and both of those are what this file measures:
//
//   WATERY  a tracked band that recomputes its coefficients per block on unchanged
//           filter state punches a step into the audio each time the note moves.  It
//           is the classic sound of a pitch-following EQ done wrong.  Measured as the
//           worst sample-to-sample step in the output against the worst step the
//           input itself contains: the output may not make a step the signal cannot.
//
//   CHIRP   a pitch tracker drops an octave for a frame (period doubling is a real
//           ambiguity in YIN), and a band that believes it slams an octave away and
//           back inside 100 ms.  Measured: a one-frame octave report may move the
//           centre by less than 5 %.
//
// and the behaviours that make it worth having:
//
//   FOLLOW  110 Hz -> 220 Hz over four seconds: the centre stays within 3 % of f0.
//   HOLD    a note that gets 12 dB louder is held within 1 dB of where it was, and the
//           time it takes is reported (the number that decides attack/release).
//   RANGE   with 3 dB of range allowed, a 20 dB jump is only corrected by ~3 dB - the
//           band never exceeds the range it was given.
//   QUIET   below -75 dBFS it stops acting, so a gap between words is not EQ'd.
//   BREATH  unvoiced for a quarter second -> gain returns to 0 dB; consonants are not
//           treated as a note.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_tracked.cpp -o build/aeq_tracked
//   ./build/aeq_tracked            # exits 0 when every line says ok
//
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "../Source/dsp/DynamicProfile.h"
#include "../Source/dsp/TrackedBand.h"

// ---------------------------------------------------------------- allocation spy
// Same trick as test_smooth: only the thread that says it is inside the block is
// counted.  A tracked band that allocated per block would break the audio thread.
static thread_local bool t_track = false;
static thread_local long t_allocs = 0;
void* operator new(std::size_t n) {
    if (t_track) ++t_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int gPass = 0, gFail = 0;
static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;

static void ok(bool pass, const char* fmt, ...) {
    std::printf("  %s   ", pass ? "ok  " : "FAIL");
    va_list a; va_start(a, fmt); std::vprintf(fmt, a); va_end(a);
    std::printf("\n");
    if (pass) ++gPass; else ++gFail;
}

// A sung note: f0 (optionally gliding), 12 harmonics at 1/k^1.1, a 5 Hz vibrato, and
// a small amount of breath so the band has something realistic to sit on.
static std::vector<float> note(double f0Start, double f0End, double sec, double gain = 0.1) {
    const long n = long(sec * kSr);
    std::vector<float> v(static_cast<size_t>(n));
    unsigned seed = 12345;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return double((seed >> 16) & 0x7fff) / 16384.0 - 1.0; };
    double phase = 0.0;
    for (long i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double u = sec > 0 ? t / sec : 0.0;
        const double f0 = f0Start * std::pow(f0End / f0Start, u);           // exponential glide
        const double vib = 1.0 + 0.010 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double x = 0.0;
        for (int k = 1; k <= 12; ++k) x += std::sin(k * phase) / std::pow(double(k), 1.1);
        v[size_t(i)] = float(gain * (x / 1.6 + 0.004 * rnd()));
    }
    return v;
}

static double rmsDb(const std::vector<float>& v, long a, long b) {
    double s = 0.0;
    long n = 0;
    for (long i = a; i < b && i < long(v.size()); ++i) { s += double(v[size_t(i)]) * double(v[size_t(i)]); ++n; }
    return 20.0 * std::log10(std::max(1e-12, std::sqrt(s / std::max(1L, n))));
}

static double maxStep(const std::vector<float>& v, long a, long b) {
    double m = 0.0;
    for (long i = std::max(1L, a + 1); i < b && i < long(v.size()); ++i)
        m = std::max(m, double(std::abs(v[size_t(i)] - v[size_t(i - 1)])));
    return m;
}

int main() {
    std::printf("\n===== AEQ RESTORE - the band that follows the note =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // ------------------------------------------------ the bell is really dB
    {
        aeq::TrackedBand b;
        b.prepare(kSr);
        aeq::TrackedBand::Tuning t;
        t.mode = aeq::TrackedBand::Mode::Fixed;
        t.fixedHz = 1000.0; t.q = 6.0;
        t.rangeDown = 0.0; t.rangeUp = 0.0;
        t.targetDb = -90.0;                       // target below the signal: force full cut
        t.releaseMs = 5.0; t.attackMs = 5.0; t.holdOffSec = 0.0;
        b.setTuning(t);

        // a steady tone exactly at the requested centre, 0.5 s to settle
        std::vector<float> v(static_cast<size_t>(kSr * 1.2));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = float(0.05 * std::sin(2.0 * kPi * 1000.0 * i / kSr));
        const double inRms = rmsDb(v, long(kSr * 0.8), long(kSr * 1.15));
        b.process(v.data(), nullptr, int(v.size()));
        const double outRms = rmsDb(v, long(kSr * 0.8), long(kSr * 1.15));
        const double cut = outRms - inRms;
        // rangeDown is 0 here, so nothing may happen at all - proof the range is a range
        ok(std::abs(cut) < 0.02, "range 0 dB is a real limit: a -12 dB request moved %.2f dB", cut);

        t.rangeDown = 12.0;
        b.setTuning(t);
        b.prepare(kSr);
        b.setTuning(t);
        std::vector<float> w(static_cast<size_t>(kSr * 1.2));
        for (size_t i = 0; i < w.size(); ++i)
            w[i] = float(0.05 * std::sin(2.0 * kPi * 1000.0 * i / kSr));
        const double in2 = rmsDb(w, long(kSr * 0.8), long(kSr * 1.15));
        b.process(w.data(), nullptr, int(w.size()));
        const double out2 = rmsDb(w, long(kSr * 0.8), long(kSr * 1.15));
        ok(std::abs((out2 - in2) + 12.0) < 0.35,
           "requested -12 dB at the centre, measured %.2f dB", out2 - in2);
    }

    // ------------------------------------------------ FOLLOW: a four-second glide
    {
        const double sec = 4.0;
        auto take = note(110.0, 220.0, sec);
        auto out = take;

        aeq::TrackedBand b;
        b.prepare(kSr);
        aeq::TrackedBand::Tuning t;
        t.mode = aeq::TrackedBand::Mode::Pitch;
        t.harmonic = 1; t.q = 5.0;
        t.targetDb = -26.0; t.rangeDown = 6.0; t.rangeUp = 0.0;
        b.setTuning(t);

        // feed pitch the way the processor does, once per 64-sample control block
        const int blk = 64;
        double worstErr = 0.0;
        for (long i = 0; i + blk <= long(out.size()); i += blk) {
            const double u = double(i) / out.size();
            const double f0 = 110.0 * std::pow(2.0, u);
            b.setPitch(f0, true);
            b.process(out.data() + i, nullptr, blk);
            if (i > long(kSr * 0.2)) {
                const double got = b.centreHz();
                worstErr = std::max(worstErr, std::abs(std::log2(got / f0)));
            }
        }
        ok(worstErr * 1200.0 < 36.0,           // 36 cents = 2.1 %
           "centre follows 110 -> 220 Hz within %.0f cents (limit 36)", worstErr * 1200.0);
        ok(std::abs(b.centreHz() - 220.0) < 8.0,
           "ends on the note it ended on: %.1f Hz (truth 220.0)", b.centreHz());

        const double inStep = maxStep(take, long(kSr * 0.2), long(take.size()));
        const double outStep = maxStep(out, long(kSr * 0.2), long(out.size()));
        ok(outStep <= inStep * 1.10 + 1e-4,
           "no click while moving: worst step %.5f against the signal's own %.5f", outStep, inStep);
        ok(b.gainDb() <= 0.001, "never boosts past 0 dB with rangeUp = 0 (%.3f dB)", b.gainDb());
    }

    // ------------------------------------------------ HOLD: a note gets 12 dB louder
    {
        const long n = long(kSr * 2.0);
        std::vector<float> in(static_cast<size_t>(n));
        double phase = 0.0;
        for (long i = 0; i < n; ++i) {
            const double t = i / kSr;
            const double amp = (t >= 1.0 && t < 1.6) ? 0.20 : 0.05;     // +12.0 dB
            phase += 2.0 * kPi * 942.0 / kSr;
            in[size_t(i)] = float(amp * std::sin(phase));
        }
        auto out = in;

        aeq::TrackedBand b;
        b.prepare(kSr);
        aeq::TrackedBand::Tuning t;
        t.mode = aeq::TrackedBand::Mode::Fixed;
        t.fixedHz = 942.0; t.q = 6.0;
        t.targetDb = rmsDb(in, long(kSr * 0.4), long(kSr * 0.9));      // hold the quiet level
        t.rangeDown = 15.0; t.rangeUp = 0.0;
        t.attackMs = 6.0; t.releaseMs = 140.0;
        t.holdOffSec = 0.0;
        b.setTuning(t);

        const int blk = 64;
        long settleAt = -1;
        for (long i = 0; i + blk <= n; i += blk) {
            b.setPitch(942.0, true);                   // fixed mode ignores it, kept honest
            b.process(out.data() + i, nullptr, blk);
            if (i >= long(kSr * 1.0) && settleAt < 0 && b.gainDb() <= -10.9) settleAt = i;
        }
        const double quiet = rmsDb(out, long(kSr * 0.4), long(kSr * 0.9));
        const double loud = rmsDb(out, long(kSr * 1.3), long(kSr * 1.55));
        const double inQuiet = rmsDb(in, long(kSr * 0.4), long(kSr * 0.9));
        const double inLoud = rmsDb(in, long(kSr * 1.3), long(kSr * 1.55));

        ok(inLoud - inQuiet > 11.9, "the input really did jump: %.2f dB", inLoud - inQuiet);
        ok(std::abs(loud - quiet) < 1.0,
           "output level held: %.2f dB instead of %.2f (moved %.2f dB)", loud, quiet, loud - quiet);
        ok(settleAt > 0 && (settleAt - long(kSr)) < long(kSr * 0.05),
           "settled in %.0f ms (limit 50)",
           settleAt > 0 ? 1000.0 * double(settleAt - long(kSr)) / kSr : -1.0);
        ok(b.gainDb() < -1.0, "and it is cutting while the note is loud (%.2f dB)", b.gainDb());
    }

    // --------------------------- CORPUS SLOPE: the hold tightens with loudness
    // The 15-stem study says the low end barely follows the singer (0.19 dB of band per
    // dB of voice at 100 Hz) while the mids follow at 1.0-1.4.  A band told 0.0 holds the
    // absolute level; a band told 1.0 must do nothing at all, because following loudness
    // is what a dry take already does and the correct action is no action.
    {
        auto drive = [](double slope, double rangeDown) {
            const long n = long(kSr * 1.6);
            std::vector<float> in(static_cast<size_t>(n));
            double phase = 0.0;
            for (long i = 0; i < n; ++i) {
                const double amp = (i > long(kSr * 0.6)) ? 0.158 : 0.05;      // +10.0 dB
                phase += 2.0 * kPi * 100.0 / kSr;
                in[size_t(i)] = float(amp * std::sin(phase));
            }
            auto out = in;
            aeq::TrackedBand b;
            b.prepare(kSr);
            aeq::TrackedBand::Tuning t;
            t.mode = aeq::TrackedBand::Mode::Fixed;
            t.fixedHz = 100.0; t.q = 6.0;
            t.targetDb = rmsDb(in, 0, long(kSr * 0.4));          // hold the quiet level
            t.rangeDown = rangeDown; t.rangeUp = 0.0;
            t.holdOffSec = 0.0; t.deadBandDb = 0.0;
            t.slopeDb = slope;
            t.loudRefDb = -40.0;                                 // the quiet note's loudness
            b.setTuning(t);
            const int blk = 64;
            for (long i = 0; i + blk <= n; i += blk) {
                const double t_ = i / kSr;
                b.setLoudness(-40.0 + (t_ > 0.6 ? 10.0 : 0.0));   // the same +10 dB, as a level
                b.process(out.data() + i, nullptr, blk);
            }
            return b.gainDb();
        };
        const double gFollow = drive(1.0, 24.0);      // "let it follow": no relative move
        const double gHold   = drive(0.0, 24.0);      // "hold it": the jump comes back
        const double gCorpus = drive(0.19, 24.0);     // what the corpus says at 100 Hz
        ok(std::abs(gFollow) < 0.5,
           "a band told 1.0 leaves a louder note alone (%.2f dB)", gFollow);
        ok(gFollow - gHold > 8.0,
           "a band told 0.0 holds it: %.2f dB of a +10 dB note comes back", gFollow - gHold);
        // the two holds differ by exactly slope x 10 dB, which is calibration-free: both
        // are measured against the same reference, so the band's own detector offset
        // cancels and the corpus number is what is left
        ok(std::abs((gCorpus - gHold) - 1.9) < 0.6,
           "and the corpus's 0.19 instead of 0.0 gives %.2f dB less cut (expect 1.9)",
           gCorpus - gHold);
        const double gLimited = drive(0.0, 3.0);
        ok(std::abs(gLimited) < 3.01 && std::abs(gLimited) > 2.5,
           "and the range still wins over the slope (%.2f dB of a 3 dB range)", gLimited);
    }

    // ------------------------------------------------ RANGE: never more than asked
    {
        const long n = long(kSr * 1.5);
        std::vector<float> in(static_cast<size_t>(n));
        double phase = 0.0;
        for (long i = 0; i < n; ++i) {
            const double amp = (i > long(kSr * 0.5)) ? 0.5 : 0.05;       // +20 dB
            phase += 2.0 * kPi * 1500.0 / kSr;
            in[size_t(i)] = float(amp * std::sin(phase));
        }
        auto out = in;
        aeq::TrackedBand b;
        b.prepare(kSr);
        aeq::TrackedBand::Tuning t;
        t.mode = aeq::TrackedBand::Mode::Fixed;
        t.fixedHz = 1500.0; t.q = 4.0;
        t.targetDb = rmsDb(in, 0, long(kSr * 0.4));
        t.rangeDown = 3.0; t.rangeUp = 0.0;
        b.setTuning(t);
        b.process(out.data(), nullptr, int(n));
        const double inQuiet = rmsDb(in, 0, long(kSr * 0.4));
        const double outQuiet = rmsDb(out, 0, long(kSr * 0.4));
        const double inLoud = rmsDb(in, long(kSr * 1.0), long(kSr * 1.4));
        const double outLoud = rmsDb(out, long(kSr * 1.0), long(kSr * 1.4));
        (void)inQuiet;
        ok(std::abs((inLoud - outLoud) - 3.0) < 0.4,
           "range 3 dB: a %.0f dB jump came out %.2f dB lower", inLoud - inQuiet, inLoud - outLoud);
        ok(std::abs(outQuiet - inQuiet) < 0.3,
           "and it did nothing at the level it was told to hold (%.2f dB)", outQuiet - inQuiet);
    }

    // ------------------------------------------------ CHIRP: a one-frame octave drop
    {
        auto take = note(200.0, 200.0, 1.5);
        auto out = take;
        aeq::TrackedBand b;
        b.prepare(kSr);
        aeq::TrackedBand::Tuning t;
        t.mode = aeq::TrackedBand::Mode::Pitch;
        t.harmonic = 1; t.q = 6.0;
        t.targetDb = -30.0; t.rangeDown = 6.0;
        b.setTuning(t);

        const int blk = 64;
        double worstMove = 0.0;
        for (long i = 0; i + blk <= long(out.size()); i += blk) {
            const bool glow = (i >= long(kSr * 0.6) && i < long(kSr * 0.6) + blk);
            b.setPitch(glow ? 100.0 : 200.0, true);           // the tracker's octave error
            b.process(out.data() + i, nullptr, blk);
            if (i > long(kSr * 0.3))
                worstMove = std::max(worstMove, std::abs(std::log2(b.centreHz() / 200.0)));
        }
        ok(worstMove * 1200.0 < 60.0,
           "one frame of 100 Hz moved the centre %.0f cents (limit 60)", worstMove * 1200.0);
        ok(std::abs(b.centreHz() - 200.0) < 6.0, "still on 200 Hz afterwards: %.1f Hz", b.centreHz());

        // ... but a real octave change, held, is taken
        aeq::TrackedBand c;
        c.prepare(kSr);
        t.q = 6.0;
        c.setTuning(t);
        for (int i = 0; i < 40; ++i) { c.setPitch(200.0, true); c.process(out.data(), nullptr, blk); }
        for (int i = 0; i < 120; ++i) { c.setPitch(400.0, true); c.process(out.data(), nullptr, blk); }
        ok(std::abs(c.centreHz() - 400.0) < 8.0,
           "a real octave change is followed (160 ms): %.1f Hz (truth 400)", c.centreHz());
    }

    // ------------------------------------------------ BREATH: unvoiced returns to 0 dB
    {
        const long n = long(kSr * 1.6);
        std::vector<float> in(static_cast<size_t>(n));
        unsigned seed = 99;
        auto rnd = [&] { seed = seed * 1103515245u + 12345u; return double((seed >> 16) & 0x7fff) / 16384.0 - 1.0; };
        double phase = 0.0;
        for (long i = 0; i < n; ++i) {
            const bool sung = i < long(kSr * 0.8);
            const double amp = sung ? 0.20 : 0.02;                 // then a quiet breath
            if (sung) { phase += 2.0 * kPi * 500.0 / kSr; in[size_t(i)] = float(amp * std::sin(phase)); }
            else      { in[size_t(i)] = float(amp * rnd()); }
        }
        auto out = in;
        aeq::TrackedBand b;
        b.prepare(kSr);
        aeq::TrackedBand::Tuning t;
        t.mode = aeq::TrackedBand::Mode::Fixed;
        t.fixedHz = 500.0; t.q = 6.0;
        t.targetDb = rmsDb(in, long(kSr * 0.3), long(kSr * 0.7));  // hold the sung level
        t.rangeDown = 12.0; t.rangeUp = 0.0;
        t.holdOffSec = 0.25;
        b.setTuning(t);

        const int blk = 64;
        for (long i = 0; i + blk <= n; i += blk) {
            const bool voiced = i < long(kSr * 0.8);
            b.setPitch(500.0, voiced);
            b.process(out.data() + i, nullptr, blk);
        }
        ok(std::abs(b.gainDb()) < 0.5,
           "unvoiced for 0.8 s -> gain back to 0 dB (%.2f dB)", b.gainDb());
        const double inTail = rmsDb(in, long(kSr * 1.2), long(kSr * 1.55));
        const double outTail = rmsDb(out, long(kSr * 1.2), long(kSr * 1.55));
        ok(std::abs(outTail - inTail) < 0.4,
           "and the breath passes at its own level: %.2f dB against %.2f", outTail, inTail);
    }

    // ------------------------------------------------ no allocation, stereo, mono
    {
        auto L = note(180.0, 180.0, 0.5);
        auto R = L;
        aeq::TrackedBand b;
        b.prepare(kSr);
        b.setPitch(180.0, true);
        t_allocs = 0; t_track = true;
        for (long i = 0; i + 512 <= long(L.size()); i += 512)
            b.process(L.data() + i, R.data() + i, 512);
        t_track = false;
        ok(t_allocs == 0, "the audio path never touches the heap (%ld allocations)", t_allocs);
        bool finite = true;
        for (float x : L) finite = finite && std::isfinite(x);
        for (float x : R) finite = finite && std::isfinite(x);
        ok(finite, "stereo output stays finite");
    }

    // ------------------------------------------------ the corpus table itself
    {
        ok(std::abs(aeq::corpusSlope(100.0) - 0.19) < 0.08,
           "the corpus table says 100 Hz barely follows loudness (%.2f)", aeq::corpusSlope(100.0));
        ok(aeq::corpusSlope(1000.0) > 1.0,
           "and that 1 kHz follows it more than 1:1 (%.2f)", aeq::corpusSlope(1000.0));
        ok(aeq::corpusCorrectionDb(100.0, 10.0) < -5.0,
           "so a +10 dB note asks the low end to come down %.1f dB",
           aeq::corpusCorrectionDb(100.0, 10.0));
        ok(aeq::corpusCorrectionDb(1000.0, 10.0) == 0.0,
           "while the mids are never expanded (%.1f dB)", aeq::corpusCorrectionDb(1000.0, 10.0));
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
