// test_threads.cpp - the concurrency contract between the audio thread and the UI.
//
// Why this file exists: the plugin crashed inside FL Studio.  The audio thread runs
// RestoreCore::processBlock while the message thread runs refresh(), and the editor
// runs its own timer.  A single write from the wrong thread (rebuilding the biquad
// cascade under the audio thread, for instance) is invisible on a fast machine and
// lethal in a DAW that runs blocks of 64 samples with a 2 ms deadline.
//
// Run it under ThreadSanitizer, which is the only tool that actually sees this:
//
//   g++ -std=c++17 -O1 -g -fsanitize=thread -Iplugin/Source \
//       plugin/Tests/test_threads.cpp -o build/aeq_threads && build/aeq_threads
//
// Exit code 0 means the two threads never touched the same object without a
// happens-before edge.  TSAN reports a data race as a fatal error before we get
// here, so a clean exit is the assertion.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "../Source/dsp/RestoreCore.h"

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

std::vector<float> makeVoice(int n, double f0 = 220.0) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        double s = 0.0;
        for (int k = 1; k <= 20; ++k)
            s += std::sin(2.0 * M_PI * k * f0 * t) / k;
        x[size_t(i)] = float(0.1 * s);
    }
    return x;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("\n===== AEQ RESTORE - thread contract =====\n");

    aeq::RestoreCore core;
    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = 0.0;         // apply as soon as there is a proposal
    core.prepare(kSr, kBlock);
    core.setSettings(s);

    const int blocks = 600;
    const auto voice = makeVoice(blocks * kBlock);

    std::atomic<bool> stop{false};
    std::atomic<int> blocksDone{0};

    // ---- the audio thread: process, never allocate beyond what the core owns ----
    std::thread audio([&] {
        std::vector<float> l(kBlock), r(kBlock);
        for (int b = 0; b < blocks && !stop.load(); ++b) {
            for (int i = 0; i < kBlock; ++i) { l[size_t(i)] = voice[size_t(b * kBlock + i)]; r[size_t(i)] = l[size_t(i)]; }
            core.processBlock(l.data(), r.data(), kBlock);
            blocksDone.fetch_add(1);
        }
    });

    // ---- the message thread: the plugin's timer (analysis) ----
    std::thread analysis([&] {
        for (int i = 0; i < 40 && !stop.load(); ++i) {
            core.refresh();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // ---- the editor thread: read snapshots and drag handles ----
    std::thread editor([&] {
        for (int i = 0; i < 200 && !stop.load(); ++i) {
            const auto st = core.state();                 // locked copy
            const auto m = core.measurements();           // locked copy
            const auto bands = core.activeBands();        // locked copy
            (void) st; (void) m; (void) bands;
            core.setBandTrim(0, (i % 10) * 0.5);          // must be audio-thread applied
            if (i == 100) core.clearTrims();
            (void) core.bandTrim(0);
            (void) core.learnProgress();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    audio.join();
    stop.store(true);
    analysis.join();
    editor.join();

    // the engine must still be alive and producing finite audio after all of that
    {
        std::vector<float> l(kBlock), r(kBlock);
        for (int i = 0; i < kBlock; ++i) { l[size_t(i)] = voice[size_t(i)]; r[size_t(i)] = l[size_t(i)]; }
        core.processBlock(l.data(), r.data(), kBlock);
        bool finite = true;
        for (float v : l) if (!std::isfinite(v)) finite = false;
        std::printf("  %s  audio thread survived %d blocks\n",
                    finite ? "ok  " : "FAIL", blocksDone.load());
        if (!finite) { std::printf("\n===== 0 passed, 1 failed =====\n\n"); return 1; }
    }

    std::printf("  ok   no data race reported between audio, analysis and editor\n");
    std::printf("\n===== 2 passed, 0 failed =====\n\n");
    return 0;
}
