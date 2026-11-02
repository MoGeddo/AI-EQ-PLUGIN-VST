// AEQ Restore core - no JUCE, no allocation in the audio path once prepared.
//
// Signal chain (this is deliberately the FIRST insert in the chain):
//    input trim -> DC/rumble -> adaptive 24 dB/oct HPF -> corrective EQ cascade
//    -> dynamic de-esser -> output trim
//
// The analysis runs on a background thread over whatever the DAW plays, so the
// user can let the take run from beginning to end and the plugin keeps learning.
#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include <cmath>
#include <algorithm>

#include "Analyzer.h"
#include "Biquad.h"
#include "DeEsser.h"
#include "EqSolver.h"
#include "ProblemDetector.h"
#include "../model/TargetCurve.h"

namespace aeq {

struct RestoreSettings {
    // analysis
    bool   learn = true;             // keep accumulating while playing
    bool   autoApply = true;         // apply the solved correction automatically
    int    maxBands = 5;
    int    mode = 0;                 // 0 = Restore (fix problems), 1 = Match corpus
    double maxGainDb = 3.5;          // restore stays gentle: this is not a re-voice
    double correctionAmount = 1.0;   // 0..1 - the "standard %" macro

    // manual overrides / user intent
    double hpfHz = 0.0;              // 0 = decide from the measurement
    double deEssAmount = 0.0;        // 0..1, 0 = decide automatically
    double airTrimDb = 0.0;          // user lift above 8 kHz
    bool   bypass = false;

    // taste axes from the corpus study: warmth <-> presence, body <-> air
    double axis1 = 0.0;              // -1 .. +1
    double axis2 = 0.0;              // -1 .. +1

    // How much sung audio to collect before the correction is allowed into the
    // signal path.  0 = apply immediately (the CLI and the gates want this).
    // The plugin sets ~30 s because a curve solved from four seconds of a verse
    // is not a curve for the performance - and the first thing a mixer hears
    // must be the answer, not a guess that keeps changing.
    double settleSeconds = 0.0;
};

struct RestoreState {
    std::vector<BandSpec> corrective;   // what the solver produced
    std::vector<BandSpec> resonance;    // narrow static-resonance cuts
    double hpfHz = 0.0;
    double deEssThresholdDb = -30.0;
    double deEssMaxGrDb = 0.0;
    std::vector<Problem> problems;
    double distBeforeDb = 0.0, distAfterDb = 0.0;
    double fixBeforeDb = 0.0, fixAfterDb = 0.0;   // over the regions we acted on
    double measuredF0 = 0.0;
    double analyzedSeconds = 0.0;   // sung audio analysed so far
    bool   settled = false;         // enough audio to trust the numbers
    bool   valid = false;
};

class RestoreCore {
public:
    void prepare(double sampleRate, int maxBlockSize) {
        fs_ = sampleRate;
        maxBlock_ = std::max(1, maxBlockSize);
        analyzer_.prepare(fs_);
        hpf_.set(80.0, fs_);
        deEsser_.prepare(fs_);
        eqLeft_.reset();
        eqRight_.reset();
        scratch_.assign(maxBlock_, 0.0f);
        state_.valid = false;
        updateBands();
    }

    void reset() {
        analyzer_.reset();
        eqLeft_.reset(); eqRight_.reset();
        hpf_.reset(); deEsser_.reset();
        std::lock_guard<std::mutex> lk(stateMx_);
        state_ = RestoreState{};
        updateBands();
    }

    // ---------------------------------------------------------------- audio
    void processBlock(float* left, float* right, int n) {
        if (n <= 0) return;

        // 0. pick up any correction solved in the background.  Coefficients are
        //    only ever *applied* here, on the audio thread, so the processing
        //    objects are never written from another thread.
        pollAndApply();

        // 1. feed the analyser a mono view (vocal is essentially mono).
        //    The analyser is shared with the background solver, so we take the
        //    lock only if it is free: if the solver happens to be reading, we
        //    drop this block.  These are long-term statistics, so a dropped
        //    block changes nothing and the audio thread never waits.
        if (settings_.learn && !settings_.bypass) {
            for (int i = 0; i < n; ++i)
                scratch_[i] = 0.5f * (left[i] + (right ? right[i] : left[i]));
            std::unique_lock<std::mutex> lk(analyzeMx_, std::try_to_lock);
            if (lk.owns_lock()) {
                analyzer_.processBlock(scratch_.data(), n);
                learnProgress_ = analyzer_.activeFraction();
            }
        }

        if (settings_.bypass) return;

        // 2. chain
        for (int i = 0; i < n; ++i) {
            double l = left[i];
            l = hpf_.process(l);
            l = eqLeft_.process(l);
            l = deEsser_.process(l);
            l *= outGain_;
            left[i] = static_cast<float>(l);
            if (right) {
                double r = right[i];
                r = hpf_.process(r);
                r = eqRight_.process(r);
                r = deEsser_.process(r);
                r *= outGain_;
                right[i] = static_cast<float>(r);
            }
        }
    }

