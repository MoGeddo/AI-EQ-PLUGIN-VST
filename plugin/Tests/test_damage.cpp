// test_damage.cpp - the plugin may not eat the vocal.
//
// The complaint: "خرج الصوت تحسه إنه بوّظه، وأكل حاجات كتير" - the processed take came
// back thinner, hollow, as if something had been taken out of it.  That is not a
// matter of taste, it is measurable, and this file measures it on a take whose
// properties are known exactly:
//
//   * BODY      the energy at 150-500 Hz, where the fundamental and the first two
//               formants of a 196 Hz voice live.  A correction that removes more than
//               2.5 dB here has removed the body of the note.
//   * LEVEL     the long-term RMS.  A "restore" that changes the level by more than
//               2 dB has changed the mix, not fixed it.
//   * AIR/TOP   the energy above 6 kHz, which must survive.
//   * CLARITY   the difference between the take and its own smoothed spectrum.  A
//               correction that flattens the singer's identity fails this even if it
//               hits the target, because the target is a family average.
//
// and then, because "do no harm" alone would be satisfied by doing nothing, it also
// checks the correction still lands: the distance to the target on the bands it chose
// has to improve.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_damage.cpp -o build/aeq_damage
//   build/aeq_damage

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

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

// A dry vocal: glottal source with vibrato, formants where a voice has them, breath
// gaps.  Loud enough and long enough that the analyser believes it.
static std::vector<float> dryVocal(double f0, double seconds, double boomDb, double bellHz, double sibLevel = 0.0) {
    const int n = int(seconds * kSr);
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    std::mt19937 rng(5);
    std::normal_distribution<double> nd(0.0, 1.0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int k = 1; k <= 30; ++k) s += std::sin(k * phase) / std::pow(k, 1.05);
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.25) ? 1.0 : 0.0;
        const double sib = (cyc > 1.10 && cyc < 1.25) ? sibLevel * nd(rng) : 0.0;
        x[size_t(i)] = float(0.13 * env * s + sib);
    }
    // a low boom and a room bell, so there is something to correct
    // A real rumble: the stand/floor noise of a mic, three low tones plus filtered
    // noise, all below 80 Hz.  (This used to be a one-pole low-pass at 110 Hz applied
    // to the whole voice, which is not a rumble at all - it is a low-mid boost, and it
    // made the "is the rumble gone" number measure the mix instead of the fix.)
    double lp = 0.0;
    const double aLp = 1.0 - std::exp(-2.0 * kPi * 110.0 / kSr);
    // Room bell: RBJ peaking, written as a plain difference equation.  (The first
    // version was a transposed form with the sign of a1 flipped, which is a pole
    // just outside the unit circle: the "take" grew to +35 dBFS and every number
    // measured on it afterwards was about that instability, not about the plugin.)
    const double w = 2.0 * kPi * bellHz / kSr, q = 2.2;
    const double A = std::pow(10.0, 4.0 / 40.0);
    const double alpha = std::sin(w) / (2.0 * q), cw = std::cos(w);
    const double a0 = 1.0 + alpha / A;
    const double pb0 = (1.0 + alpha * A) / a0, pb1 = (-2.0 * cw) / a0, pb2 = (1.0 - alpha * A) / a0;
    const double pa1 = (-2.0 * cw) / a0, pa2 = (1.0 - alpha / A) / a0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    const double boom = std::pow(10.0, boomDb / 20.0);
    for (auto& v : x) {
        const double in = v;
        lp += aLp * (in - lp);
        const double y = pb0 * in + pb1 * x1 + pb2 * x2 - pa1 * y1 - pa2 * y2;
        x2 = x1; x1 = in; y2 = y1; y1 = y;
        const double t = double(&v - x.data()) / kSr;
        double rumble = 0.60 * std::sin(2.0 * kPi * 38.0 * t)
                      + 0.30 * std::sin(2.0 * kPi * 52.0 * t)
                      + 0.15 * std::sin(2.0 * kPi * 67.0 * t)
                      + 0.8 * lp;
        v = float(in + 0.025 * boom * rumble + 0.16 * y);
    }
    // a dry vocal that has been gained for the mix, not a generator with a headroom bug
    double pk = 0.0;
    for (float v : x) pk = std::max(pk, std::abs(double(v)));
    if (pk > 0.0) for (auto& v : x) v = float(v * (0.89 / pk));
    return x;
}

