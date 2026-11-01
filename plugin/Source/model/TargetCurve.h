// Generated from analysis/out/target_curve.csv - do not edit by hand.
// Consensus vocal target of the 13-song corpus: 1/3-octave levels relative to
// 1 kHz, register-corrected, smoothed to 1/6 octave, resampled to 1/12 octave.
// Valid 100 Hz .. 13.56 kHz (above ~14.7 kHz the corpus MP3s are low-passed,
// so there is no measurement to trust there and the curve is not defined).
#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace aeq {

struct TargetCurve {
    static constexpr int    kPoints   = 86;
    static constexpr double kFmin     = 100.0;
    static constexpr double kPointsPerOct = 12.0;
    static constexpr double kVoiceMin = 100.0;   // usable range
    static constexpr double kVoiceMax = 13561.1;

    static const std::array<float, kPoints>& table() {
        static const std::array<float, kPoints> v {
     -20.021f,  -18.699f,  -17.178f,  -15.473f,  -13.585f,  -11.496f,
      -9.176f,   -6.634f,   -3.937f,   -1.307f,    1.056f,    3.002f,
       4.576f,    5.895f,    7.035f,    7.978f,    8.684f,    9.119f,
       9.268f,    9.137f,    8.762f,    8.199f,    7.522f,    6.834f,
       6.252f,    5.868f,    5.714f,    5.753f,    5.905f,    6.067f,
       6.146f,    6.081f,    5.830f,    5.351f,    4.619f,    3.681f,
       2.672f,    1.744f,    0.984f,    0.403f,   -0.064f,   -0.508f,
      -0.988f,   -1.474f,   -1.883f,   -2.154f,   -2.323f,   -2.510f,
      -2.845f,   -3.398f,   -4.149f,   -5.014f,   -5.898f,   -6.752f,
      -7.591f,   -8.482f,   -9.483f,  -10.553f,  -11.534f,  -12.261f,
     -12.709f,  -13.022f,  -13.413f,  -13.999f,  -14.722f,  -15.410f,
     -15.908f,  -16.197f,  -16.382f,  -16.593f,  -16.907f,  -17.313f,
     -17.764f,  -18.221f,  -18.665f,  -19.092f,  -19.507f,  -19.940f,
     -20.459f,  -21.149f,  -22.069f,  -23.242f,  -24.690f,  -26.543f,
     -29.198f,  -33.442f,
        };
        return v;
    }

    // Linear interpolation in log2(f).  Outside the measured range the curve is
    // flat-extended so the solver never sees a step it would try to "fix".
    static double at(double f) {
        const auto& v = table();
        double x = std::log2(std::max(f, 1.0) / kFmin) * kPointsPerOct;
        if (x <= 0.0) return v.front();
        if (x >= kPoints - 1) return v.back();
        int i = static_cast<int>(x);
        double t = x - i;
        return v[i] * (1.0 - t) + v[i + 1] * t;
    }

    static bool inMeasuredRange(double f) { return f >= kVoiceMin && f <= kVoiceMax; }

    // The corpus target contains features that belong to *those* mixes (for
    // example a narrow dip at 4.34 kHz that 11 of the 13 songs share).  For a
    // corrective "restore" pass we must not force a singer to reproduce another
    // mix's dip, so the restore layer uses a heavily smoothed version that only
    // carries the gross spectral balance of a good vocal.
    static double atSmoothed(double f, double octaves) {
        const auto& v = table();
        const double sigma = std::max(0.5, octaves * kPointsPerOct);
        const int r = static_cast<int>(std::ceil(3.0 * sigma));
        double x = std::log2(std::max(f, 1.0) / kFmin) * kPointsPerOct;
        const int c = std::clamp(static_cast<int>(std::lround(x)), 0, kPoints - 1);
        double acc = 0.0, wsum = 0.0;
        for (int k = -r; k <= r; ++k) {
            const int j = std::clamp(c + k, 0, kPoints - 1);
            const double w = std::exp(-0.5 * (k / sigma) * (k / sigma));
            acc += v[j] * w; wsum += w;
        }
        return acc / wsum;
    }
};

} // namespace aeq
