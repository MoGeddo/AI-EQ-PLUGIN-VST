// Band-split dynamic de-esser.
//
// The corpus measurement says the sibilance *average* is fine (median -10 dB)
// but the peaks are not (p90 -2.6 dB).  So this is a dynamics problem, not an EQ
// problem: a static notch would dull every "s" while leaving the peaks that
// actually hurt.  We split at 5 kHz, watch the 5-10 kHz band, and pull gain only
// while it is loud.
#pragma once
#include <cmath>
#include <algorithm>
#include "Biquad.h"

namespace aeq {

class DeEsser {
public:
    void prepare(double fs) {
        fs_ = fs;
        lowA_ = Biquad::lowPass(5000.0, 0.7071, fs);
        lowB_ = Biquad::lowPass(5000.0, 0.7071, fs);
        hiA_  = Biquad::highPass(5000.0, 0.7071, fs);
        hiB_  = Biquad::highPass(5000.0, 0.7071, fs);
        env_ = 0.0;
        grDb_ = 0.0;
        setAttackRelease(1.0, 80.0);
    }
    void reset() {
        lowA_.reset(); lowB_.reset(); hiA_.reset(); hiB_.reset();
        env_ = 0.0; grDb_ = 0.0;
    }
    void setAttackRelease(double attackMs, double releaseMs) {
        aAtt_ = std::exp(-1.0 / (0.001 * std::max(attackMs, 0.1) * fs_));
        aRel_ = std::exp(-1.0 / (0.001 * std::max(releaseMs, 1.0) * fs_));
    }
    // thresholdDb relative to full scale, maxReductionDb 0..12
    void setParameters(double thresholdDb, double maxReductionDb) {
        thr_ = thresholdDb;
        maxGr_ = std::max(0.0, std::min(12.0, maxReductionDb));
    }
    void setEnabled(bool e) { on_ = e; }

    inline double process(double x) {
        if (!on_ || maxGr_ <= 0.0) { grDb_ = 0.0; return x; }
        const double low = lowB_.process(lowA_.process(x));
        const double hi  = hiB_.process(hiA_.process(x));
        // envelope of the high band (fast attack, slow release)
        const double a = std::abs(hi);
        env_ = a > env_ ? aAtt_ * env_ + (1.0 - aAtt_) * a
                        : aRel_ * env_ + (1.0 - aRel_) * a;
        const double lvlDb = 20.0 * std::log10(std::max(env_, 1e-9));
        double target = 0.0;
        if (lvlDb > thr_) target = -std::min((lvlDb - thr_) * 0.6, maxGr_);
        // smooth the gain so it never clicks
        grDb_ = 0.75 * grDb_ + 0.25 * target;
        return low + hi * std::pow(10.0, grDb_ / 20.0);
    }
    double currentGainReductionDb() const { return -grDb_; }

private:
    double fs_ = 48000.0, aAtt_ = 0.0, aRel_ = 0.0;
    double thr_ = -30.0, maxGr_ = 4.0, env_ = 0.0, grDb_ = 0.0;
    bool on_ = false;
    Biquad lowA_, lowB_, hiA_, hiB_;
};

} // namespace aeq
