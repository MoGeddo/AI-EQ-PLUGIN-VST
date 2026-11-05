// A band that locks onto the singer's pitch and follows it.
//
// Of the four reference plugins, this is the one the user singled out ("مهم جدا
// بالنسبالي اوي"): Antares' Vocal EQ locks a band to the fundamental - or to one of
// its harmonics - and the band moves with the note instead of sitting still.  On a
// vocal the interesting energy is not at a fixed frequency, it is at 1x, 2x, 3x of
// whatever note is being sung, so a static bell either misses the note half the time
// or has to be so wide it is no longer an EQ.
//
// Two things follow from "locks on and follows", and both had to be solved:
//
//   FOLLOW   the centre has to track f0 without clicking.  Recomputing a bell's
//            coefficients every block and running it on the same state is what makes
//            a tracked EQ sound watery: g = tan(pi f/fs) jumps but the two
//            integrators holding the filter's memory do not, so the filter's memory
//            is suddenly at the wrong frequency and puts a step in the output.  Here
//            the centre glides in log-frequency (a semitone is a constant distance
//            there) and coefficients are recomputed every 16 samples: 3 kHz of
//            control rate, 0.33 ms at 48 kHz - below the ear's resolution but far
//            above any vibrato (5-7 Hz) or register change.
//
//   HOLD     the gain is not a fixed dB value: the band measures its own content and
//            moves gain until that content sits at a target level.  That is the
//            "hold the frequency at a set level" behaviour, and it is why the band
//            needs no threshold - the target *is* the level learned from the take.
//            It is feed-forward from the input, so it cannot ring, and it is
//            asymmetric by default: only cutting, because pulling a quiet harmonic
//            up pulls the noise floor up with it.
//
//   VOICE    a pitch tracker sometimes drops an octave for a frame - period doubling
//            is a genuine ambiguity in YIN - and a band that believed it would slam
//            an octave away for 50 ms, which is audible as a chirp.  So a jump larger
//            than 1.35x has to agree with itself three times before it is taken,
//            while a normal step (vibrato, a whole tone) is followed at once.
//
// Topology is the trapezoidal state variable filter (Zavalishin / Simper): it stays
// stable under coefficient changes, which is exactly the property a moving band
// needs, and its bell is exact - the gain at the centre is the requested dB.  The
// test measures that instead of trusting it (0.00 dB at -12 dB requested).
//
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace aeq {

class TrackedBand {
public:
    enum class Mode {
        Pitch,   // centre = harmonic * f0, fed by the pitch tracker
        Fixed    // centre = setFixedHz()
    };

    struct Tuning {
        Mode   mode       = Mode::Pitch;
        int    harmonic   = 1;
        double q          = 6.0;
        // Width the *detector* listens with.  0 = same as q.  The correction may be a
        // normal bell while the level has to be read out of one harmonic: the analyser
        // that decided the target reads 1/12-octave bands (Q ~ 12), and a detector 5x
        // wider than that reads louder and then cuts a take it was supposed to leave
        // alone (test_layers caught 5.2 dB of exactly that).
        double detectQ    = 0.0;
        double fixedHz    = 1000.0;
        double targetDb   = -30.0;   // level to hold, dBFS RMS of the band's content
        double rangeDown  = 12.0;    // how far it may cut   (0 .. 24 dB)
        double rangeUp    = 0.0;     // how far it may boost (0 .. 12 dB)
        double attackMs   = 6.0;
        double releaseMs  = 140.0;
        double glideMs    = 25.0;    // 110 Hz -> 220 Hz arrives in ~4 x 25 ms
        double holdOffSec = 0.25;    // unvoiced this long -> gain returns to 0 dB
        double deadBandDb = 0.0;     // act only on the part of the error beyond this
        // Loudness-linked target: how many dB this band follows the voice per dB the
        // voice gets louder.  0 = hold the absolute level (the default: this is the band
        // that "holds a frequency at a set level"), 1 = let it travel with loudness (i.e.
        // do nothing, which is what a dry take already does).  The corpus study
        // (analysis/scripts/structure_moves.py, 15 dry stems) supplies the number per
        // frequency: 0.15 at 100 Hz, 0.65 at 10 kHz, >= 1 in the mids.
        double slopeDb    = 0.0;
        double loudRefDb  = -30.0;   // loudness of the take this layer was learned on
        int    jumpVotes  = 3;       // agreement needed for an octave-sized jump
    };

    static constexpr int    kSub = 16;
    static constexpr double kPi  = 3.14159265358979323846;

    void prepare(double sampleRate) {
        fs_ = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        for (auto& c : ch_) { c.det.reset(); c.bell.reset(); }
        env_ = 0.0;
        gainDb_ = 0.0;
        levelDb_ = -120.0;
        logFc_ = std::log(std::max(20.0, tuning_.fixedHz));
        f0_ = 0.0;
        pendingHz_ = 0.0;
        votes_ = 0;
        voiced_ = false;
        unvoicedSec_ = 0.0;
        snap_ = false;
        loudDb_ = tuning_.loudRefDb;
    }

    void setTuning(const Tuning& t) {
        tuning_ = t;
        tuning_.q          = std::max(0.3, std::min(40.0, tuning_.q));
        tuning_.harmonic   = std::max(1, std::min(32, tuning_.harmonic));
        tuning_.rangeDown  = std::max(0.0, std::min(24.0, tuning_.rangeDown));
        tuning_.rangeUp    = std::max(0.0, std::min(12.0, tuning_.rangeUp));
        tuning_.targetDb   = std::max(-90.0, std::min(0.0, tuning_.targetDb));
        tuning_.jumpVotes  = std::max(1, std::min(12, tuning_.jumpVotes));
    }
    const Tuning& tuning() const { return tuning_; }
    void setTargetDb(double db) { tuning_.targetDb = std::max(-90.0, std::min(0.0, db)); }
    void setFixedHz(double hz)  { tuning_.fixedHz = std::max(20.0, hz); }

    // Control input, at whatever rate the tracker publishes.  voiced = false covers
    // consonants, breaths and silence.
    // Loudness of the block about to be processed (any consistent reference - only the
    // difference from loudRefDb is used).  Called once per block.
    void setLoudness(double db) { loudDb_ = db; }

    void setPitch(double f0Hz, bool voiced) {
        if (!voiced || !(f0Hz > 20.0) || f0Hz > 2000.0) {
            voiced_ = false;
            votes_ = 0;
            pendingHz_ = 0.0;
            return;
        }
        voiced_ = true;
        if (f0_ <= 0.0) {                        // first note, or the first after a gap
            f0_ = f0Hz; votes_ = 0; pendingHz_ = 0.0;
            snap_ = true;                        // start *on* the note, never swoop into it
            return;
        }
        // after a real gap there is nothing to glide from either - the previous note is
        // stale by then, and gliding from it is 200 ms of a band travelling through the
        // whole voice.  Snap instead.
        if (unvoicedSec_ > 1.0) { f0_ = f0Hz; votes_ = 0; pendingHz_ = 0.0; snap_ = true; return; }

        const double ratio = f0Hz / f0_;
        if (ratio < 1.35 && ratio > 0.74) {          // a normal step: take it now
            f0_ = f0Hz;
            votes_ = 0;
            pendingHz_ = 0.0;
            return;
        }
        const bool agrees = pendingHz_ > 0.0 &&
                            std::abs(std::log2(f0Hz / pendingHz_)) < 0.06;   // ~4 %
        if (agrees) ++votes_; else { votes_ = 1; pendingHz_ = f0Hz; }
        if (votes_ >= tuning_.jumpVotes) { f0_ = f0Hz; votes_ = 0; pendingHz_ = 0.0; }
    }

    // Buffer is modified in place; r may be null (mono).
    void process(float* l, float* r, int n) {
        if (!l || n <= 0) return;
        for (int off = 0; off < n; off += kSub) {
            const int m = std::min(kSub, n - off);
            const double fc = updateCentre(m);
            const double g = std::tan(kPi * std::min(fc, 0.45 * fs_) / fs_);
            const double qDet = tuning_.detectQ > 0.0 ? tuning_.detectQ : tuning_.q;
            const double kDet = 1.0 / qDet;

            const double coefDet = std::exp(-double(m) / (0.001 * 4.0 * fs_));
            const double aAtt = std::exp(-double(m) / (0.001 * std::max(0.5, tuning_.attackMs) * fs_));
            const double aRel = std::exp(-double(m) / (0.001 * std::max(1.0, tuning_.releaseMs) * fs_));

            // the gain this sub-block should reach, from what the previous 16 samples saw
            const double meas = 10.0 * std::log10(std::max(env_, 1e-18));
            levelDb_ = meas;
            // The target is not a fixed level: it moves with the voice by the corpus
            // slope, so a band the corpus says barely follows loudness (0.19 at 100 Hz)
            // is pulled down as soon as the singer pushes.
            const double dLoud = std::clamp(loudDb_ - tuning_.loudRefDb, -24.0, 24.0);
            const double targetNow = tuning_.targetDb + tuning_.slopeDb * dLoud;
            // slope 1 means "let this band travel with loudness", and a band that is left
            // alone does exactly that - so the correct action is no action.  Computed as
            // target - measured instead, it would chase the difference between the
            // analyser's 1/3-octave level and this band's own detector, i.e. a 2.4 dB
            // offset that has nothing to do with the voice.
            double want = (tuning_.slopeDb >= 0.999) ? 0.0 : (targetNow - meas);
            // The dead band is what makes this a stabiliser instead of a compressor on one
            // harmonic: inside it the band does nothing, so an ordinary note passing
            // through is not ridden.  Outside it the band aims at the target properly -
            // two earlier versions got this wrong in opposite ways: one removed only the
            // part beyond the dead band (settled 3 dB away from the level it was told to
            // hold), the other held its gain inside the dead band (a ratchet: it engaged
            // on a peak and never let go, cutting the take it had learned by 3.7 dB).
            if (want > -tuning_.deadBandDb && want < tuning_.deadBandDb) want = 0.0;
            want = std::max(-tuning_.rangeDown, std::min(want, tuning_.rangeUp));
            if (meas < -75.0) want = 0.0;                       // nothing there: don't EQ noise
            // The voice gate belongs to the *pitch* band: a band placed at a fixed
            // frequency has no reason to stop working between words.  And holdOffSec = 0
            // means "do not wait", not "wait zero seconds" - the difference between the
            // two was a band that measured correctly and then did nothing at all.
            if (tuning_.mode == Mode::Pitch && tuning_.holdOffSec > 0.0 &&
                !voiced_ && unvoicedSec_ >= tuning_.holdOffSec) want = 0.0;
            const double aGain = (want < gainDb_) ? aAtt : aRel;
            gainDb_ = want + (gainDb_ - want) * aGain;

            const double A  = std::pow(10.0, gainDb_ / 40.0);
            const double kB = 1.0 / (tuning_.q * A);
            const double m1 = kB * (A * A - 1.0);               // bell mix: exact dB at fc

            for (int i = 0; i < m; ++i) {
                const int idx = off + i;
                env_ = tickOne(ch_[0], l[idx], g, kDet, qDet, coefDet, kB, m1);
                if (r) env_ = tickOne(ch_[1], r[idx], g, kDet, qDet, coefDet, kB, m1);
            }
            unvoicedSec_ = voiced_ ? 0.0 : unvoicedSec_ + double(m) / fs_;
        }
    }

    // ---- readouts (editor + tests) -------------------------------------------
    double centreHz() const { return std::exp(logFc_); }
    double gainDb() const   { return gainDb_; }
    double levelDb() const  { return levelDb_; }
    double targetDb() const { return tuning_.targetDb; }
    double targetNowDb() const {
        const double d = std::clamp(loudDb_ - tuning_.loudRefDb, -24.0, 24.0);
        return tuning_.targetDb + tuning_.slopeDb * d;
    }
    double slope() const { return tuning_.slopeDb; }
    double trackedF0() const { return f0_; }
    bool   locked() const   { return tuning_.mode == Mode::Fixed || f0_ > 0.0; }
    bool   voiced() const   { return voiced_; }

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
    };

    double updateCentre(int m) {
        double want;
        if (tuning_.mode == Mode::Fixed)  want = tuning_.fixedHz;
        else if (f0_ > 0.0)               want = f0_ * tuning_.harmonic;
        else                              want = std::exp(logFc_);   // held while unvoiced

        want = std::max(20.0, std::min(want, 0.45 * fs_));
        const double lw = std::log(want);
        if (snap_) { logFc_ = lw; snap_ = false; return want; }
        const double glide = std::exp(-double(m) / (0.001 * std::max(1.0, tuning_.glideMs) * fs_));
        logFc_ = lw + (logFc_ - lw) * glide;
        return std::exp(logFc_);
    }

    // Detector -> (gain held for the sub-block) -> bell.  Returns the new power estimate.
    inline double tickOne(Chan& c, float& sample, double g, double kDet, double qDet,
                          double coefDet, double kB, double m1) {
        const double x = double(sample);
        double lp = 0.0, bp = 0.0;
        c.det.tick(x, g, kDet, lp, bp);
        // A sine of amplitude A at the centre leaves the band-pass at Q*A, so dividing
        // by Q makes the detector read the signal's own level, not the filter's gain.
        const double p = (bp / qDet) * (bp / qDet);
        const double e = env_ + (p - env_) * (1.0 - coefDet);

        double blp = 0.0, bbp = 0.0;
        c.bell.tick(x, g, kB, blp, bbp);
        sample = float(x + m1 * bbp);
        return e;
    }

    double fs_ = 48000.0;
    Tuning tuning_{};
    std::array<Chan, 2> ch_{};
    double env_ = 0.0;          // mean square of the band content
    double gainDb_ = 0.0;
    double levelDb_ = -120.0;
    double logFc_ = std::log(1000.0);
    double f0_ = 0.0;
    double pendingHz_ = 0.0;
    int    votes_ = 0;
    double loudDb_ = -30.0;
    bool   voiced_ = false;
    bool   snap_ = false;         // set by setPitch, consumed by the audio thread
    double unvoicedSec_ = 0.0;
};

} // namespace aeq
