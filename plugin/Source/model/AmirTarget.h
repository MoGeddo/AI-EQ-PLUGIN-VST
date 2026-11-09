// Generated from analysis/out/amir15/amir15.json - do not edit by hand.
// Amir Mahrous's 15 dry stems: 1/3-octave band level, averaged over the stems, in dB.
// Leave-one-out shift across the 15: 1.05 dB (the noise floor of his own material).
// Absolute levels in dBFS; TargetCurve-style interpolation in log2(f), flat outside.
//
// MEASURED BY THE ANALYSIS PIPELINE, NOT BY THIS PLUGIN'S ANALYSER (1/3 octave, its own
// gating, its own window, against the plugin's 1/12 octave on gated frames).  It is a
// starting target, not a certificate: when the stems are on the machine that runs the
// plugin, `aeq_cli profile --from-dir` measures them with the plugin's own instrument and
// that profile replaces this table.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace aeq {

struct AmirTarget {
    static constexpr int kPoints = 27;

    static const std::array<float, kPoints>& hz() {
        static const std::array<float, kPoints> v {
        40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f,
        160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f,
        630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f,
        2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f,
        10000.0f, 12500.0f, 16000.0f,
        };
        return v;
    }
    static const std::array<float, kPoints>& db() {
        static const std::array<float, kPoints> v {
        -40.58f, -37.29f, -34.20f, -30.04f, -28.98f, -24.10f,
        -7.50f, 4.32f, 8.30f, 7.56f, 5.99f, 5.46f,
        5.36f, 1.72f, -0.74f, -2.98f, -4.53f, -6.18f,
        -9.81f, -12.95f, -14.54f, -19.16f, -19.76f, -22.03f,
        -23.48f, -27.92f, -38.16f,
        };
        return v;
    }
    // the spread of his own 15 stems, band by band: the dead zone of a match is the
    // natural variation of the material, and this is that variation, not a guess
    static const std::array<float, kPoints>& sd() {
        static const std::array<float, kPoints> v {
        6.88f, 6.41f, 6.97f, 9.05f, 8.99f, 12.91f,
        16.63f, 10.35f, 5.87f, 3.21f, 2.83f, 3.85f,
        2.46f, 0.72f, 0.93f, 1.86f, 1.77f, 2.69f,
        3.01f, 3.04f, 2.62f, 4.72f, 2.50f, 2.34f,
        2.37f, 2.89f, 4.13f,
        };
        return v;
    }

    static double at(double f) {
        const auto& x = hz();
        const auto& y = db();
        if (f <= x.front()) return y.front();
        if (f >= x.back())  return y.back();
        for (int i = 1; i < kPoints; ++i)
            if (x[i] >= f) {
                const double t = std::log2(f / x[i - 1]) / std::log2(x[i] / x[i - 1]);
                return y[i - 1] * (1.0 - t) + y[i] * t;
            }
        return y.back();
    }

    // Same shape, divided by its own 1 kHz - the frame the solver compares in.
    static double shapeAt(double f) { return at(f) - at(1000.0); }

    static double spreadAt(double f) {
        const auto& x = hz();
        const auto& s = sd();
        if (f <= x.front()) return s.front();
        if (f >= x.back())  return s.back();
        for (int i = 1; i < kPoints; ++i)
            if (x[i] >= f) {
                const double t = std::log2(f / x[i - 1]) / std::log2(x[i] / x[i - 1]);
                return s[i - 1] * (1.0 - t) + s[i] * t;
            }
        return s.back();
    }
};

} // namespace aeq