    // ------------------------------------------------------------- analysis
    // Call from a timer (message/timer thread) - never from the audio thread.
    void refresh() {
        std::lock_guard<std::mutex> lk(analyzeMx_);
        refreshLocked();
    }

    // Which blocks may call refresh(): the background thread (plugin) or the
    // main thread (CLI).  Never the audio thread - that is what pollAndApply is.
    void refreshLocked() {
        if (!analyzer_.ready()) return;
        Measurements m;
        m.freqs = analyzer_.grid().f;
        m.bandDb = analyzer_.bandLevelsDb();
        m.bandSdDb = analyzer_.bandSdDb();
        analyzer_.excessStats(m.excessMean, m.excessSd, tmpCount_);
        m.envDb = analyzer_.envelopeLevelsDb();
        m.envSdDb = analyzer_.envelopeSdDb();
        m.envFrames.resize(m.freqs.size());
        for (size_t i = 0; i < m.freqs.size(); ++i)
            m.envFrames[i] = analyzer_.envelopeFrames(static_cast<int>(i));
        analyzer_.sibilance(m.sibMed, m.sibP90);
        m.medianF0 = analyzer_.medianF0();
        m.dynamicRange = analyzer_.dynamicRangeDb();
        m.peakDbfs = analyzer_.peakDbfs();
        m.dcOffset = analyzer_.dcOffset();
        m.clipRate = analyzer_.clipRate();
        m.clipSamples = static_cast<double>(analyzer_.clipSamples());
        m.activeFraction = analyzer_.activeFraction();
        m.noiseFloorDb = analyzer_.minActiveRmsDb();
        m.maxRmsDb = analyzer_.maxRmsDb();
        m.nActive = static_cast<int>(analyzer_.activeFrames());

        // trustworthy range: above 16 kHz we have no target, below 100 Hz the
        // 1/12-oct bands are narrower than the harmonics
        SolveInput in;
        in.freqs = m.freqs;
        in.levelDb = m.bandDb;
        in.valid.assign(m.freqs.size(), 0.0);
        for (size_t i = 0; i < m.freqs.size(); ++i)
            in.valid[i] = (m.freqs[i] >= 100.0 && TargetCurve::inMeasuredRange(m.freqs[i])) ? 1.0 : 0.0;

        SolverConfig sc;
        sc.mode = settings_.mode == 1 ? MatchMode::Match : MatchMode::Restore;
        sc.maxBands = settings_.maxBands;
        sc.maxGainDb = settings_.maxGainDb;
        if (sc.mode == MatchMode::Restore) { sc.maxQ = 1.2; sc.deadZoneDb = 2.5; }
        auto corr = solver_.solve(in, sc, fs_);
        const auto probs = detector_.detect(m);
        // the Standard % macro scales the *solved* correction (never the manual
        // taste moves, which the user sets on purpose)
        const double amt = std::clamp(settings_.correctionAmount, 0.0, 1.0);
        for (auto& b : corr) b.gainDb *= amt;
        const double before = EqSolver::distanceToTarget(in, {}, fs_);
        const double after  = EqSolver::distanceToTarget(in, corr, fs_);
        // honest secondary metric: did we actually fix the regions we chose to
        // touch?  (the overall distance can rise slightly when the dead-zone
        // refuses to chase the family's natural variation - that is by design)
        const double fixBefore = EqSolver::distanceOnActive(in, {}, fs_, 2.5);
        const double fixAfter  = EqSolver::distanceOnActive(in, corr, fs_, 2.5);

        // narrow cuts for static resonances (kept separate: they are surgical)
        std::vector<BandSpec> reso;
        double resoTotal = 0.0;
        for (const auto& p : probs) {
            if (p.action == Action::CutNarrow && resoTotal < 8.0) {
                BandSpec b{BandType::Peak, p.freq, p.q, std::max(p.gainDb, -7.0)};
                b.gainDb *= amt;
                if (std::abs(b.gainDb) < 0.1) continue;
                reso.push_back(b);
                resoTotal += std::abs(b.gainDb);
            }
        }

        // ---- HPF decision ------------------------------------------------
        // Policy: find where this take's own low end starts to fall away, then
        // lift the corner to at most the corpus standard (172 Hz frame) and only
        // when there is actually something below it worth removing. We never
        // push a corner higher than the corpus sits, because that would eat the
        // fundamental of a low voice.
        double corner = settings_.hpfHz;
        if (corner <= 0.0) {
            const double measured = measuredLowCorner(m);
            bool hasRumble = false;
            for (const auto& p : probs) if (p.action == Action::RaiseHpf) hasRumble = true;
            const double standard = 172.0;
            corner = 60.0;
            if (hasRumble || measured < 100.0) corner = std::min(standard, std::max(measured, 80.0));
            else corner = std::min(measured, standard);
            if (m.medianF0 > 0) corner = std::min(corner, std::max(85.0, 0.62 * m.medianF0));
        }
        corner = std::clamp(corner, 30.0, 220.0);

        // de-esser decision
        double deThr = -30.0, deGr = 0.0;
        for (const auto& p : probs) {
            if (p.action == Action::DeEss) { deGr = std::min(6.0, std::abs(p.gainDb) * 0.8); deThr = -34.0; }
        }
        if (settings_.deEssAmount > 0.0) deGr = 8.0 * settings_.deEssAmount;

        {
            std::lock_guard<std::mutex> lk(stateMx_);
            state_.corrective = corr;
            state_.resonance = reso;
            state_.hpfHz = corner;
            state_.deEssThresholdDb = deThr;
            state_.deEssMaxGrDb = deGr;
            state_.problems = probs;
            state_.distBeforeDb = before;
            state_.distAfterDb = after;
            state_.fixBeforeDb = fixBefore;
            state_.fixAfterDb = fixAfter;
            state_.measuredF0 = m.medianF0;
            state_.analyzedSeconds = analyzer_.activeSeconds();
            state_.settled = state_.analyzedSeconds >= settings_.settleSeconds;
            state_.valid = true;
            measurements_ = m;
        }

        // hand the solution to the audio thread; it applies it at the top of the
        // next block, so no processing object is ever written from here
        {
            std::lock_guard<std::mutex> lk(pendingMx_);
            pendingBands_ = activeBandsLocked();
            pendingHpf_ = corner;
            pendingDeEssOn_ = deGr > 0.05;
            pendingDeThr_ = deThr;
            pendingDeGr_ = deGr;
            pendingSettled_ = state_.settled;
            ++pendingVersion_;
        }
        pendingSeen_.store(false);
    }

