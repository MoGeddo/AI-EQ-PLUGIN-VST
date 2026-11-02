// Generated from analysis/out/amir15/env_target.csv - do not edit by hand.
// The same 15 mixes, measured on the HARMONIC ENVELOPE instead of the raw
// spectrum: the vocal-tract curve with the harmonic ladder removed.  Level is
// re the 900-1150 Hz reference, in the same units as TargetCurve.h, so the two
// are not interchangeable - an envelope level sits ~14 dB below the raw level at
// 200 Hz and ~0.5 dB below at 1.5 kHz (docs/14 section 4).
//
// Bands the corpus cannot measure (fewer than 8 of 15 takes have the band
// above 0.9*F0 for most frames) carry kUndefined: a monophonic voice has no
// energy below its fundamental, and inventing a target there would be a lie.
#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace aeq {

struct EnvTargetCurve {
    static constexpr int    kPoints      = 86;
    static constexpr double kFmin        = 100.0;
    static constexpr double kPointsPerOct = 12.0;
    static constexpr float  kUndefined   = 999.0f;

    static const std::array<float, kPoints>& level() {
        static const std::array<float, kPoints> v {
       999.000f,   999.000f,   999.000f,   999.000f,   999.000f,   999.000f,
       999.000f,   999.000f,   999.000f,   999.000f,   999.000f,   999.000f,
       999.000f,   999.000f,   999.000f,     5.135f,     4.909f,     4.386f,
         3.690f,     3.078f,     2.769f,     2.460f,     2.178f,     2.115f,
         2.168f,     2.283f,     2.453f,     2.564f,     2.496f,     2.210f,
         1.756f,     1.243f,     0.783f,     0.518f,     0.495f,     0.596f,
         0.629f,     0.483f,     0.247f,     0.113f,     0.104f,     0.034f,
        -0.251f,    -0.550f,    -0.624f,    -0.604f,    -0.599f,    -0.262f,
         0.272f,     0.265f,     0.160f,     0.407f,     0.188f,    -0.028f,
        -0.001f,     0.105f,    -0.290f,    -0.803f,    -0.836f,    -0.483f,
        -0.773f,    -1.217f,    -0.952f,    -0.486f,    -0.781f,    -1.786f,
        -2.594f,    -2.955f,    -2.791f,    -2.174f,    -1.456f,    -1.525f,
        -1.740f,    -2.300f,    -3.068f,    -2.721f,    -2.812f,    -3.516f,
        -3.684f,    -3.947f,    -4.067f,    -4.352f,    -4.989f,    -5.888f,
        -6.537f,    -7.838f,
        };
        return v;
    }

    static const std::array<float, kPoints>& sd() {
        static const std::array<float, kPoints> v {
       999.000f,   999.000f,   999.000f,   999.000f,   999.000f,   999.000f,
       999.000f,   999.000f,   999.000f,   999.000f,   999.000f,   999.000f,
       999.000f,   999.000f,   999.000f,     2.448f,     2.185f,     2.070f,
         2.049f,     1.943f,     1.373f,     1.183f,     1.098f,     1.015f,
         0.942f,     0.860f,     0.886f,     0.907f,     0.933f,     0.975f,
         1.008f,     1.057f,     1.084f,     1.049f,     0.923f,     0.720f,
         0.517f,     0.442f,     0.481f,     0.434f,     0.287f,     0.210f,
         0.544f,     0.674f,     0.523f,     0.737f,     0.876f,     0.889f,
         0.956f,     1.221f,     1.356f,     1.579f,     1.907f,     2.034f,
         2.055f,     1.883f,     1.756f,     2.148f,     2.308f,     2.370f,
         2.020f,     1.393f,     1.142f,     1.578f,     2.116f,     2.896f,
         3.275f,     3.285f,     2.964f,     2.428f,     1.747f,     1.571f,
         1.589f,     1.631f,     1.914f,     2.068f,     2.139f,     2.165f,
         2.257f,     2.748f,     2.523f,     1.891f,     1.915f,     2.115f,
         1.949f,     2.047f,
        };
        return v;
    }

    static bool definedAt(double f) {
        const int i = indexOf(f);
        return i >= 0 && level()[i] < kUndefined * 0.5f;
    }
    static int indexOf(double f) {
        const double x = std::log2(std::max(f, 1.0) / kFmin) * kPointsPerOct;
        const int i = static_cast<int>(std::lround(x));
        return (i >= 0 && i < kPoints) ? i : -1;
    }
    // linear in log2(f), same interpolation as TargetCurve::at.  Returns kUndefined
    // where the corpus has no measurement, so callers must check definedAt().
    static double at(double f) { return interp(level(), f); }
    static double sdAt(double f) { return interp(sd(), f); }
private:
    template <typename T>
    static double interp(const T& v, double f) {
        const double x = std::log2(std::max(f, 1.0) / kFmin) * kPointsPerOct;
        if (x <= 0.0) return v.front();
        if (x >= kPoints - 1) return v.back();
        const int i = static_cast<int>(x);
        const double t = x - i;
        return v[i] * (1.0 - t) + v[i + 1] * t;
    }
};

} // namespace aeq