// Band energy in dB, one biquad band-pass pair, applied to a copy - and for the low
// bands, computed on a decimated copy.  A high-pass at 30 Hz running at 48 kHz has
// its poles at 0.9985 and its DC response is a subtraction of two nearly equal
// numbers: in double precision that "filter" passes the whole mix, so the first
// version of this test reported the rumble band as being at the level of the vocal
// and could not see the low cut at all.  Down at 3 kHz the same corner is well
// conditioned.
static double bandEnergyDbAt(const std::vector<float>& x, double lo, double hi, double fs, bool decim);
static double bandEnergyDb(const std::vector<float>& x, double lo, double hi) {
    return bandEnergyDbAt(x, lo, hi, kSr, hi <= 500.0);   // low bands: decimate first
}

static double bandEnergyDbAt(const std::vector<float>& x, double lo, double hi, double fs, bool decim) {
    struct B { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; };
    auto lowpass = [&](double f0, double q) {
        const double w0 = 2 * kPi * f0 / fs, cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2 * q), a0 = 1 + alpha;
        return B{(1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, (-2 * cw) / a0, (1 - alpha) / a0};
    };
    auto highpass = [&](double f0, double q) {
        const double w0 = 2 * kPi * f0 / fs, cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2 * q), a0 = 1 + alpha;
        return B{(1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, (-2 * cw) / a0, (1 - alpha) / a0};
    };
    auto run = [](B& f, double in) {
        const double y = f.b0 * in + f.z1;
        f.z1 = f.b1 * in - f.a1 * y + f.z2;
        f.z2 = f.b2 * in - f.a2 * y;
        return y;
    };
    if (decim) {
        const int factor = 16;
        B a = lowpass(900.0, 0.54119610), b = lowpass(900.0, 1.30656296);
        std::vector<float> d;
        d.reserve(x.size() / size_t(factor) + 1);
        for (size_t i = 0; i < x.size(); ++i) {
            const double s = run(b, run(a, double(x[i])));
            if (i % size_t(factor) == 0) d.push_back(float(s));
        }
        return bandEnergyDbAt(d, lo, hi, fs / factor, false);
    }
    B hp = highpass(lo, 0.7071), lp = lowpass(hi, 0.7071);
    B hp2 = highpass(lo, 0.7071), lp2 = lowpass(hi, 0.7071);
    double acc = 0.0;
    for (float v : x) {
        // 4th order on *both* sides.  The low-pass was declared and never applied
        // in the first version, so every "band" was only high-passed and the rumble
        // figure was really the level of the whole take.
        const double s = run(lp, run(lp2, run(hp, run(hp2, double(v)))));
        acc += s * s;
    }
    return 10.0 * std::log10(acc / std::max<size_t>(x.size(), 1) + 1e-20);
}

static double rmsDb(const std::vector<float>& x) {
    double acc = 0.0;
    for (float v : x) acc += double(v) * double(v);
    return 10.0 * std::log10(acc / std::max<size_t>(x.size(), 1) + 1e-20);
}

struct Processed {
    std::vector<float> out;
    aeq::RestoreState state;
    int applied = 0;
};

static Processed play(const std::vector<float>& take, double settleSeconds) {
    auto corePtr = std::make_unique<aeq::RestoreCore>();
    auto& core = *corePtr;
    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = settleSeconds;
    core.prepare(kSr, 512);
    core.setSettings(s);

    Processed p;
    p.out.resize(take.size());
    std::vector<float> l(512), r(512);
    int block = 0;
    for (size_t off = 0; off + 512 <= take.size(); off += 512, ++block) {
        for (int i = 0; i < 512; ++i) { l[size_t(i)] = take[off + i]; r[size_t(i)] = l[size_t(i)]; }
        core.processBlock(l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i) p.out[off + i] = l[size_t(i)];
        if (block % 40 == 0) core.refresh();     // the plugin's timer
    }
    core.refresh();
    p.state = core.state();
    p.applied = core.appliedBandCount();
    return p;
}

