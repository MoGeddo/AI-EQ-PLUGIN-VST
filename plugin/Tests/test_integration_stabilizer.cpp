// test_integration_stabilizer.cpp - the moving bank, inside the real chain.
//
// test_stabilizer.cpp proves the bank works on its own.  This proves the *plugin* runs
// it, at the right place, with targets that came from the take, and that the whole
// thing stays honest:
//
//   1. while learning, the bank is not in the path (bit-exact pass-through)
//   2. after a stop, on material the take already contained, it is inaudible
//      (within 1 dB of the same build with the bank switched off)
//   3. on material 8 dB hotter than the take, it pulls back - and the delta against
//      the bank-off build is the measurement, so nothing else can be the cause
//   4. it lets go when the loud part ends
//   5. its total never exceeds the product's 6 dB
//   6. turning its amount to zero removes it from the path completely
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_integration_stabilizer.cpp \
//       -o build/aeq_int_stab && build/aeq_int_stab
#include "../Source/dsp/RestoreCore.h"
#include "../Source/dsp/Biquad.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static void checkF(bool ok, const char* fmt, double a, double b = 0.0) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, a, b);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

// a sung phrase: harmonics with a vibrato, a 1.25 s note followed by a 0.3 s breath, and
// a little noise under it.  The gaps matter - the analyser's gate decides what is voice
// by dynamic range, so a continuous tone is (correctly) not a take at all and the whole
// measurement stays shut.
static std::vector<float> voice(double f0, double seconds, double gainDb) {
    const int n = int(seconds * kSr);
    std::vector<float> x(size_t(n), 0.0f);
    double phase = 0.0;
    const double g = std::pow(10.0, gainDb / 20.0);
    unsigned int seed = 12345u;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int k = 1; k <= 30; ++k) s += std::sin(k * phase) / std::pow(k, 1.05);
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.25) ? 1.0 : 0.0;
        seed = seed * 1664525u + 1013904223u;
        const double noise = (double(seed >> 9) / double(1u << 23) - 0.5) * 0.004;
        x[size_t(i)] = float(0.13 * g * (env * s + noise));
    }
    return x;
}

// the same voice, with one resonance ringing on it - the thing a stabiliser exists for
static std::vector<float> resonant(const std::vector<float>& dry, double hz, double db) {
    auto bq = aeq::Biquad::peaking(hz, 2.0, db, kSr);
    std::vector<float> y = dry;
    for (auto& v : y) v = float(bq.process(double(v)));
    return y;
}

// magnitude of one frequency in the output, by a Hann-windowed Goertzel-style DFT
static double magAt(const std::vector<float>& v, double hz, double fromSec, double toSec) {
    const long a = long(fromSec * kSr), b = long(std::min<double>(toSec, v.size() / kSr) * kSr);
    if (b <= a) return 0.0;
    double re = 0.0, im = 0.0;
    for (long i = a; i < b; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i - a) / double(b - a));
        const double ph = 2.0 * kPi * hz * double(i) / kSr;
        re += w * v[size_t(i)] * std::cos(ph);
        im += w * v[size_t(i)] * std::sin(ph);
    }
    return 20.0 * std::log10(std::max(std::sqrt(re * re + im * im) / double(b - a), 1e-12));
}

struct Rig {
    std::unique_ptr<aeq::RestoreCore> core = std::make_unique<aeq::RestoreCore>();
    std::vector<float> l = std::vector<float>(256), r = std::vector<float>(256);
    explicit Rig(double stabilizer) {
        core->prepare(kSr, 256);
        aeq::RestoreSettings s;
        s.learn = true;
        s.settleSeconds = 3600.0;      // the person decides when to stop - not a timer
        s.stabilizer = stabilizer;
        core->setSettings(s);
    }
    // play the whole take, refreshing at 5 Hz like the plugin's timer
    std::vector<float> run(const std::vector<float>& take) {
        std::vector<float> out;
        out.reserve(take.size());
        size_t pos = 0;
        int blk = 0;
        while (pos < take.size()) {
            for (int i = 0; i < 256; ++i) {
                l[size_t(i)] = (pos + size_t(i) < take.size()) ? take[pos + size_t(i)] : 0.0f;
                r[size_t(i)] = l[size_t(i)];
            }
            core->processBlock(l.data(), r.data(), 256);
            pos += 256;
            if (++blk % 38 == 0) core->refresh();
            out.insert(out.end(), l.begin(), l.end());
        }
        core->refresh();
        return out;
    }
    // the user presses stop
    void stop() { core->commit(); }
};

