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

    // Match is a different job from Restore, and it gets a different envelope: the user
    // has named a target (a record, a stem, a reference they like) and asked to be moved
    // onto it.  Restore's limits - 6 bands, +-3.5 dB, Q <= 1.2, a 2.5 dB dead zone - are
    // there so the plugin cannot "fix" a voice into someone else; used against a named
    // target they simply refuse to arrive, which is why matching a 24 dB/octave tilt
    // moved the take by 0.2 dB.  These are still bounded, just to the job.
    bool   useShelves  = false;      // mop up the residual tilt the peaks cannot
    double shelfLowHz  = 220.0;
    double shelfHighHz = 4000.0;
    double shelfS      = 0.71;
};

struct SolveInput {
    std::vector<double> freqs;     // analysis grid (Hz)
    std::vector<double> levelDb;   // measured, absolute
    std::vector<double> valid;     // 1 where the measurement is trustworthy

    // The curve to move toward, sampled on `freqs` and expressed *relative to 1 kHz* -
    // exactly the frame the measured level is put into below.  Empty means "the corpus
    // consensus", which is what this plugin solved against until a reference profile
    // existed: the corpus table was measured by different code, in a different frame
    // (1/3-octave, register-corrected), so the solver was comparing two instruments.
    // With a profile loaded, target and measurement come from the same analyser.
    std::vector<double> targetDb;
    bool hasTarget() const { return targetDb.size() == freqs.size(); }

    // Bandwidth of the target.  Both the corpus table and Amir's stems were measured as
    // **1/3-octave** band levels; this analyser measures 1/12 octave.  A wider band sums
    // more power, so a 1/3-octave level sits about 6 dB above a 1/12-octave level at the
    // same centre - and that offset is not constant across a curved spectrum, which is why
    // it cannot simply be subtracted.  Comparing one against the other is comparing two
    // instruments, the mistake G72 fixed at the profile boundary.  So the *measurement* is
    // smoothed to the target's own width before the subtraction: both sides then describe
    // the same band, and the constant cancels at the 1 kHz normalisation.
    // 0 disables it (a reference profile measured by this analyser needs no smoothing).
    double targetWidthOct = 1.0 / 3.0;

    double targetAt(double f) const {
        if (! hasTarget()) return TargetCurve::at(f);
        return interpLog(freqs, targetDb, f);
    }
    double targetSmoothAt(double f, double octaves) const {
        if (! hasTarget()) return TargetCurve::atSmoothed(f, octaves);
        // same gaussian, same width, on the supplied curve
        const double sigma = std::max(0.5, octaves * 12.0);
        const int r = static_cast<int>(std::ceil(3.0 * sigma));
        int c = 0;
        for (int i = 0; i < int(freqs.size()); ++i) if (freqs[size_t(i)] <= f) c = i;
        double acc = 0.0, wsum = 0.0;
        for (int k = -r; k <= r; ++k) {
            const int j = std::clamp(c + k, 0, int(targetDb.size()) - 1);
            const double w = std::exp(-0.5 * (k / sigma) * (k / sigma));
            acc += targetDb[size_t(j)] * w; wsum += w;
        }
        return acc / wsum;
    }

    static double interpLog(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
        if (xs.empty()) return 0.0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        for (size_t i = 1; i < xs.size(); ++i)
            if (xs[i] >= x) {
                const double t = std::log(x / xs[i - 1]) / std::log(xs[i] / xs[i - 1]);
                return ys[i - 1] * (1 - t) + ys[i] * t;
            }
        return ys.back();
    }
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

// Smoothing a spectrum means averaging *energy*, not decibels.  Averaging dB values of a
// signal whose bands alternate between +10 and -40 dB (a sung note at 1/12-octave
// resolution is exactly that: harmonics with notches between them) collapses the whole
// band range to one number, and two different voices then look identical because both
// collapsed to their own average.  Every smoothing here goes through power.
inline void smoothLogPower(std::vector<double>& dbCurve, double octaves, double pointsPerOct) {
    if (dbCurve.empty() || octaves <= 0.0) return;
    std::vector<double> p(dbCurve.size());
    for (size_t i = 0; i < dbCurve.size(); ++i) p[i] = std::pow(10.0, dbCurve[i] / 10.0);
    smoothLog(p, octaves, pointsPerOct);
    for (size_t i = 0; i < dbCurve.size(); ++i)
        dbCurve[i] = 10.0 * std::log10(std::max(p[i], 1e-20));
}

class EqSolver {
public:
    // The measurement as the comparison sees it: smoothed to the target's bandwidth.
    // Every metric in this file goes through here, so the solver's objective and the
    // number on the panel cannot be two different quantities (the mistake G71 found in
    // the stabiliser, one layer down).
    static std::vector<double> asMeasured(const SolveInput& in) {
        std::vector<double> m = in.levelDb;
        smoothLogPower(m, in.targetWidthOct, 12.0);   // no-op when the width is 0
        return m;
    }

