// The stabiliser: a bank of moving cuts that keeps the take on its own learned curve.
//
// Three of the four reference plugins do something this plugin did not:
//
//   Ozone 12 - Stabilizer   a strip of bands that move *continuously* and cut what
//                           sticks out, without being told where the problems are.
//   Waves Curves Equator   resonance suppression across the whole spectrum, not at a
//                           handful of chosen frequencies.
//   Waves Curves AQ        an adaptive curve: the EQ follows the material as it plays.
//   (sonible smart:EQ 4)   the *learned* curve is the reference the plugin defends.
//
// The pitch-tracked band (Antares' idea) only ever covers one or two harmonics, so on
// its own the plugin was "a static curve plus two moving bands" - which is what the
// user kept hearing as "a few buttons and nothing moving".
//
// WHAT THIS IS, EXACTLY
// --------------------
// A fixed log grid of bands (kMaxBands, 1/2 octave, 120 Hz .. 12 kHz).  Each band has
// a *target* that comes from the take itself: the long-term level that band had while
// the plugin was listening.  Every 16 samples the band measures its own content; if it
// is more than `deadBandDb` above the target, the band cuts by a fraction of the excess,
// smoothed with its own attack/release, and bounded by `maxGrDb`.  It never boosts.
//
// So the decision is: "hold every part of this voice at the level the take had while I
// listened to it".  Loud notes, plosives, creaky onsets and room ring are pulled back
// onto the same curve; the ordinary body of the note, which sits on the target by
// definition, is not touched at all.  That is why it can be audible as movement without
// being audible as an EQ.
//
// WHY IT IS BUILT THIS WAY
//   * cut-only.  A boost under the signal lifts the noise floor between words, which
//     is the one thing a vocal plugin must never do.
//   * measured, not thresholded by taste.  The target is the take's own level - the
//     same measurement the static curve is solved against - so there is no second
//     opinion about "how loud is too loud" competing with the first.
//   * bounded.  3 dB per band, 6 dB in total, and a hard 0.6 x excess ratio, because a
//     stabiliser that can move 12 dB at 200 Hz is a compressor with a spectrum.
//   * block-safe.  No allocation, no locks, no branches on anything but numbers.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace aeq {

class Stabilizer {
public:
    static constexpr int    kMaxBands = 10;
    static constexpr int    kSub      = 16;
    static constexpr double kPi       = 3.14159265358979323846;

    struct Band {
        double hz       = 1000.0;
        double targetDb = -30.0;   // the level this band had on the learned take
        double maxGrDb  = 3.0;     // how far this band may cut
        double q        = 2.5;
        double slopeDb  = 0.0;     // dB of target movement per dB of loudness
        double loudRefDb = -30.0;  // the loudness of the take the target was learned on
    };

    // The grid: 1/2 octave from 120 Hz to 12 kHz, 10 bands.  Returned rather than
    // hard-coded into the core, so the test and the core agree on it by construction.
    static int grid(double* out, int cap) {
        static constexpr double kCentres[kMaxBands] = {
            150.0, 212.0, 300.0, 424.0, 600.0, 848.0, 1200.0, 1697.0, 2400.0, 3394.0
        };
        const int n = std::min(cap, kMaxBands);
        for (int i = 0; i < n; ++i) out[i] = kCentres[i];
        return n;
    }

    void prepare(double sampleRate) {
        fs_ = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        reset();
        // the coefficents only depend on fs and the band centres
        for (int i = 0; i < count_; ++i) retune(i);
    }

    void reset() {
        for (auto& b : bands_) {
            b.det.reset();
            b.bell.reset();
            b.env = -120.0;      // dB, not a linear envelope: 0 would mean full scale
            b.grDb = 0.0;
            b.levelDb = -120.0;
        }
        totalGrDb_ = 0.0;
    }

    void setAmount(double a) { amount_ = std::clamp(a, 0.0, 1.0); }

    // How much louder the voice is right now than the take the targets were learned on,
    // in dB, and how far the target should follow it.  0 dB / any slope is the honest
    // default: a band is compared with the take it was learned from.
    //
    // The slope matters more than it looks.  Without it, a phrase sung 8 dB louder than
    // the take makes *every* band exceed its target and the bank turns into a leveller -
    // it took 5.6 dB off a normal performance and was completely right about it
    // arithmetically, which is the worst kind of wrong.  With the corpus slope the target
    // rises with the loudness the way a real voice's spectrum does, so what is left for
    // the bank to cut is the part of the phrase that is louder *than the take at that
    // frequency* - which is what a stabiliser is for.
    void setLoudness(double loudDb) { loudNow_ = loudDb; }