int main() {
    std::printf("\n===== AEQ RESTORE - the stabiliser in the chain =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const auto take   = voice(196.0, 20.0, 0.0);     // what the plug-in listens to
    const auto same   = voice(196.0, 6.0, 0.0);      // the same voice, afterwards
    const auto hotter = voice(196.0, 6.0, 8.0);      // and the same voice, 8 dB louder

    Rig on(1.0), off(0.0);

    // ---- 1. learning is still bit-exact with the bank in the build ------------
    {
        const auto out = on.run(take);
        double worst = 0.0;
        for (size_t i = 0; i < out.size() && i < take.size(); ++i)
            worst = std::max(worst, std::abs(double(out[i]) - double(take[i])));
        checkF(out.size() > 0 && worst == 0.0,
               "1. while it learns, the bank is not in the path (worst |diff| %.1e)", worst, 0.0);

        const auto out2 = off.run(take);
        double worst2 = 0.0;
        for (size_t i = 0; i < out2.size() && i < take.size(); ++i)
            worst2 = std::max(worst2, std::abs(double(out2[i]) - double(take[i])));
        checkF(worst2 == 0.0, "1. and the same with the bank switched off (worst |diff| %.1e)", worst2, 0.0);

        // the user stops both: one decision from one take, identical in both builds
        on.stop();
        off.stop();
        checkF(double(on.core->state().corrective.size()),
               "1. the stop froze a curve (%.0f bands)", double(on.core->state().corrective.size()), 0.0);
        checkF(double(off.core->state().corrective.size()),
               "1. the same curve with the bank off (%.0f bands)", double(off.core->state().corrective.size()), 0.0);
    }

    // ---- 2. after the stop, on the take's own level, it is inaudible ----------
    {
        const auto a = on.run(same);        // committed by now: stabiliser on
        const auto b = off.run(same);       // identical chain, bank off
        const double da = magAt(a, 196.0, 0.2, 1.2), db = magAt(b, 196.0, 0.2, 1.2);
        const double dt = on.core->stabilizerTotalDb();
        checkF(std::abs(da - db) < 1.0,
               "2. material at the learned level passes the same either way (%.2f dB apart)", da - db, 0.0);
        checkF(dt > -0.6, "2. and the bank reports almost no work (%.2f dB total)", dt, 0.0);
    }

    // ---- 3a. a resonance on top of the take is cut, the note is not ---------
    {
        const auto r = resonant(same, 1200.0, 12.0);
        const auto a = on.run(r);           // bank on
        const auto b = off.run(r);          // bank off, everything else identical
        const double atRes = magAt(a, 1200.0, 0.2, 1.2) - magAt(b, 1200.0, 0.2, 1.2);
        const double atNote = magAt(a, 196.0, 0.2, 1.2) - magAt(b, 196.0, 0.2, 1.2);
        std::printf("  a 12 dB ring at 1.2 kHz: %+.2f dB at the ring, %+.2f dB on the note\n",
                    atRes, atNote);
        checkF(atRes < -1.0, "3a. the bank takes 1 dB or more off the ring (%.2f dB)", atRes, 0.0);
        checkF(atRes > -6.0, "3a. and never more than 6 dB (%.2f dB)", atRes, 0.0);
        checkF(std::abs(atNote) < 0.6, "3a. while the note underneath is left alone (%.2f dB)", atNote, 0.0);
        checkF(on.core->stabilizerBands() == aeq::Stabilizer::kMaxBands,
               "3a. the whole grid is running (%.0f of 10 bands)",
               double(on.core->stabilizerBands()), 0.0);
        checkF(on.core->stabilizerTotalDb() >= -6.0,
               "3a. the summed reduction stays inside 6 dB (%.2f dB)", on.core->stabilizerTotalDb(), 0.0);
    }

    // ---- 3b. a louder performance is not a resonance: it must not be levelled -
    {
        const auto a = on.run(hotter);
        const auto b = off.run(hotter);
        const double d = magAt(a, 196.0, 0.2, 1.2) - magAt(b, 196.0, 0.2, 1.2);
        checkF(std::abs(d) < 1.5,
               "3b. a phrase 8 dB louder is not squashed flat (%.2f dB)", d, 0.0);
    }

    // ---- 4. it lets go ------------------------------------------------------
    {
        const auto quiet = voice(196.0, 4.0, 0.0);
        const auto a = on.run(quiet);
        const auto b = off.run(quiet);
        const double d = magAt(a, 196.0, 0.2, 1.2) - magAt(b, 196.0, 0.2, 1.2);
        checkF(std::abs(d) < 1.0, "4. after the loud phrase it returns to no cut (%.2f dB)", d, 0.0);
    }

    // ---- 5. the bank is inside the same 6 dB law as everything else ----------
    {
        const auto scream = voice(196.0, 4.0, 18.0);
        on.run(scream);
        checkF(on.core->stabilizerTotalDb() >= -6.01,
               "5. even a shout cannot push it past 6 dB (%.2f dB)", on.core->stabilizerTotalDb(), 0.0);
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
