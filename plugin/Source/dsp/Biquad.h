// Minimal, dependency-free audio primitives for AEQ Restore.
// All maths is RBJ (Audio EQ Cookbook) in double precision -> the same curve is
// reproducible in the Python reference implementation to < 0.01 dB.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace aeq {

#ifndef AEQ_PI
#define AEQ_PI
constexpr double kPi = 3.14159265358979323846;
#endif

// --------------------------------------------------------------------------- //
// a single biquad section (transposed direct form II)
// --------------------------------------------------------------------------- //
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process(double x) noexcept {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    inline void reset() noexcept { z1 = z2 = 0.0; }

    static Biquad peaking(double f0, double Q, double gainDb, double fs) {
        Biquad q;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * std::min(f0, fs * 0.49) / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * std::max(Q, 0.05));
        const double a0 = 1.0 + alpha / A;
        q.b0 = (1.0 + alpha * A) / a0;
        q.b1 = (-2.0 * cw) / a0;
        q.b2 = (1.0 - alpha * A) / a0;
        q.a1 = (-2.0 * cw) / a0;
        q.a2 = (1.0 - alpha / A) / a0;
        return q;
    }

    static Biquad lowShelf(double f0, double S, double gainDb, double fs) {
        Biquad q;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * std::min(f0, fs * 0.49) / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / std::max(S, 0.1) - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1) + (A - 1) * cw + sq;
        q.b0 = A * ((A + 1) - (A - 1) * cw + sq) / a0;
        q.b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        q.b2 = A * ((A + 1) - (A - 1) * cw - sq) / a0;
        q.a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        q.a2 = ((A + 1) + (A - 1) * cw - sq) / a0;
        return q;
    }

    static Biquad highShelf(double f0, double S, double gainDb, double fs) {
        Biquad q;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * std::min(f0, fs * 0.49) / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / std::max(S, 0.1) - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1) - (A - 1) * cw + sq;
        q.b0 = A * ((A + 1) + (A - 1) * cw + sq) / a0;
        q.b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
        q.b2 = A * ((A + 1) + (A - 1) * cw - sq) / a0;
        q.a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
        q.a2 = ((A + 1) - (A - 1) * cw - sq) / a0;
        return q;
    }

    static Biquad highPass(double f0, double Q, double fs) {
        Biquad q;
        const double w0 = 2.0 * kPi * std::min(f0, fs * 0.49) / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * std::max(Q, 0.05));
        const double a0 = 1.0 + alpha;
        q.b0 = (1.0 + cw) / 2.0 / a0;
        q.b1 = -(1.0 + cw) / a0;
        q.b2 = (1.0 + cw) / 2.0 / a0;
        q.a1 = (-2.0 * cw) / a0;
        q.a2 = (1.0 - alpha) / a0;
        return q;
    }

    static Biquad lowPass(double f0, double Q, double fs) {
        Biquad q;
        const double w0 = 2.0 * kPi * std::min(f0, fs * 0.49) / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * std::max(Q, 0.05));
        const double a0 = 1.0 + alpha;
        q.b0 = (1.0 - cw) / 2.0 / a0;
        q.b1 = (1.0 - cw) / a0;
        q.b2 = (1.0 - cw) / 2.0 / a0;
        q.a1 = (-2.0 * cw) / a0;
        q.a2 = (1.0 - alpha) / a0;
        return q;
    }

    // magnitude response in dB at frequency f
    double magDb(double f, double fs) const {
        const double w = 2.0 * kPi * f / fs;
        const double cw = std::cos(w), sw = std::sin(w);
        const double cw2 = std::cos(2 * w), sw2 = std::sin(2 * w);
        const double nr = b0 + b1 * cw + b2 * cw2;
        const double ni = -(b1 * sw + b2 * sw2);
        const double dr = 1.0 + a1 * cw + a2 * cw2;
        const double di = -(a1 * sw + a2 * sw2);
        const double num = std::sqrt(nr * nr + ni * ni);
        const double den = std::sqrt(dr * dr + di * di);
        return 20.0 * std::log10(std::max(num / std::max(den, 1e-12), 1e-12));
    }
};

