// test_smooth.cpp - the plugin must not stutter, click, or allocate while it plays.
//
// The complaint that produced this file: "مش سلس بيقطع" - it is not smooth, it cuts.
// Three things in the audio path were doing that, all of them invisible to a test
// that only checks the output is finite:
//
//   1. setBands() called sections_.clear() + push_back() on the audio thread, four
//      times a second.  That is a heap allocation inside processBlock.
//   2. The same call replaced every biquad with a freshly built one, so z1/z2 - the
//      filter's memory - went to zero at 4 Hz.  A filter whose state is zeroed on a
//      live signal does not "change settings", it punches a hole and a click.
//   3. HighPass24::set() did the same thing to the high-pass, twice a second.
//
// So this measures what a listener hears:
//
//   * ALLOCATIONS   - every operator new on the audio thread, counted by overriding
//                     the global allocator.  The answer has to be zero.
//   * CLICKS        - the largest sample-to-sample step in the output, compared with
//                     the largest step the signal itself contains.  A discontinuity
//                     shows up as a step no sine wave can make.
//   * HOLES         - the quietest 10 ms window over the run, against the steady
//                     level: a cut shows up as a dip that nothing in the input has.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_smooth.cpp -o build/aeq_smooth
//   build/aeq_smooth
//
// Exits 0 when the audio path is clean, 1 otherwise.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

#include "../Source/dsp/RestoreCore.h"

// ------------------------------------------------------------------ allocations
// Overriding the global allocator is the only way to see the audio thread's heap
// traffic from outside the DSP code.  Only the thread that says "I am inside
// processBlock" is counted, so the analysis thread's legitimate allocation does not
// pollute the number.
static thread_local bool t_trackAllocs = false;
static thread_local long t_allocCount = 0;

void* operator new(std::size_t n) {
    if (t_trackAllocs) ++t_allocCount;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) {
    if (t_trackAllocs) ++t_allocCount;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int gPass = 0, gFail = 0;
static void checkTrue(bool ok, const char* what) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;

static void checkTrueF(bool ok, const char* fmt, double value) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, value);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