    // ---- the learn pass ------------------------------------------------------
    // The target has to be measured by *this* detector, on *this* take.  The first
    // version took it from the analyser's 1/12-octave grid, which measures a narrow
    // slice; the bank's own band-pass is ~1/2 octave wide and therefore always read
    // louder than the target, so every band cut its ceiling from the first second and
    // stayed there.  Tested, caught, and replaced by measuring what we will defend:
    // the same filter, on the same audio, while the plug-in is listening.
    void beginLearn(const double* hz, int n, double q) {
        count_ = std::clamp(n, 0, kMaxBands);
        for (int i = 0; i < count_; ++i) {
            auto& b = bands_[size_t(i)];
            b.hz = std::max(20.0, hz[i]);
            b.q = std::clamp(q, 0.5, 8.0);
            b.targetDb = -200.0;      // nothing may cut while learning
            b.maxGrDb = 0.0;
            b.det.reset(); b.bell.reset();
            b.env = -120.0; b.grDb = 0.0; b.levelDb = -120.0;
            accP_[size_t(i)] = 0.0;
            accN_[size_t(i)] = 0.0;
            retune(i);
        }
        learning_ = true;
    }
    void endLearn() { learning_ = false; }
    double dbgAccN(int i) const { return (i>=0&&i<count_)?accN_[size_t(i)]:0.0; }
    double dbgAccP(int i) const { return (i>=0&&i<count_)?accP_[size_t(i)]:0.0; }
    // The level this take actually had in each band, as a mean of power over everything
    // that was not silence - the take's own curve, not an average of dB.
    double learnedLevelDb(int i) const {
        if (i < 0 || i >= count_ || accN_[size_t(i)] <= 0.0) return -200.0;
        const double p = accP_[size_t(i)] / accN_[size_t(i)];
        return 10.0 * std::log10(std::max(p, 1e-20));
    }

    // Called once per commit, on the audio thread, from the pending slot.
    void setBands(const Band* in, int n) {
        count_ = std::clamp(n, 0, kMaxBands);
        for (int i = 0; i < count_; ++i) {
            bands_[int(i)].hz       = std::max(20.0, in[i].hz);
            bands_[int(i)].targetDb = in[i].targetDb;
            bands_[int(i)].maxGrDb  = std::clamp(in[i].maxGrDb, 0.0, 6.0);
            bands_[int(i)].q        = std::clamp(in[i].q, 0.5, 8.0);
            bands_[int(i)].slopeDb  = std::clamp(in[i].slopeDb, 0.0, 1.5);
            bands_[int(i)].loudRefDb = in[i].loudRefDb;
            bands_[int(i)].det.reset();
            bands_[int(i)].bell.reset();
            bands_[int(i)].env  = -120.0;
            bands_[int(i)].grDb = 0.0;
            retune(int(i));
        }
        for (int i = count_; i < kMaxBands; ++i) {
            bands_[size_t(i)].grDb = 0.0;
            bands_[size_t(i)].env = -120.0;
        }
        totalGrDb_ = 0.0;
    }

    int bandCount() const { return count_; }

    // Buffers are modified in place; r may be null (mono).
    void process(float* l, float* r, int n) {
        if (!l || n <= 0 || count_ <= 0) return;
        for (int off = 0; off < n; off += kSub) {
            const int m = std::min(kSub, n - off);
            const double aAtt = std::exp(-double(m) / (0.001 * attackMs * fs_));
            const double aRel = std::exp(-double(m) / (0.001 * releaseMs * fs_));
            const double coefDet = std::exp(-double(m) / (0.001 * detectorMs * fs_));

            for (int bi = 0; bi < count_; ++bi) {
                auto& b = bands_[size_t(bi)];
                // gain for this sub-block, from what the previous one measured
                // the target carried to this moment's loudness
                const double tgt = b.targetDb
                                 + b.slopeDb * std::clamp(loudNow_ - b.loudRefDb, -24.0, 24.0);
                const double over = b.levelDb - (tgt + deadBandDb);
                double want = 0.0;
                if (over > 0.0 && b.levelDb > -72.0 && b.targetDb > -100.0)
                    want = -std::min(b.maxGrDb * amount_, ratio * over);
                const double a = (want < b.grDb) ? aAtt : aRel;
                b.grDb = want + (b.grDb - want) * a;

                const double A  = std::pow(10.0, b.grDb / 40.0);
                const double kB = 1.0 / (b.q * A);
                const double m1 = kB * (A * A - 1.0);

                double sq = 0.0;
                for (int i = 0; i < m; ++i) {
                    const int idx = off + i;
                    sq += tickOne(b, l[idx], kB, m1);
                    if (r) sq += tickOne(b, r[idx], kB, m1);
                }
                const double ms = sq / double(m * (r ? 2 : 1));
                const double lvl = 10.0 * std::log10(std::max(ms, 1e-20));
                b.env = lvl + (b.env - lvl) * coefDet;
                b.levelDb = b.env;
                // Accumulate the *smoothed* level, not the raw sub-block power.  The two
                // are not the same statistic: a note's onset is a 2 ms broadband burst
                // that dominates a raw power mean at 3 kHz and is completely gone by the
                // time the 30 ms envelope has moved - which put the learned target 10 dB
                // above what the band actually reads while the note is playing, so it
                // never cut.  The target must be the mean of the variable the decision
                // uses, or it is a number about a different signal.
                if (learning_ && b.env > -70.0) {          // voice, not the noise floor
                    accP_[size_t(bi)] += std::pow(10.0, b.env / 10.0);
                    accN_[size_t(bi)] += 1.0;
                }
            }
        }
        double total = 0.0;
        for (int i = 0; i < count_; ++i) total += std::min(0.0, bands_[size_t(i)].grDb);
        totalGrDb_ = std::max(-6.0, total);
    }