    // apply a pending correction (audio thread, called from processBlock)
    void pollAndApply() {
        if (pendingSeen_.load(std::memory_order_acquire)) return;
        std::unique_lock<std::mutex> lk(pendingMx_, std::try_to_lock);
        if (!lk.owns_lock()) return;                 // solver is writing; try again next block
        // Analyse-then-apply: until enough of the performance has been heard,
        // only the manual taste moves reach the signal path.  The corrective
        // curve would otherwise be a different filter every few seconds during
        // the first pass, which is both audible and dishonest.
        const bool settled = pendingSettled_ || settings_.settleSeconds <= 0.0;
        std::vector<BandSpec> bands = settled ? pendingBands_ : manualBands();
        eqLeft_.setBands(bands, fs_);
        eqRight_.setBands(bands, fs_);
        hpf_.set(settled && pendingHpf_ > 0 ? pendingHpf_ : 70.0, fs_);
        deEsser_.setEnabled(settled && pendingDeEssOn_);
        if (settled) deEsser_.setParameters(pendingDeThr_, pendingDeGr_);
        pendingSeen_.store(true, std::memory_order_release);
    }

    // ------------------------------------------------------------- settings
    void setSettings(const RestoreSettings& s) { settings_ = s; updateBands(); }
    const RestoreSettings& settings() const { return settings_; }

