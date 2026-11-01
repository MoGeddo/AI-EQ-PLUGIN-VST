// Solves the corrective EQ.
//
// Philosophy: this is not a "clone the reference" EQ.  It answers one question -
// "which small set of moves brings THIS voice closer to the target without
// changing who the singer is?" - and it refuses to answer with moves it cannot
// justify:
//   * gains are capped (default +-6 dB),
//   * Q is bounded (0.4 .. 3.0) so it cannot turn into a comb filter,
//   * below 160 Hz nothing is boosted (rumble is the HPF's job),
//   * everything above the measured range is untouched,
//   * total number of bands is bounded.
//
// Greedy placement + bounded coordinate descent.  Deterministic: same input ->
// bit-identical output, which is what makes it trustworthy in a mix chain.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include "Biquad.h"
#include "../model/TargetCurve.h"

namespace aeq {

enum class MatchMode {
    Restore,   // fix problems, protect identity (smoothed target, gentle limits)
    Match      // pull toward the corpus signature (full target)
};

struct SolverConfig {
    MatchMode mode     = MatchMode::Restore;
    double  restoreSmoothingOct = 1.0 / 2.0;
    int    maxBands    = 6;
    // Below 200 Hz a 1/12-octave band is narrower than the spacing between the
    // harmonics of a 200-300 Hz voice, so the measurement there is dominated by
    // which harmonic happens to fall in the band - not by the mix.  The low end
    // is therefore left to the HPF decision, and nothing else is corrected here.
    double minFreq     = 200.0;
    double maxFreq     = 13000.0;
    double maxGainDb   = 6.0;
    double minQ        = 0.4;
    double maxQ        = 1.8;
    double smoothOct   = 1.0 / 3.0;  // regularisation: never chase 1/12-oct wiggles
    double minMoveDb   = 1.2;        // do not add a band for less than this
    // The 13 reference mixes differ from each other by about +-3 dB per band
    // after pitch correction.  That spread is *legitimate* - it is what makes a
    // singer sound like themselves - so anything inside it is left alone.
    double deadZoneDb   = 2.5;
    double maxTotalDb  = 12.0;       // sum of |gain| sanity cap
};

struct SolveInput {
    std::vector<double> freqs;     // analysis grid (Hz)
    std::vector<double> levelDb;   // measured, absolute
    std::vector<double> valid;     // 1 where the measurement is trustworthy
};

// log-frequency gaussian smoothing of a curve sampled at ~constant log spacing
inline void smoothLog(std::vector<double>& y, double octaves, double pointsPerOct) {
    const double sigma = std::max(0.5, octaves * pointsPerOct);
    const int r = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<double> tmp(y.size(), 0.0);
    for (int i = 0; i < static_cast<int>(y.size()); ++i) {
        double acc = 0.0, wsum = 0.0;
        for (int k = -r; k <= r; ++k) {
            const int j = std::clamp(i + k, 0, static_cast<int>(y.size()) - 1);
            const double w = std::exp(-0.5 * (k / sigma) * (k / sigma));
            acc += y[j] * w; wsum += w;
        }
        tmp[i] = acc / wsum;
    }
    y.swap(tmp);
}

class EqSolver {
public:
    // target vs measured -> corrective bands
    std::vector<BandSpec> solve(const SolveInput& in, const SolverConfig& cfg, double fs) const {
        std::vector<BandSpec> bands;
        const int n = static_cast<int>(in.freqs.size());
        if (n < 8) return bands;

        // ---- what we want: target - measured, on the same grid --------------
        // normalise both at 1 kHz so only the *shape* matters
        std::vector<double> want(n, 0.0);
        std::vector<double> mask(n, 0.0);
        double refMeas = interpAt(in.freqs, in.levelDb, 1000.0);
        for (int i = 0; i < n; ++i) {
            const double f = in.freqs[i];
            const bool usable = in.valid[i] > 0.5 && f >= cfg.minFreq && f <= cfg.maxFreq;
            mask[i] = usable ? 1.0 : 0.0;
            if (usable) {
                const double t = (cfg.mode == MatchMode::Match)
                                   ? TargetCurve::at(f)
                                   : TargetCurve::atSmoothed(f, cfg.restoreSmoothingOct);
                const double m = in.levelDb[i] - refMeas;            // re 1 kHz
                double d = t - m;
                // soft dead-zone: subtract the family's own natural spread
                const double mag = std::abs(d);
                d = (mag <= cfg.deadZoneDb) ? 0.0
                    : std::copysign(mag - cfg.deadZoneDb, d);
                want[i] = d;
            }
        }
        smoothLog(want, cfg.smoothOct, 12.0);   // 12 points per octave grid

        // ---- greedy placement ---------------------------------------------
        std::vector<double> resid = want;
        const int maxB = std::min(cfg.maxBands, 10);
        for (int k = 0; k < maxB; ++k) {
            // find the largest masked residual
            int bi = -1; double bv = 0.0;
            for (int i = 0; i < n; ++i) {
                if (mask[i] < 0.5) continue;
                if (std::abs(resid[i]) > std::abs(bv)) { bv = resid[i]; bi = i; }
            }
            if (bi < 0 || std::abs(bv) < cfg.minMoveDb) break;

            const double f0 = in.freqs[bi];
            const double gain = std::clamp(bv, -cfg.maxGainDb, cfg.maxGainDb);

            // Q from the half-prominence width of the residual lobe
            const double half = std::abs(bv) * 0.5;
            int lo = bi, hi = bi;
            const double sgn = bv > 0 ? 1.0 : -1.0;
            while (lo > 0 && resid[lo] * sgn > half && sgn * resid[lo] > 0) --lo;
            while (hi < n - 1 && resid[hi] * sgn > half && sgn * resid[hi] > 0) ++hi;
            double bwOct = std::log2(in.freqs[hi] / std::max(in.freqs[lo], 1.0));
            bwOct = std::clamp(bwOct, 1.0 / 3.0, 4.0);
            const double q = std::clamp(1.0 / bwOct * 0.8, cfg.minQ, cfg.maxQ);

            // never stack two bands on the same place - duplicated bands are
            // how naive solvers end up with 3 dB of unintended boost
            bool tooClose = false;
            for (const auto& ex : bands)
                if (std::abs(std::log2(ex.f0 / f0)) < 0.25 && (ex.gainDb > 0) == (gain > 0)) tooClose = true;
            if (tooClose) {
                // keep the strongest one and stop adding in that region
                for (int i = 0; i < n; ++i) resid[i] -= bandMagDb(BandSpec{BandType::Peak, f0, q, gain}, in.freqs[i], fs);
                continue;
            }
            BandSpec b{BandType::Peak, f0, q, gain};
            bands.push_back(b);
            for (int i = 0; i < n; ++i) resid[i] -= bandMagDb(b, in.freqs[i], fs);
        }

        // ---- bounded refinement -------------------------------------------
        // coordinate descent over (f0, Q, gain) for each band; a handful of
        // deterministic sweeps is enough and keeps this real-time friendly.
        auto cost = [&](const std::vector<BandSpec>& bb) {
            double c = 0.0;
            for (int i = 0; i < n; ++i) {
                if (mask[i] < 0.5) continue;
                double mod = 0.0;
                for (const auto& b : bb) mod += bandMagDb(b, in.freqs[i], fs);
                const double e = (want[i] - mod);
                c += std::min(e * e, 16.0);          // soft-L1: ignore outliers
            }
            return c;
        };

        for (int sweep = 0; sweep < 3; ++sweep) {
            for (auto& b : bands) {
                const double fMul[] = {0.85, 0.93, 1.0, 1.07, 1.18};
                const double qMul[] = {0.75, 0.88, 1.0, 1.14, 1.3};
                const double gMul[] = {-0.4, -0.2, 0.0, 0.2, 0.4};
                BandSpec best = b;
                double bestC = cost(bands);
                for (double fm : fMul) for (double qm : qMul) {
                    BandSpec t = b;
                    t.f0 = std::clamp(b.f0 * fm, cfg.minFreq, cfg.maxFreq);
                    t.q  = std::clamp(b.q * qm, cfg.minQ, cfg.maxQ);
                    const BandSpec keep = t;
                    for (double gm : gMul) {
                        t = keep;
                        t.gainDb = std::clamp(b.gainDb + gm, -cfg.maxGainDb, cfg.maxGainDb);
                        if (std::abs(t.gainDb) < 0.3) continue;
                        // evaluate this band in place
                        const BandSpec old = b;
                        for (auto& x : bands) if (&x == &b) x = t;
                        const double c = cost(bands);
                        for (auto& x : bands) if (&x == &b) x = old;
                        if (c < bestC) { bestC = c; best = t; }
                    }
                }
                b = best;
            }
        }

        // ---- clean up ------------------------------------------------------
        std::vector<BandSpec> out;
        double total = 0.0;
        for (auto& b : bands) {
            if (std::abs(b.gainDb) < cfg.minMoveDb * 0.5) continue;
            total += std::abs(b.gainDb);
            out.push_back(b);
        }
        if (total > cfg.maxTotalDb) {          // scale back proportionally
            const double k = cfg.maxTotalDb / total;
            for (auto& b : out) b.gainDb *= k;
        }
        std::sort(out.begin(), out.end(),
                  [](const BandSpec& a, const BandSpec& b) { return a.f0 < b.f0; });
        return out;
    }

