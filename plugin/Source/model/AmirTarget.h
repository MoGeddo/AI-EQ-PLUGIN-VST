// Generated from analysis/out/amir15/amir_measured.json - do not edit by hand.
// Amir Mahrous's 15 dry stems, MEASURED BY THIS PLUGIN'S OWN ANALYSER:
// 1/12-octave bands, gated frames, mean power per bin (aeq_cli profile, 15/15 stems).
// Leave-one-out shift across the 15 stems: 0.39 dB (the noise floor of his own material).
// Target and take are on one ruler, so `sameInstrument` is true and Match compares
// them without widening either side.  Band levels are relative to 1 kHz.
#pragma once
#include <array>
#include <cmath>

namespace aeq {

struct AmirTarget {
    static constexpr int kPoints = 104;
    // Are these numbers and the engine's own measurement the same instrument?  When they
    // are, the solver must not smooth the take to match a table that was never smoothed.
    static constexpr bool sameInstrument = true;
    static constexpr const char* name = "AMIR 15";

    static const std::array<float, kPoints>& hz() {
        static const std::array<float, kPoints> v {
        40.00f, 42.38f, 44.90f, 47.57f, 50.40f, 53.39f,
        56.57f, 59.93f, 63.50f, 67.27f, 71.27f, 75.51f,
        80.00f, 84.76f, 89.80f, 95.14f, 100.79f, 106.79f,
        113.14f, 119.86f, 126.99f, 134.54f, 142.54f, 151.02f,
        160.00f, 169.51f, 179.59f, 190.27f, 201.59f, 213.57f,
        226.27f, 239.73f, 253.98f, 269.09f, 285.09f, 302.04f,
        320.00f, 339.03f, 359.19f, 380.55f, 403.17f, 427.15f,
        452.55f, 479.46f, 507.97f, 538.17f, 570.18f, 604.08f,
        640.00f, 678.06f, 718.38f, 761.09f, 806.35f, 854.30f,
        905.10f, 958.92f, 1015.94f, 1076.35f, 1140.35f, 1208.16f,
        1280.00f, 1356.11f, 1436.75f, 1522.19f, 1612.70f, 1708.60f,
        1810.19f, 1917.83f, 2031.87f, 2152.69f, 2280.70f, 2416.32f,
        2560.00f, 2712.23f, 2873.50f, 3044.37f, 3225.40f, 3417.19f,
        3620.39f, 3835.67f, 4063.75f, 4305.39f, 4561.40f, 4832.64f,
        5120.00f, 5424.45f, 5747.01f, 6088.74f, 6450.80f, 6834.38f,
        7240.77f, 7671.33f, 8127.49f, 8610.78f, 9122.80f, 9665.27f,
        10240.00f, 10848.90f, 11494.01f, 12177.48f, 12901.59f, 13668.76f,
        14481.55f, 15342.66f,
        };
        return v;
    }
    static const std::array<float, kPoints>& db() {
        static const std::array<float, kPoints> v {
        -34.42f, -33.59f, -33.59f, -32.70f, -31.96f, -31.24f,
        -30.66f, -29.66f, -27.38f, -26.48f, -24.44f, -22.13f,
        -20.96f, -20.51f, -20.66f, -20.80f, -20.60f, -19.55f,
        -17.73f, -10.43f, -6.97f, -3.77f, 1.51f, 5.60f,
        7.80f, 10.10f, 10.10f, 10.63f, 11.42f, 11.80f,
        13.16f, 11.88f, 12.23f, 12.73f, 11.21f, 10.42f,
        9.79f, 10.25f, 9.54f, 9.12f, 9.27f, 8.42f,
        10.03f, 8.14f, 7.73f, 9.16f, 8.57f, 8.11f,
        6.97f, 7.52f, 6.35f, 4.23f, 4.22f, 2.88f,
        3.04f, 1.00f, -0.13f, 0.66f, 0.10f, -1.50f,
        -2.06f, -0.78f, -1.61f, -2.41f, -1.97f, -2.64f,
        -2.58f, -3.74f, -4.64f, -4.08f, -6.28f, -6.32f,
        -9.12f, -10.88f, -10.16f, -10.09f, -10.82f, -12.86f,
        -12.64f, -12.03f, -13.16f, -14.65f, -15.75f, -16.06f,
        -16.43f, -16.17f, -16.74f, -17.92f, -18.15f, -19.52f,
        -20.75f, -20.71f, -20.77f, -21.35f, -21.67f, -22.56f,
        -23.66f, -24.77f, -25.97f, -27.18f, -28.04f, -30.70f,
        -32.79f, -35.01f,
        };
        return v;
    }
    // the spread of his own 15 stems, band by band: the dead zone of a match is the
    // natural variation of the material, and this is that variation, not a guess
    static const std::array<float, kPoints>& sd() {
        static const std::array<float, kPoints> v {
        6.62f, 6.61f, 6.61f, 6.48f, 6.43f, 6.49f,
        6.48f, 6.78f, 7.51f, 7.84f, 8.56f, 9.35f,
        9.62f, 9.77f, 9.49f, 9.24f, 9.25f, 9.56f,
        10.06f, 11.85f, 13.54f, 14.87f, 16.19f, 16.81f,
        17.15f, 17.26f, 15.07f, 13.15f, 11.52f, 9.82f,
        8.70f, 7.15f, 6.51f, 6.44f, 4.24f, 4.16f,
        4.52f, 4.16f, 4.28f, 4.07f, 4.71f, 3.86f,
        5.04f, 5.06f, 5.44f, 5.28f, 4.47f, 3.89f,
        3.36f, 2.05f, 2.71f, 2.88f, 3.32f, 1.89f,
        3.37f, 2.06f, 0.78f, 2.22f, 2.82f, 3.02f,
        3.00f, 2.80f, 3.18f, 3.31f, 3.47f, 2.59f,
        3.32f, 2.51f, 3.80f, 5.13f, 4.13f, 3.96f,
        3.57f, 3.37f, 3.16f, 3.76f, 4.08f, 2.90f,
        3.05f, 2.97f, 2.58f, 4.14f, 5.30f, 5.45f,
        5.21f, 4.84f, 3.69f, 3.75f, 3.43f, 2.71f,
        2.32f, 2.72f, 3.17f, 3.26f, 3.01f, 2.85f,
        2.75f, 2.94f, 3.25f, 3.70f, 4.24f, 4.28f,
        4.53f, 4.69f,
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