int main() {
    std::printf("\n===== AEQ RESTORE - is the audio path smooth? =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = 1.0;      // settle quickly, so the correction lands mid-run

    // A voiced take, not a sine: the analyser's gate only counts frames it believes
    // are a voice (that is the point of it), and a pure tone is not one.  This is the
    // same glottal source the other gates use: vibrato, harmonics, a breath gap once
    // every 1.55 s, so the analysis actually produces a correction while we measure.
    const double f0 = 196.0;
    const long total = long(kSr * 12.0);
    std::vector<float> take(static_cast<size_t>(total));
    {
        unsigned seed = 7;
        auto rnd = [&] { seed = seed * 1103515245u + 12345u; return double((seed >> 16) & 0x7fff) / 16384.0 - 1.0; };
        double phase = 0.0;
        for (long i = 0; i < total; ++i) {
            const double t = i / kSr;
            const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
            phase += 2.0 * kPi * f0 * vib / kSr;
            double x = 0.0;
            for (int k = 1; k <= 24; ++k) x += std::sin(k * phase) / std::pow(k, 1.1);
            const double cyc = std::fmod(t, 1.55);
            const double env = (cyc < 1.2) ? 1.0 : 0.0;
            const double sib = (cyc > 1.05 && cyc < 1.2) ? 0.25 * rnd() : 0.0;
            take[size_t(i)] = float(0.12 * env * x + sib);
        }
    }
    auto sample = [&](long i) { return take[size_t(i)]; };

    aeq::RestoreCore core;
    core.prepare(kSr, 512);
    core.setSettings(s);

    // the analysis thread, exactly as the plugin runs it: 4 Hz, off the audio thread
    std::atomic<bool> stop{false};
    std::thread analyser([&] {
        while (!stop.load()) {
            core.refresh();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });

    // ---------------------------------------------------------------- run
    const int block = 512;
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));

    long allocs = 0;
    // how long the callback takes, worst case: at 512 samples / 48 kHz the budget is
    // 10.7 ms.  This is the number a dropout is made of.
    double worstBlockMs = 0.0, sumBlockMs = 0.0;
    long blocksTimed = 0;
    double maxStep = 0.0, maxOut = 0.0;
    double prevOut = 0.0;
    long i = 0;

    // 10 ms windows, for the hole detector: output RMS against the input RMS of the
    // same window, so the source's own breath gaps cannot be mistaken for a dropout
    const int win = int(kSr * 0.010);
    double outSum = 0.0, inSum = 0.0;
    int winN = 0;
    double worstDropDb = 0.0;

    while (i < total) {
        for (int k = 0; k < block; ++k) { l[size_t(k)] = sample(i + k); r[size_t(k)] = l[size_t(k)]; }
        t_allocCount = 0;
        t_trackAllocs = true;
        const auto t0 = std::chrono::steady_clock::now();
        core.processBlock(l.data(), r.data(), block);
        const auto t1 = std::chrono::steady_clock::now();
        t_trackAllocs = false;
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        worstBlockMs = std::max(worstBlockMs, ms);
        sumBlockMs += ms; ++blocksTimed;
        allocs += t_allocCount;

        for (int k = 0; k < block; ++k) {
            const double y = l[size_t(k)];
            const double step = std::abs(y - prevOut);
            if (i + k > 4800) maxStep = std::max(maxStep, step);   // after the start-up
            maxOut = std::max(maxOut, std::abs(y));
            prevOut = y;
            outSum += y * y;
            inSum += double(sample(i + k)) * double(sample(i + k));
            if (++winN == win) {
                const double outRms = std::sqrt(outSum / winN);
                const double inRms = std::sqrt(inSum / winN);
                if (inRms > 0.02 && i > long(kSr)) {                     // a window with signal in it
                    const double dropDb = 20.0 * std::log10(std::max(outRms, 1e-9) / inRms);
                    worstDropDb = std::min(worstDropDb, dropDb);
                }
                outSum = inSum = 0.0; winN = 0;
            }
        }
        i += block;
    }
    stop.store(true);
    analyser.join();

    const auto st = core.state();
    std::printf("  bands applied: %d, settled: %s, analysed %.1f s\n\n",
                int(core.activeBands().size()), st.settled ? "yes" : "no", st.analyzedSeconds);

    // ------------------------------------------------------------ verdicts
    checkTrue(allocs == 0, "the audio thread never touches the heap");
    if (allocs != 0) std::printf("       %ld allocation(s) inside processBlock\n", allocs);

    // the steepest step this signal can make on its own: a full-scale step of the
    // 6th harmonic at 220 Hz is d/dx of sin, so the bound is 2*pi*f*total gain/fs
    double inMax = 0.0;
    for (long k = 1; k < total; ++k) inMax = std::max(inMax, double(std::abs(sample(k) - sample(k - 1))));
    checkTrueF(maxStep <= inMax * 3.0 + 1e-4,
               "no click: biggest step is a slope, not a jump (%.2e per sample)", maxStep);
    if (maxStep > inMax * 3.0 + 1e-4)
        std::printf("       the input's own steepest step is %.2e\n", inMax);

    // a cut is a hole: 10 ms of signal in, 10 ms of silence out.  -6 dB is a big
    // dip, so that is the bar; a dropout or a filter blow-up is far past it.
    checkTrueF(worstDropDb > -6.0,
               "no hole: the worst 10 ms drop against the input is %.2f dB", worstDropDb);

    // and the correction has to actually be applied, or the test proves a bypass is smooth
    checkTrue(st.valid && !core.activeBands().empty(), "a correction was being applied while all this ran");

    // The analysis used to run inside this callback.  A 512-sample block has 10.7 ms;
    // anything above a third of that is a stutter waiting for a busy machine.
    std::printf("       block time: worst %.3f ms, average %.3f ms (budget 10.7 ms)\n",
                worstBlockMs, sumBlockMs / std::max<long>(blocksTimed, 1));
    checkTrueF(worstBlockMs < 3.5, "the callback fits the budget: worst block %.3f ms", worstBlockMs);

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
