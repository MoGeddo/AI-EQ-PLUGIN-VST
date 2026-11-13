// Minimal, dependency-free audio primitives for AEQ Restore.
// All maths is RBJ (Audio EQ Cookbook) in double precision -> the same curve is
// reproducible in the Python reference implementation to < 0.01 dB.
#pragma once
#include <cmath>
#include <cstddef>
#include <array>          // std::array, said out loud: MSVC does not get it from <vector>
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
    // Fixed capacity: no heap.  setBands() used to clear() and push_back() into a
    // std::vector on the audio thread, four times a second, which both allocates
    // inside processBlock and throws away every filter's memory (z1/z2) - a click
    // at 4 Hz that reads as "the plugin stutters".
    static constexpr int kMaxSections = 24;

    void prepare(double fs) { fs_ = fs; reset(); }

    void setBands(const std::vector<BandSpec>& bands, double fs) {
        setBands(bands.data(), int(bands.size()), fs);
    }

    // Build the new cascade without allocating, carry each section's memory over,
    // and crossfade from the old one so the change is a slope, not a step.
    void setBands(const BandSpec* bands, int count, double fs) {
        if (fs > 0.0) fs_ = fs;
        std::array<Biquad, kMaxSections> next{};
        int n = 0;
        for (int i = 0; i < count && n < kMaxSections; ++i) {
            const BandSpec& b = bands[i];
            if (std::abs(b.gainDb) < 1e-6) continue;
            const double q = std::max(b.q, 0.1);
            Biquad sec;
            switch (b.type) {
                case BandType::Peak:      sec = Biquad::peaking(f0Safe(b.f0, fs_), q, b.gainDb, fs_); break;
                case BandType::LowShelf:  sec = Biquad::lowShelf(f0Safe(b.f0, fs_), q, b.gainDb, fs_); break;
                case BandType::HighShelf: sec = Biquad::highShelf(f0Safe(b.f0, fs_), q, b.gainDb, fs_); break;
            }
            // the section that was in this slot keeps its delay line: the filter is
            // being retuned, not replaced
            if (n < nCur_) { sec.z1 = cur_[size_t(n)].z1; sec.z2 = cur_[size_t(n)].z2; }
            next[size_t(n++)] = sec;
        }

        if (n == nCur_ && sameCoefficients(next, n)) return;   // nothing to do: no click, no work

        prev_ = cur_;
        nPrev_ = nCur_;
        cur_ = next;
        nCur_ = n;
        fadePos_ = 0;
        fadeLen_ = int(std::max(32.0, fs_ * 0.15));    // 150 ms
        fading_ = true;
    }

    void reset() { for (auto& s : cur_) s.reset(); for (auto& s : prev_) s.reset(); nCur_ = nPrev_ = 0; fading_ = false; fadePos_ = 0; }

    inline double process(double x) noexcept {
        double y = x;
        for (int i = 0; i < nCur_; ++i) y = cur_[size_t(i)].process(y);
        if (! fading_) return y;
        double old = x;
        for (int i = 0; i < nPrev_; ++i) old = prev_[size_t(i)].process(old);
        const double t = double(fadePos_) / double(fadeLen_);
        if (++fadePos_ >= fadeLen_) { fading_ = false; nPrev_ = 0; }
        return old + (y - old) * t;
    }

    std::vector<double> magDb(const std::vector<double>& freqs, double fs) const {
        std::vector<double> out(freqs.size(), 0.0);
        for (int s = 0; s < nCur_; ++s)
            for (size_t i = 0; i < freqs.size(); ++i) out[i] += cur_[size_t(s)].magDb(freqs[i], fs);
        return out;
    }
    size_t size() const { return size_t(nCur_); }
    bool   isFading() const { return fading_; }

private:
    bool sameCoefficients(const std::array<Biquad, kMaxSections>& a, int n) const {
        for (int i = 0; i < n; ++i) {
            const auto& x = a[size_t(i)];
            const auto& y = cur_[size_t(i)];
            if (x.b0 != y.b0 || x.b1 != y.b1 || x.b2 != y.b2 || x.a1 != y.a1 || x.a2 != y.a2) return false;
        }
        return true;
    }

    std::array<Biquad, kMaxSections> cur_{}, prev_{};
    int nCur_ = 0, nPrev_ = 0;
    int fadePos_ = 0, fadeLen_ = 1;
    bool fading_ = false;
    double fs_ = 48000.0;
};

class HighPass24 {
public:
    // Retuning keeps the delay lines.  Assigning a freshly built Biquad - which is
    // what this did - zeroed z1/z2, so every retune (twice a second while the
    // measurement updates) punched a hole in the low end instead of sliding the
    // corner.
    void set(double f0, double fs) {
        const Biquad na = Biquad::highPass(f0, 0.54119610, fs);   // Butterworth pair
        const Biquad nb = Biquad::highPass(f0, 1.30656296, fs);
        const Biquad oldA = a_, oldB = b_;
        a_ = na; b_ = nb;
        a_.z1 = oldA.z1; a_.z2 = oldA.z2;
        b_.z1 = oldB.z1; b_.z2 = oldB.z2;
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