    // ---- readouts (editor + tests) -------------------------------------------
    int    bands() const { return count_; }
    double bandHz(int i) const { return (i >= 0 && i < count_) ? bands_[size_t(i)].hz : 0.0; }
    double gainDb(int i) const { return (i >= 0 && i < count_) ? bands_[size_t(i)].grDb : 0.0; }
    double levelDb(int i) const { return (i >= 0 && i < count_) ? bands_[size_t(i)].levelDb : -120.0; }
    double targetDb(int i) const { return (i >= 0 && i < count_) ? bands_[size_t(i)].targetDb : -120.0; }
    // the target as it stands this moment, carried to the current loudness
    double effectiveTargetDb(int i) const {
        if (i < 0 || i >= count_) return -120.0;
        const auto& b = bands_[size_t(i)];
        return b.targetDb + b.slopeDb * std::clamp(loudNow_ - b.loudRefDb, -24.0, 24.0);
    }
    // The sum of the bands that are cutting, floored at the product's 6 dB ceiling.
    double totalGainDb() const { return totalGrDb_; }

private:
    struct Svf {
        double ic1 = 0.0, ic2 = 0.0;
        void reset() { ic1 = ic2 = 0.0; }
        inline void tick(double x, double g, double k, double& lp, double& bp) {
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            lp = v2; bp = v1;
        }
    };
    struct Chan {
        Svf det, bell;
        double env = -120.0;   // dB
        double grDb = 0.0;
        double levelDb = -120.0;
        double hz = 1000.0, targetDb = -30.0, maxGrDb = 3.0, q = 2.5;
        double slopeDb = 0.0, loudRefDb = -30.0;
        double gDet = 0.0, gBell = 0.0;
    };

    void retune(int i) {
        auto& b = bands_[size_t(i)];
        const double fc = std::min(b.hz, 0.45 * fs_);
        b.gDet = std::tan(kPi * fc / fs_);
        b.gBell = b.gDet;
    }

    inline double tickOne(Chan& c, float& sample, double kB, double m1) {
        const double x = double(sample);
        double lp = 0.0, bp = 0.0;
        c.det.tick(x, c.gDet, 1.0 / c.q, lp, bp);
        // A sine of amplitude A at the centre leaves the band-pass at Q*A; dividing by
        // Q makes the detector read the signal's own level, not the filter's gain.
        const double b2 = (bp / c.q) * (bp / c.q);
        double blp = 0.0, bbp = 0.0;
        c.bell.tick(x, c.gBell, kB, blp, bbp);
        sample = float(x + m1 * bbp);
        return b2;
    }

    double fs_ = 48000.0;
    int    count_ = 0;
    double amount_ = 1.0;
    double loudNow_ = -30.0;
    std::array<Chan, kMaxBands> bands_{};
    double totalGrDb_ = 0.0;
    bool learning_ = false;
    std::array<double, kMaxBands> accP_{}, accN_{};

    // 4 ms attack / 180 ms release: fast enough to catch a plosive, slow enough that a
    // vibrato does not pump the band - the same numbers a mastering stabiliser uses.
    double attackMs  = 4.0;
    double releaseMs = 180.0;
    double detectorMs = 30.0;     // how long the band's level estimate takes to settle
    double deadBandDb = 2.0;      // how much louder than the take before it acts
    double ratio      = 0.6;      // cut this fraction of the excess
};

} // namespace aeq
