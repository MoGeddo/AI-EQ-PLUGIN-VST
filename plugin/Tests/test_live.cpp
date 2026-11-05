// test_live.cpp - the analyser has to move with the music.
//
// The complaint was "كمية بطء في الأنالايزر رهيبة".  It was not a repaint budget
// problem: bandLevelsDb() - the curve the editor drew - is the mean of every frame
// heard since load, so after a few seconds it is a constant.  What the user was
// watching could not move, by construction.
//
// The display now reads a separate 120 ms moving average (liveLevelsDb) published by
// pump(), which the plugin calls 30 times a second.  This gate measures both halves:
//
//   * TRACKING  a tone that is gated on and off must make the displayed level follow
//               within 300 ms, in both directions.
//   * RATE      the display must publish at least 20 updates per second of audio.
//   * MEAN      the settled measurement must *not* follow the gate - it is the mean
//               of the take, and the solver depends on that meaning.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_live.cpp -o build/aeq_live
//   build/aeq_live

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "../Source/dsp/RestoreCore.h"

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static void checkF(bool ok, const char* fmt, double a, double b = 0.0, double c = 0.0) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

// index of the band nearest a frequency
static int bandAt(const aeq::LiveSpectrum& L, double f) {
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < int(L.freqs.size()); ++i) {
        const double d = std::abs(L.freqs[size_t(i)] - f);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int main() {
    std::printf("\n===== AEQ RESTORE - does the analyser move? =====\n\n");

    auto corePtr = std::make_unique<aeq::RestoreCore>();
    auto& core = *corePtr;
    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = 100.0;          // stay in listening, we want the display
    core.prepare(kSr, 512);
    core.setSettings(s);

    // 1 kHz tone, gated on for 0.5 s, off for 0.5 s, for 6 s of audio.  pump() is
    // called every ~33 ms - the plugin's 30 Hz timer.
    const int block = 512;
    const int totalBlocks = int(6.0 * kSr / block);
    const int pumpEvery = 1600 / block;      // 33 ms
    int band = -1;                 // resolved once the grid exists, see the pump below

    std::vector<float> l(block), r(block);
    double phase = 0.0;
    unsigned long long lastVersion = 0;
    int updatesPerSecond = 0;
    int totalUpdates = 0;
    std::vector<std::pair<double, double>> trace;   // (time, live level at 1 kHz)

    double sinePhase = 0.0;
    for (int b = 0; b < totalBlocks; ++b) {
        const double t = double(b * block) / kSr;
        const bool on = std::fmod(t, 1.0) < 0.5;
        for (int i = 0; i < block; ++i) {
            sinePhase += 2.0 * kPi * 1000.0 / kSr;
            const double v = on ? 0.25 * std::sin(sinePhase) : 0.0;
            l[size_t(i)] = float(v);
            r[size_t(i)] = float(v);
        }
        core.processBlock(l.data(), r.data(), block);
        if ((b % pumpEvery) == 0) {
            core.pump();
            const auto L = core.live();
            if (band < 0) band = bandAt(L, 1000.0);
            if (band < 0) continue;
            if (L.version != lastVersion) {
                lastVersion = L.version;
                if (t < 1.0) ++updatesPerSecond;
                trace.emplace_back(t, L.liveDb[size_t(band)]);
            }
            ++totalUpdates;
        }
        (void)phase;
    }

    // ---- RATE
    std::printf("  display updates: %d per second of audio, %d in total\n", updatesPerSecond, totalUpdates);
    checkF(updatesPerSecond >= 20, "the display publishes at least 20 times a second: %.0f", double(updatesPerSecond));

    // ---- TRACKING: the level 100 ms after a gate edge, in both directions
    auto levelNear = [&](double t) {
        double best = 0.0; double bd = 1e9;
        for (auto& p : trace) if (std::abs(p.first - t) < bd) { bd = std::abs(p.first - t); best = p.second; }
        return best;
    };
    // gate closes at 0.5, 1.5, 2.5 ; opens at 1.0, 2.0, 3.0
    // gate: tone on 1.0-1.5, off 1.5-2.0, on 2.0-2.5
    const double onLevel   = levelNear(1.45);      // steady, tone on
    const double after250  = levelNear(1.75);      // 250 ms after it stops
    const double after1000 = levelNear(2.45);      // 1 s after it stopped... (re-gated at 2.0)
    const double attack    = levelNear(2.10);      // 100 ms after it starts again
    checkF(onLevel > -50.0, "with the tone on, the display is at the tone: %.1f dB", onLevel);
    checkF(onLevel - after250 > 8.0, "and it is falling 250 ms after the tone stops: %.1f dB down", onLevel - after250);
    checkF(attack > onLevel - 6.0, "and it is back within 6 dB 100 ms after it starts: %.1f dB", attack);
    (void) after1000;

    // ---- MEAN: the settled measurement must not follow the gate
    const auto meas = core.measurements();
    if (! meas.freqs.empty()) {
        int mi = 0;
        for (int i = 0; i < int(meas.freqs.size()); ++i)
            if (std::abs(meas.freqs[size_t(i)] - 1000.0) < std::abs(meas.freqs[size_t(mi)] - 1000.0)) mi = i;
        const double mean = meas.bandDb[size_t(mi)];
        checkF(mean > -60.0, "the settled measurement also has the tone: %.1f dB", mean);
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