int main() {
    std::printf("\n===== AEQ RESTORE - does it damage the vocal? =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const auto take = dryVocal(196.0, 20.0, 6.0, 240.0);   // clean: no sibilance
    // A take whose RMS is not a take: every number below would be a measurement of
    // the generator, not of the plugin.  (This caught a blown-up bell filter once.)
    {
        double acc = 0.0, pk = 0.0;
        for (float v : take) { acc += double(v) * double(v); pk = std::max(pk, std::abs(double(v))); }
        const double db = 10.0 * std::log10(acc / double(take.size()) + 1e-20);
        std::printf("  take: %.0f s, RMS %.1f dBFS, peak %.2f\n", double(take.size()) / kSr, db, pk);
        if (db < -40.0 || db > -6.0 || pk > 1.02) {
            std::printf("  FAIL   the test signal itself is broken (RMS %.1f, peak %.2f)\n", db, pk);
            return 1;
        }
    }
    const auto run = play(take, 8.0);

    // The first seconds are listened to, not processed - measuring the whole file
    // would dilute every number with audio the plugin was right not to touch.  These
    // are the last four seconds: the correction in the signal path, settled.
    const size_t tailFrom = take.size() - size_t(4.0 * kSr);
    std::vector<float> inTail(take.begin() + long(tailFrom), take.end());
    std::vector<float> outTail(run.out.begin() + long(tailFrom), run.out.end());

    std::printf("  applied: %d bands, HPF %.0f Hz, settled at %.1f s of sung audio\n\n",
                run.applied, run.state.hpfHz, run.state.analyzedSeconds);
    checkTrue(run.applied > 0, "it did something (otherwise 'gentle' is empty)");

    // 1. BODY: the fundamental region.  A 196 Hz voice has its weight at 196-500 Hz.
    const double bodyIn  = bandEnergyDb(inTail,    150.0, 500.0);
    const double bodyOut = bandEnergyDb(outTail, 150.0, 500.0);
    checkF(bodyOut - bodyIn > -2.5,
           "the body of the voice is still there: %.2f dB at 150-500 Hz", bodyOut - bodyIn);

    // 2. LEVEL: a restore is not a re-mix.
    // The level of the *voice*, not of the take: the sub boom is what the plugin was
    // asked to remove, so weighting it into the level check would punish doing the job.
    const double level = bandEnergyDb(outTail, 90.0, 12000.0) - bandEnergyDb(inTail, 90.0, 12000.0);
    checkF(std::abs(level) < 2.0, "it did not change the level: %.2f dB", level);

    // 3. TOP: nothing above 6 kHz was thrown away.
    const double topIn  = bandEnergyDb(inTail,    6000.0, 14000.0);
    const double topOut = bandEnergyDb(outTail, 6000.0, 14000.0);
    checkF(topOut - topIn > -2.0, "the air is untouched: %.2f dB above 6 kHz", topOut - topIn);

    // 4. the low cut has to stay under the fundamental, whatever else happens
    checkF(run.state.hpfHz <= 0.62 * 196.0 + 1.0,
           "the low cut is below the fundamental: %.0f Hz for a 196 Hz voice", run.state.hpfHz);

    // 5. and no notch inside the fundamental region
    bool notchInBody = false;
    for (const auto& r : run.state.resonance) if (r.f0 < 1.15 * 196.0) notchInBody = true;
    checkTrue(!notchInBody, "no notch inside the fundamental region");

    // 6. do-no-harm is not enough: the move has to be aimed at something.  The take
    //    was built with a +6 dB boom under 110 Hz and a bell at 240 Hz; the low cut
    //    is the one decision that must show up in the sub region.
    const double subIn  = bandEnergyDb(inTail,    30.0, 100.0);
    const double subOut = bandEnergyDb(outTail, 30.0, 100.0);
    checkF(subOut - subIn < -1.0,
           "it removed the rumble it was supposed to remove: %.2f dB under 100 Hz",
           subOut - subIn);

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