    RestoreState state() const {
        std::lock_guard<std::mutex> lk(stateMx_);
        return state_;
    }
    Measurements measurements() const {
        std::lock_guard<std::mutex> lk(stateMx_);
        return measurements_;
    }
    double learnProgress() const { return learnProgress_.load(); }
    Analyzer& analyzer() { return analyzer_; }

    // the full correction that is actually running (for the UI curve / delta)
    std::vector<BandSpec> activeBands() const {
        std::lock_guard<std::mutex> lk(stateMx_);
        return activeBandsLocked();
    }

    // the bands the *user* asked for, independent of any analysis (audio thread)
    std::vector<BandSpec> manualBands() const {
        std::vector<BandSpec> out;
        if (std::abs(settings_.airTrimDb) > 0.01)
            out.push_back({BandType::HighShelf, 9000.0, 0.7, settings_.airTrimDb});
        out.push_back({BandType::Peak, 250.0, 0.9, 2.5 * settings_.axis1});   // PC1 warm
        out.push_back({BandType::Peak, 330.0, 0.9, 1.5 * settings_.axis1});
        out.push_back({BandType::Peak, 3500.0, 0.8, 1.2 * settings_.axis1});  // PC1 presence
        out.push_back({BandType::Peak, 200.0, 1.0, 2.0 * settings_.axis2});   // PC2 body
        out.push_back({BandType::Peak, 2200.0, 1.2, 1.5 * settings_.axis2});
        return out;
    }

    std::vector<BandSpec> activeBandsLocked() const {
        std::vector<BandSpec> out = state_.corrective;
        out.insert(out.end(), state_.resonance.begin(), state_.resonance.end());
        const auto man = manualBands();
        out.insert(out.end(), man.begin(), man.end());
        return out;
    }

    double hpfFreq() const { std::lock_guard<std::mutex> lk(stateMx_); return state_.hpfHz; }
    double deEssGainReduction() const { return deEsser_.currentGainReductionDb(); }
    double sampleRate() const { return fs_; }

public:
    // Lowest frequency whose level is within 3 dB of the 200-400 Hz plateau.
    static double measuredLowCorner(const Measurements& m) {
        double plateau = 0.0; int n = 0;
        for (size_t i = 0; i < m.freqs.size(); ++i)
            if (m.freqs[i] >= 200 && m.freqs[i] <= 400) { plateau += m.bandDb[i]; ++n; }
        if (!n) return 60.0;
        plateau /= n;
        for (size_t i = 0; i < m.freqs.size(); ++i) {
            if (m.freqs[i] < 40 || m.freqs[i] > 400) continue;
            if (m.bandDb[i] > plateau - 3.0) return m.freqs[i];
        }
        return 60.0;
    }

private:
    void updateBands() {
        std::vector<BandSpec> bands = activeBands();
        eqLeft_.setBands(bands, fs_);
        eqRight_.setBands(bands, fs_);
        const double hf = hpfFreq();
        hpf_.set(hf > 0 ? hf : 70.0, fs_);
        RestoreState st;
        { std::lock_guard<std::mutex> lk(stateMx_); st = state_; }
        deEsser_.setEnabled(st.deEssMaxGrDb > 0.05);
        deEsser_.setParameters(st.deEssThresholdDb, st.deEssMaxGrDb);
    }

    double fs_ = 48000.0;
    int maxBlock_ = 512;
    RestoreSettings settings_;
    Analyzer analyzer_;
    HighPass24 hpf_;
    BiquadCascade eqLeft_, eqRight_;
    DeEsser deEsser_;
    EqSolver solver_;
    ProblemDetector detector_;
    std::vector<float> scratch_;
    std::vector<int> tmpCount_;
    std::atomic<double> learnProgress_{0.0};
    double outGain_ = 1.0;

    mutable std::mutex stateMx_;
    std::mutex analyzeMx_;                    // guards Analyzer (audio <-> solver)
    std::mutex pendingMx_;                    // guards the hand-off below
    std::vector<BandSpec> pendingBands_;
    double pendingHpf_ = 70.0, pendingDeThr_ = -30.0, pendingDeGr_ = 0.0;
    bool pendingDeEssOn_ = false;
    bool pendingSettled_ = true;
    std::atomic<bool> pendingSeen_{true};
    std::atomic<long> pendingVersion_{0};
    RestoreState state_;
    Measurements measurements_;
};

} // namespace aeq