    // A curve's value at one frequency, on the analyser's own log grid.  Public because
    // RestoreCore needs the take's level at 1 kHz to decide which bands it occupies.
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

    // target vs measured -> corrective bands
    std::vector<BandSpec> solve(const SolveInput& in, const SolverConfig& cfg, double fs) const {
        std::vector<BandSpec> bands;
        const int n = static_cast<int>(in.freqs.size());
        if (n < 8) return bands;

        // ---- what we want: target - measured, on the same grid --------------
        // normalise both at 1 kHz so only the *shape* matters
        std::vector<double> want(n, 0.0);
        std::vector<double> mask(n, 0.0);
        // the measurement, expressed in the target's own bandwidth
        const std::vector<double> measured = asMeasured(in);
        double refMeas = interpAt(in.freqs, measured, 1000.0);
        for (int i = 0; i < n; ++i) {
            const double f = in.freqs[i];
            const bool usable = in.valid[i] > 0.5 && f >= cfg.minFreq && f <= cfg.maxFreq;
            mask[i] = usable ? 1.0 : 0.0;
            if (usable) {
                const double t = (cfg.mode == MatchMode::Match)
                                   ? in.targetAt(f)
                                   : in.targetSmoothAt(f, cfg.restoreSmoothingOct);
                const double m = measured[i] - refMeas;             // re 1 kHz
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

        // ---- the tilt, which peaks cannot do -----------------------------
        // A vocal's difference from another vocal's curve is mostly a *tilt*, not a set
        // of lobes: peaks chase lobes well and a slope badly, so six peaks fighting a
        // 6 dB/octave difference leave most of it in place.  One low shelf and one high
        // shelf, each set to the mean residual on its side of a corner, remove the slope
        // in two filters instead of six approximations of it.
        if (cfg.useShelves) {
            const auto fitShelf = [&](bool low) {
                const double corner = low ? cfg.shelfLowHz : cfg.shelfHighHz;
                double acc = 0.0; int m = 0;
                for (int i = 0; i < n; ++i) {
                    if (mask[i] < 0.5) continue;
                    const double f = in.freqs[i];
                    if (low ? (f <= corner) : (f >= corner)) { acc += resid[i]; ++m; }
                }
                if (m < 3) return;
                const double g = std::clamp(acc / double(m), -cfg.maxGainDb, cfg.maxGainDb);
                if (std::abs(g) < cfg.minMoveDb) return;
                const BandSpec b{low ? BandType::LowShelf : BandType::HighShelf,
                                 corner, cfg.shelfS, g};
                bands.push_back(b);
                for (int i = 0; i < n; ++i) resid[i] -= bandMagDb(b, in.freqs[i], fs);
            };
            fitShelf(true);
            fitShelf(false);
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
        const std::vector<double> meas = asMeasured(in);
        const double refMeas = interpAt(in.freqs, meas, 1000.0);
        for (size_t i = 0; i < in.freqs.size(); ++i) {
            const double f = in.freqs[i];
            if (f < 200.0 || f > 13000.0 || in.valid[i] < 0.5) continue;
            const double dev = in.targetSmoothAt(f, 0.5) - (meas[i] - refMeas);
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
        const std::vector<double> meas = asMeasured(in);
        const double refMeas = interpAt(in.freqs, meas, 1000.0);
        for (size_t i = 0; i < in.freqs.size(); ++i) {
            const double f = in.freqs[i];
            if (f < f1 || f > f2 || in.valid[i] < 0.5) continue;
            double mod = 0.0;
            for (const auto& b : bands) mod += bandMagDb(b, f, fs);
            const double corrected = meas[i] - refMeas + mod;
            const double e = in.targetAt(f) - corrected;
            acc += e * e; ++n;
        }
        return n ? std::sqrt(acc / n) : 0.0;
    }

private:
};

} // namespace aeq
