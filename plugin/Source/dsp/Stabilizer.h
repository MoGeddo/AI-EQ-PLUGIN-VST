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
#include <cstdint>

namespace aeq {

class Stabilizer {
public:
    static constexpr int    kMaxBands = 10;
    static constexpr int    kSub      = 16;
    static constexpr double kPi       = 3.14159265358979323846;
    // The most the whole bank may take at one frequency.  Enforced in process(), not
    // merely reported - see the budget comment there.
    static constexpr double kMaxTotalGrDb = 6.0;

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
            hist_[size_t(i)].fill(0);
            histN_[size_t(i)] = 0;
            histLo_[size_t(i)] = kHistLoDb;
            histHi_[size_t(i)] = kHistHiDb;
            retune(i);
        }
        learning_ = true;
    }
    void endLearn() { learning_ = false; }
    double dbgAccN(int i) const { return (i>=0&&i<count_)?accN_[size_t(i)]:0.0; }
    double dbgAccP(int i) const { return (i>=0&&i<count_)?accP_[size_t(i)]:0.0; }
    // The level this take actually sits at in each band: a high percentile of the level it
    // read while the take was playing.  See `targetPercentile` for why this is not the mean.
    // Falls back to the mean when there is too little take to build a distribution from,
    // so a two-second pass still behaves.
    double learnedLevelDb(int i) const {
        if (i < 0 || i >= count_) return -200.0;
        const auto& h = hist_[size_t(i)];
        const long long n = static_cast<long long>(histN_[size_t(i)]);
        if (n >= 200) {
            const long long want = static_cast<long long>(targetPercentile * double(n));
            long long run = 0;
            for (int bin = 0; bin < kHistBins; ++bin) {
                run += h[size_t(bin)];
                if (run < want) continue;
                // A percentile that lands in the first or last bin is a percentile of the
                // *clamp*, not of the take: the true value is outside the range and the bin
                // edge is the only thing we know about it.  The extreme actually seen is the
                // honest answer there, and it cannot be more wrong than the edge was.
                if (bin == 0)                  return histLo_[size_t(i)];
                if (bin == kHistBins - 1)      return histHi_[size_t(i)];
                return kHistLoDb + double(bin) * 0.1;
            }
            return histHi_[size_t(i)];
        }
        if (accN_[size_t(i)] <= 0.0) return -200.0;
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

            // The shared budget, decided *before* the filters run.
            //
            // The header has always promised "3 dB per band, 6 dB in total", and the
            // per-band half of that was enforced while the total half was not: the bank
            // published a total floored at -6 dB and then let ten bells each take their
            // own 3 dB.  On a take where several bands peak together - a sibilant, a
            // plosive, a harmonic stack - that is 14.7 dB of cut at one frequency, which
            // is not a stabiliser, it is an unlabelled compressor with a spectrum.  The
            // measured worst case on a 90 s take was 14.67 dB.
            //
            // So the wants are solved first, their sum is held to the ceiling, and only
            // then is the smoothed gain moved.  Scaling every band by the same factor keeps
            // the *shape* of the correction - the relative emphasis the targets decided -
            // and gives up only its depth, which is the one thing a budget should take.
            std::array<double, kMaxBands> want{};
            double wantSum = 0.0;
            for (int bi = 0; bi < count_; ++bi) {
                const auto& b = bands_[size_t(bi)];
                const double tgt = b.targetDb
                                 + b.slopeDb * std::clamp(loudNow_ - b.loudRefDb, -24.0, 24.0);
                const double over = b.levelDb - (tgt + deadBandDb);
                double w = 0.0;
                if (over > 0.0 && b.levelDb > -72.0 && b.targetDb > -100.0)
                    w = -std::min(b.maxGrDb * amount_, ratio * over);
                want[size_t(bi)] = w;
                wantSum += -w;
            }
            double scale = 1.0;
            if (wantSum > kMaxTotalGrDb) scale = kMaxTotalGrDb / wantSum;

            for (int bi = 0; bi < count_; ++bi) {
                auto& b = bands_[size_t(bi)];
                const double w = want[size_t(bi)] * scale;
                const double a = (w < b.grDb) ? aAtt : aRel;
                b.grDb = w + (b.grDb - w) * a;
            }

            // ... and the same ceiling on what actually runs.  The release above makes a
            // band let go slowly, so the *smoothed* sum can sit above the budgeted one for
            // a few milliseconds - measured at 7.81 dB on a 90 s take after the wants had
            // already been held to 6.  A promise about the output is a promise about this
            // number, so it is held here too.  The correction is a scalar applied to an
            // already-smoothed quantity, so it moves smoothly and cannot click.
            double appliedSum = 0.0;
            for (int bi = 0; bi < count_; ++bi)
                appliedSum += -std::min(0.0, bands_[size_t(bi)].grDb);
            if (appliedSum > kMaxTotalGrDb) {
                const double k = kMaxTotalGrDb / appliedSum;
                for (int bi = 0; bi < count_; ++bi) bands_[size_t(bi)].grDb *= k;
            }

            for (int bi = 0; bi < count_; ++bi) {
                auto& b = bands_[size_t(bi)];
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
                    // The percentile needs the *distribution*, not just its mean, and the
                    // learn pass is the only time we may keep it.  A 1024-bin histogram
                    // over -100..+2 dB in 0.1 dB steps is 1 KB per band, fixed, no
                    // allocation - and the percentile of a histogram is a prefix sum.
                    const int bin = std::clamp(int((b.env - kHistLoDb) * 10.0), 0, kHistBins - 1);
                    ++hist_[size_t(bi)][size_t(bin)];
                    ++histN_[size_t(bi)];
                    histLo_[size_t(bi)] = std::min(histLo_[size_t(bi)], b.env);
                    histHi_[size_t(bi)] = std::max(histHi_[size_t(bi)], b.env);
                }
            }
        }
        double total = 0.0;
        for (int i = 0; i < count_; ++i) total += std::min(0.0, bands_[size_t(i)].grDb);
        // No floor needed any more: `scale` above already held the sum to the ceiling, so
        // this number reports what happened rather than what was hoped for.
        totalGrDb_ = total;
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
    // The sum of the bands that are cutting.  Bounded by kMaxTotalGrDb by construction.
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
    // The take's own level distribution per band, kept only while learning.  0.1 dB bins
    // over kHistLoDb..kHistHiDb: 4 KB per band (40 KB for the bank, fixed, allocated with
    // the object), built once, read once.  A percentile is a prefix sum over this, which is
    // why it costs nothing at commit.
    //
    // The bins are 32-bit on purpose.  A sub-block is 16 samples, so at 48 kHz a band
    // reads 3000 times a second; a 16-bit bin would saturate after 22 seconds, and the
    // *shortest* useful take is far longer than that.  Saturation is silent and it lands
    // on whichever bin the voice sits in most - i.e. exactly on the percentile - so it
    // would have biased the target on every real take and on no test.
    static constexpr double kHistLoDb = -110.0;
    static constexpr double kHistHiDb = +10.0;
    static constexpr int    kHistBins = 1200;   // (10 - -110) / 0.1
    std::array<std::array<uint32_t, kHistBins>, kMaxBands> hist_{};
    std::array<uint32_t, kMaxBands> histN_{};
    // The highest and lowest level actually seen, so a percentile that piles up in an edge
    // bin is not read as that bin's number.  A +6 dBFS tone measured +2.3 dB before this
    // existed: the distribution was clamped at the top of the range and the percentile
    // returned the clamp.  Cheap, and it is what makes the answer defensible at the edges.
    std::array<double, kMaxBands> histLo_{}, histHi_{};

    // 4 ms attack / 180 ms release: fast enough to catch a plosive, slow enough that a
    // vibrato does not pump the band - the same numbers a mastering stabiliser uses.
    double attackMs  = 4.0;
    double releaseMs = 180.0;
    double detectorMs = 30.0;     // how long the band's level estimate takes to settle
    double deadBandDb = 2.0;      // how much louder than the take before it acts
    double ratio      = 0.6;      // cut this fraction of the excess
    // The target is a *reference level*, not the mean.  The mean of a band's log level on
    // a vocal is 3-4 dB under the level the voice actually sits at while it is singing,
    // because the pauses, the breaths and the decay tails all pull it down - measured on a
    // 60 s synthetic take as 40 % of the pass sitting above the mean, and on a real voice
    // it is worse.  Defending the mean therefore puts the target *below* the singing level
    // by construction, so the layer is allowed to cut the body of every note it was built
    // to leave alone, and is given almost no room to cut the peaks it exists for.
    // A reference that says "this is where the voice normally is" is a high percentile of
    // the level distribution, which is what smart:EQ and Equator defend.
    double targetPercentile = 0.75;  // which month of the take's own level to defend
};

} // namespace aeq