    // RMS distance over only those bands where the measured deviation exceeds
    // the dead-zone - i.e. the problems we decided to act on.
    static double distanceOnActive(const SolveInput& in, const std::vector<BandSpec>& bands,
                                   double fs, double deadZoneDb) {
        double acc = 0.0; int n = 0;
        const double refMeas = interpAt(in.freqs, in.levelDb, 1000.0);
        for (size_t i = 0; i < in.freqs.size(); ++i) {
            const double f = in.freqs[i];
            if (f < 200.0 || f > 13000.0 || in.valid[i] < 0.5) continue;
            const double dev = TargetCurve::atSmoothed(f, 0.5) - (in.levelDb[i] - refMeas);
            if (std::abs(dev) <= deadZoneDb) continue;
            double mod = 0.0;
            for (const auto& b : bands) mod += bandMagDb(b, f, fs);
            const double e = dev - mod;
            acc += e * e; ++n;
        }
        return n ? std::sqrt(acc / n) : 0.0;
    }

    // how far the (corrected) spectrum sits from the target, in dB RMS
    static double distanceToTarget(const SolveInput& in, const std::vector<BandSpec>& bands,
                                   double fs, double f1 = 200.0, double f2 = 12000.0) {
        double acc = 0.0; int n = 0;
        const double refMeas = interpAt(in.freqs, in.levelDb, 1000.0);
        for (size_t i = 0; i < in.freqs.size(); ++i) {
            const double f = in.freqs[i];
            if (f < f1 || f > f2 || in.valid[i] < 0.5) continue;
            double mod = 0.0;
            for (const auto& b : bands) mod += bandMagDb(b, f, fs);
            const double corrected = in.levelDb[i] - refMeas + mod;
            const double e = TargetCurve::at(f) - corrected;
            acc += e * e; ++n;
        }
        return n ? std::sqrt(acc / n) : 0.0;
    }

private:
    static double interpAt(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
        if (xs.empty()) return 0.0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        for (size_t i = 1; i < xs.size(); ++i) {
            if (xs[i] >= x) {
                const double t = std::log(x / xs[i - 1]) / std::log(xs[i] / xs[i - 1]);
                return ys[i - 1] * (1 - t) + ys[i] * t;
            }
        }
        return ys.back();
    }
};

} // namespace aeq