// --------------------------------------------------------------------------- //
// one EQ band, its description and its runtime section
// --------------------------------------------------------------------------- //
enum class BandType { Peak, LowShelf, HighShelf };

struct BandSpec {
    BandType type = BandType::Peak;
    double   f0 = 1000.0;
    double   q  = 1.0;      // Q for peak, S for shelves
    double   gainDb = 0.0;
};

inline double f0Safe(double f0, double fs) { return std::min(std::max(f0, 10.0), fs * 0.49); }

inline double bandMagDb(const BandSpec& b, double f, double fs) {
    const double q = std::max(b.q, 0.1);
    switch (b.type) {
        case BandType::Peak:      return Biquad::peaking(f0Safe(b.f0, fs), q, b.gainDb, fs).magDb(f, fs);
        case BandType::LowShelf:  return Biquad::lowShelf(f0Safe(b.f0, fs), q, b.gainDb, fs).magDb(f, fs);
        case BandType::HighShelf: return Biquad::highShelf(f0Safe(b.f0, fs), q, b.gainDb, fs).magDb(f, fs);
    }
    return 0.0;
}

// sum of bands, evaluated on a frequency grid
inline std::vector<double> bandsMagDb(const std::vector<BandSpec>& bands,
                                      const std::vector<double>& freqs, double fs) {
    std::vector<double> out(freqs.size(), 0.0);
    for (const auto& b : bands)
        for (size_t i = 0; i < freqs.size(); ++i)
            out[i] += bandMagDb(b, freqs[i], fs);
    return out;
}

// --------------------------------------------------------------------------- //
// cascade of biquads (the actual processing)
// --------------------------------------------------------------------------- //
class BiquadCascade {
public:
    void setBands(const std::vector<BandSpec>& bands, double fs) {
        sections_.clear();
        for (const auto& b : bands) {
            if (std::abs(b.gainDb) < 1e-6) continue;
            const double q = std::max(b.q, 0.1);
            switch (b.type) {
                case BandType::Peak:      sections_.push_back(Biquad::peaking(b.f0, q, b.gainDb, fs)); break;
                case BandType::LowShelf:  sections_.push_back(Biquad::lowShelf(b.f0, q, b.gainDb, fs)); break;
                case BandType::HighShelf: sections_.push_back(Biquad::highShelf(b.f0, q, b.gainDb, fs)); break;
            }
        }
    }
    void reset() { for (auto& s : sections_) s.reset(); }

    inline double process(double x) noexcept {
        for (auto& s : sections_) x = s.process(x);
        return x;
    }
    std::vector<double> magDb(const std::vector<double>& freqs, double fs) const {
        std::vector<double> out(freqs.size(), 0.0);
        for (const auto& s : sections_)
            for (size_t i = 0; i < freqs.size(); ++i) out[i] += s.magDb(freqs[i], fs);
        return out;
    }
    size_t size() const { return sections_.size(); }

private:
    std::vector<Biquad> sections_;
};

// 24 dB/oct high-pass built from two cascaded 12 dB/oct sections
class HighPass24 {
public:
    void set(double f0, double fs) {
        a_ = Biquad::highPass(f0, 0.54119610, fs);   // Butterworth pair
        b_ = Biquad::highPass(f0, 1.30656296, fs);
        fs_ = fs; f0_ = f0;
    }
    void reset() { a_.reset(); b_.reset(); }
    inline double process(double x) noexcept { return b_.process(a_.process(x)); }
    double f0() const { return f0_; }
    double magDb(double f) const { return a_.magDb(f, fs_) + b_.magDb(f, fs_); }
private:
    Biquad a_, b_;
    double fs_ = 48000.0, f0_ = 20.0;
};

} // namespace aeq
