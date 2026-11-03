// test_bounds.cpp - what a host is allowed to do to this plugin, done to it.
//
// Written because FL Studio closed itself a second or two after playback started.
// The cause was a buffer the audio thread wrote past: `scratch_` was sized from the
// block size prepareToPlay() was given, and a host is free to send a bigger block
// than that.  ASan named it immediately:
//
//     heap-buffer-overflow WRITE of size 4 ... RestoreCore.h:108
//
// This test is that argument turned into a program.  It does the things hosts do:
//
//   * blocks far larger than the prepared size, and blocks of 1;
//   * a block size that changes on every callback (FL does this with its buffer
//     setting off);
//   * mono in, mono out;
//   * prepareToPlay() again at a different rate, mid-playback;
//   * reset() between blocks;
//   * processBlock() with the analyser lock held by the "message thread";
//   * 0 and negative sample counts.
//
// The assertions are about behaviour (finite output, no bank of NaN, the analyser
// seeing every sample it was given), but the real value is running it under
// AddressSanitizer, which turns an out-of-bounds write into a failure instead of a
// crash two seconds later in somebody else's DAW:
//
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Iplugin/Source \
//       plugin/Tests/test_bounds.cpp -o build/aeq_bounds && build/aeq_bounds
//
// Without the sanitizer it still catches a truncation: if the audio thread drops
// samples silently, the analysed duration stops matching what was played.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

#include "../Source/dsp/RestoreCore.h"

namespace {

constexpr double kPi = 3.14159265358979323846;   // M_PI is absent on MSVC
constexpr double kSr = 48000.0;
int gPass = 0, gFail = 0;

void checkTrue(bool ok, const char* what) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}

std::vector<float> tone(int n, double f = 220.0, double amp = 0.2) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) x[size_t(i)] = float(amp * std::sin(2.0 * kPi * f * i / kSr));
    return x;
}

bool allFinite(const std::vector<float>& x) {
    for (float v : x) if (!std::isfinite(v)) return false;
    return true;
}

} // namespace

static std::atomic<bool> finished{false};

// reset() used to take stateMx_ and then call updateBands(), which takes the same
// mutex, so the calling thread stopped forever - on the audio thread, inside the
// host's callback.  A hung gate is worse than a failing one (CI would sit there
// until the job timeout), so the deadlock is turned into a failure with a name.
static void startWatchdog() {
    std::thread([] {
        for (int i = 0; i < 180; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (finished.load()) return;
        }
        std::printf("\nWATCHDOG: the audio thread never came back - deadlock\n");
        std::fflush(stdout);
        std::_Exit(9);
    }).detach();
}

int main() {
    startWatchdog();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("\n===== AEQ RESTORE - hostile host =====\n");

    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = 0.0;

    // ---- 1. a block much larger than the prepared one -----------------------
    {
        std::printf("\n[1] block bigger than prepareToPlay was told\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 64);                 // the host promised 64
        core.setSettings(s);
        const int n = 8192;                    // and then sends 8192
        auto l = tone(n), r = l;
        core.processBlock(l.data(), r.data(), n);
        checkTrue(allFinite(l) && allFinite(r), "finite output for a 8192-sample block");
        checkTrue(core.samplesAnalysed() == n, "the analyser saw every one of the 8192 samples");
    }

    // ---- 2. the block size changes every callback ---------------------------
    {
        std::printf("\n[2] a different block size on every callback\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 256);
        core.setSettings(s);
        const int sizes[] = { 1, 4096, 7, 1024, 33, 8191, 2, 512, 3000 };
        bool finite = true;
        for (int rep = 0; rep < 30 && finite; ++rep) {
            for (int n : sizes) {
                auto l = tone(n, 220.0 + (rep % 5) * 10.0), r = l;
                core.processBlock(l.data(), r.data(), n);
                if (!allFinite(l) || !allFinite(r)) { finite = false; break; }
            }
            if (rep % 10 == 0) core.refresh();   // the plugin's own timer, meanwhile
        }
        checkTrue(finite, "survives 270 callbacks of changing sizes, analysis running");
    }

    // ---- 3. mono -----------------------------------------------------------
    {
        std::printf("\n[3] mono in, mono out\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 512);
        core.setSettings(s);
        auto l = tone(1024);
        core.processBlock(l.data(), nullptr, int(l.size()));
        checkTrue(allFinite(l), "a null right channel is not dereferenced");
    }

    // ---- 4. prepare() again mid-playback, at another rate ------------------
    {
        std::printf("\n[4] prepare() again mid-playback at 96 kHz, then back\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 512);
        core.setSettings(s);
        bool finite = true;
        for (double sr : { kSr, 96000.0, 44100.0, 88200.0 }) {
            core.prepare(sr, 512);
            for (int i = 0; i < 4; ++i) {
                auto l = tone(2000);
                core.processBlock(l.data(), l.data(), int(l.size()));
                if (!allFinite(l)) finite = false;
            }
            core.refresh();
        }
        checkTrue(finite, "four sample-rate changes with audio in between");
    }

    // ---- 5. reset() between blocks, and an empty/negative block ------------
    {
        std::printf("\n[5] reset between blocks, and 0 / negative sample counts\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 512);
        core.setSettings(s);
        auto l = tone(512);
        core.processBlock(l.data(), l.data(), 0);
        core.processBlock(l.data(), l.data(), -7);
        core.reset();
        core.processBlock(l.data(), l.data(), 512);
        checkTrue(allFinite(l), "0 and negative counts are a no-op, reset is safe");
    }

    // ---- 6. the analysis lock is held while a block arrives ----------------
    {
        std::printf("\n[6] the message thread holds the analyser while a block arrives\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 512);
        core.setSettings(s);
        std::atomic<bool> stop{false};
        std::thread analyser([&] {
            while (!stop.load()) {
                core.refresh();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));   // ~a real 4 Hz timer, not a spin
            }
        });
        bool finite = true;
        for (int i = 0; i < 400; ++i) {
            auto l = tone(256 + (i % 700));
            core.processBlock(l.data(), l.data(), int(l.size()));
            if (!allFinite(l)) finite = false;
        }
        stop.store(true);
        analyser.join();
        checkTrue(finite, "a busy analyser never blocks or corrupts the audio thread");
    }

    // ---- 7. every sample reaches the analyser ------------------------------
    {
        std::printf("\n[7] no sample is silently dropped when the block is chunked\n");
        aeq::RestoreCore core;
        core.prepare(kSr, 512);
        core.setSettings(s);
        const int n = 48000;                    // exactly one second, one callback
        auto l = tone(n);
        core.processBlock(l.data(), l.data(), n);
        const long got = core.samplesAnalysed();
        std::printf("       %ld samples analysed of %d delivered\n", got, n);
        checkTrue(got == n, "one second in, all of it analysed");
    }

    finished.store(true);
    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
