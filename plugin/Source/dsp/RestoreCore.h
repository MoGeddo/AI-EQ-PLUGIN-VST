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
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

#include "Analyzer.h"
#include "Biquad.h"
#include "DeEsser.h"
#include "TrackedBand.h"
#include "DynamicProfile.h"
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

// The display spectrum: a 120 ms moving average plus its peak hold, published as
// often as the plugin's pump timer runs.  Separate from RestoreState because it
// changes ~25 times a second and the state changes four times.
struct LiveSpectrum {
    std::vector<double> freqs, liveDb, peakDb;
    unsigned long long version = 0;
    bool valid = false;
};

// One dynamic layer: a band that sits on a harmonic of the singer's pitch, follows
// it, and holds that harmonic at the level the take had.  The numbers are decided at
// commit (they come from the take) and are saved with the project, because a restored
// project must sound the same without listening to the vocal again.
struct TrackedSpec {
    int    harmonic  = 1;
    double q         = 4.0;
    double detectQ   = 12.0;    // matches the analyser's 1/12-octave bands
    double targetDb  = -30.0;   // level to hold, dBFS
    double rangeDown = 6.0;
    double rangeUp   = 0.0;
    double glideMs   = 25.0;
    double deadBandDb = 3.0;
    // from the corpus table (DynamicProfile.h): how many dB this harmonic should move per
    // dB the voice gets louder, and the loudness of the take it was learned on
    double slopeDb   = 0.0;     // 0 = hold the level, 1 = let it follow loudness
    double loudRefDb = -30.0;
    double attackMs   = 20.0;
    double releaseMs  = 250.0;
};
constexpr int kTrackedMax = 4;      // storage; at most two layers are ever chosen

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
    int    trackedLayers = 0;       // bands in the path that follow the pitch
    double analyzedSeconds = 0.0;   // sung audio analysed so far
    bool   settled = false;         // enough audio to trust the numbers
    bool   valid = false;
};

class RestoreCore {
public:
    // The workflow the product is built around: listen to the whole take, the user
    // stops it, then it is frozen.
    //
    //   Learning   the audio passes through *bit-exactly* (verified by
    //              Tests/test_commit.cpp): a tool that is still listening must not
    //              be mixing.  Everything the analyser sees is accumulated here.
    //   Committed  one decision, solved from the whole take, applied with a 200 ms
    //              fade and then frozen.  Only the dynamic layers move afterwards.
    enum class Phase { Learning, Committed };

    Phase phase() const { return Phase(phase_.load(std::memory_order_acquire)); }
    bool  committed() const { return phase() == Phase::Committed; }
    // Frozen = the user stopped the pass, so the decision may not be re-solved.
    // Distinct from `committed()`, because "settle 0" (the CLI and the offline gates)
    // means "apply as soon as a decision exists and keep refining" - that one is
    // committed but not frozen.
    bool  frozen() const { return frozen_.load(std::memory_order_acquire); }

    // Stop the learn pass and freeze the decision.  Called from the message thread
    // (the editor's Stop button) or the analysis thread; never from audio.
    void commit() {
        {
            std::lock_guard<std::mutex> lk(analyzeMx_);
            drainFromAudioThread();
            refreshLocked();
        }
        phase_.store(int(Phase::Committed), std::memory_order_release);
        frozen_.store(true, std::memory_order_release);
        version_.fetch_add(1, std::memory_order_release);
    }

    // Put a saved decision back, already committed and frozen - what a DAW does when
    // it reopens a project: the user's answer is restored, not recomputed.
    void restoreDecision(const RestoreState& st) {
        {
            std::lock_guard<std::mutex> lk(stateMx_);
            state_ = st;
            state_.valid = true;
            state_.settled = true;
        }
        phase_.store(int(Phase::Committed), std::memory_order_release);
        frozen_.store(true, std::memory_order_release);
        applyGain_ = 1.0;                 // no fade: this is the state the user saved
        updateBands();
        version_.fetch_add(1, std::memory_order_release);
    }

    // Forget everything and listen again (the editor's "learn again").
    void relearn() {
        frozen_.store(false, std::memory_order_release);
        phase_.store(int(Phase::Learning), std::memory_order_release);
        reset();
        version_.fetch_add(1, std::memory_order_release);
    }

    // The dynamic layers currently in the signal path (0 when nothing was committed).
    int trackedCount() const { return trackedActive_; }
    TrackedSpec trackedSpec(int i) const {
        return (i >= 0 && i < trackedActive_) ? trackSpec_[size_t(i)] : TrackedSpec{};
    }
    // What a layer is doing right now - for the editor's readout ("٦٠٠ هرتز · −٥.٤ dB")
    // and for the tests.  Published as atomics: the band itself is written by the audio
    // thread only, and a torn double would show a random frequency on screen.
    double trackedLevelDb(int i) const {
        return (i >= 0 && i < kTrackedMax) ? trackedPubLevel_[size_t(i)].load(std::memory_order_relaxed) : -120.0;
    }
    double trackedGainDb(int i) const {
        return (i >= 0 && i < kTrackedMax) ? trackedPubGain_[size_t(i)].load(std::memory_order_relaxed) : 0.0;
    }
    double trackedSlope(int i) const {
        return (i >= 0 && i < kTrackedMax) ? trackSpec_[size_t(i)].slopeDb : 1.0;
    }
    double trackedCentreHz(int i) const {
        return (i >= 0 && i < kTrackedMax) ? trackedPubHz_[size_t(i)].load(std::memory_order_relaxed) : 0.0;
    }

    // Put saved layers back with the project file.  Goes through the same pending slot
    // as a solved decision, so the audio thread picks it up in pollAndApply().
    void setTrackedLayers(const std::vector<TrackedSpec>& v) {
        std::lock_guard<std::mutex> lk(pendingMx_);
        const int n = std::min<int>(int(v.size()), kTrackedMax);
        for (int i = 0; i < n; ++i) pendingTrack_[size_t(i)] = v[size_t(i)];
        pendingTrackCount_ = n;
        pendingSeen_.store(false, std::memory_order_release);   // make the audio thread look
    }

    void prepare(double sampleRate, int maxBlockSize) {
        fs_ = sampleRate;
        maxBlock_ = std::max(1, maxBlockSize);
        analyzer_.prepare(fs_);
        hpf_.set(80.0, fs_);
        deEsser_.prepare(fs_);
        eqLeft_.reset();
        eqRight_.reset();
        scratch_.assign(maxBlock_, 0.0f);   // kept for the report; processBlock never trusts it
        state_.valid = false;
        updateBands();
    }

    void reset() {
        applyGain_ = 0.0;
        fifoRead_.store(fifoWrite_.load(std::memory_order_acquire), std::memory_order_release);
        fifoDropped_.store(0);
        version_.fetch_add(1, std::memory_order_release);
        analyzer_.reset();
        eqLeft_.reset(); eqRight_.reset();
        hpf_.reset(); deEsser_.reset();
        for (auto& t : tracked_) { t.prepare(fs_); t.reset(); }
        trackedActive_ = 0;
        pendingTrackCount_ = 0;
        {
            std::lock_guard<std::mutex> lk(stateMx_);
            state_ = RestoreState{};
        }
        trackedActive_ = 0;
        {
            std::lock_guard<std::mutex> lk(pendingMx_);
            pendingTrackCount_ = 0;
        }
        // The lock above must be *released* before this: updateBands() calls
        // activeBands(), which takes stateMx_ again.  std::mutex is not recursive,
        // so holding it across that call deadlocked the calling thread - and that
        // caller is the audio thread (via requestReset) or the editor's Reset
        // button.  A frozen audio thread inside a DAW looks exactly like a crash:
        // the host stops the engine and, in FL Studio, drops the plugin.
        updateBands();
    }

    // ---------------------------------------------------------------- audio
    void processBlock(float* left, float* right, int n) {
        if (n <= 0) return;

        // 0. pick up any correction solved in the background.  Coefficients are
        //    only ever *applied* here, on the audio thread, so the processing
        //    objects are never written from another thread.
        pollAndApply();

        // 1. hand the analyser a mono view (a vocal is essentially mono) through a
        //    ring buffer, and nothing else.
        //
        //    The FFT-8192, the cepstrum and the pitch tracker used to run *inside*
        //    this callback: ~1000x the work of a biquad, once every 2048 samples,
        //    with a lock around it.  In FL Studio with a 512-sample buffer that is a
        //    multi-millisecond spike in a 10 ms budget - audible as the plugin
        //    stuttering and cutting out - and it allocated on the first frame, too.
        //    Analysis now happens on the timer thread (refresh() drains this FIFO);
        //    the audio thread only copies samples, which cannot fail, cannot block
        //    and cannot allocate.
        if (settings_.learn && !settings_.bypass) {
            unsigned long long w = fifoWrite_.load(std::memory_order_relaxed);
            const unsigned long long r = fifoRead_.load(std::memory_order_acquire);
            for (int j = 0; j < n; ++j) {
                if (w - r >= (unsigned long long) kFifoSize) {   // consumer is behind: drop
                    fifoDropped_.fetch_add(n - j, std::memory_order_relaxed);
                    break;
                }
                fifo_[size_t(w++ & kFifoMask)] = 0.5f * (left[j] + (right ? right[j] : left[j]));
            }
            fifoWrite_.store(w, std::memory_order_release);
        }
        if (settings_.bypass) return;

        // 1b. still learning: pass the audio through untouched, sample for sample.
        //     The chain is not run at all - not "at unity gain", not "bypassed by a
        //     flag inside the filters": not executed.  That is the only version of
        //     "learning changes nothing" that can be proved.
        if (!committed()) {
            applyGain_ = 0.0;
            return;
        }

        // 2. chain.
        //
        // The level match is measured, not estimated.  Removing a boom or a body
        // resonance takes energy out, and the vocal then sits quieter in the mix -
        // which is heard as "the plugin ruined the voice" even when every individual
        // move was right.  So the block's input and output energy are accumulated and
        // the gain is nudged toward "out = in" with a 1.5 s time constant: too slow to
        // ride the performance, fast enough that a correction does not change the
        // level of the track.  Limited to +-4 dB, so it can never become a level trick.
        // The reference is the signal *after* the low cut: removing rumble is the
        // job, and it must not be "compensated" back in as gain.  What is measured
        // against is only what the EQ and the de-esser then do to the voice.
        //
        // The wet signal is built into a scratch block rather than straight into the
        // buffer because of 3: the dynamic layers are block filters and they have to
        // sit *after* the static curve, not before it.  A hold placed before the curve
        // cuts twice - the restore it makes, then the curve's own dB on top of it - and
        // the core's own convergence gate (G5) caught exactly that as a pass that made
        // the take worse.
        double inSq = 0.0, outSq = 0.0;
        // outSq is taken on the *static* chain's output, not on the final signal: the
        // level match exists to give back what the curve took out of the voice, and a
        // dynamic layer is supposed to change the level of one harmonic - that is its
        // whole job.  Measuring after the layers let the match cancel up to 6 dB of a
        // 7.7 dB hold, i.e. it silently undid the feature (test_layers caught it).

        // 1d/3. the pitch feed for the dynamic layers: one report per block, from the
        // analyser, whose estimate arrives ~4 Hz.  The bands never step to it - they
        // glide in log-frequency, and need three agreeing reports before they believe an
        // octave-sized jump, because a tracker's octave slip is a chirp if it is taken.
        if (trackedActive_ > 0) {
            const double f0 = analyzer_.currentF0();
            const bool voiced = analyzer_.voicedNow();
            const double loud = loudPub_.load(std::memory_order_relaxed);
            for (int i = 0; i < trackedActive_; ++i) {
                tracked_[size_t(i)].setPitch(f0, voiced);
                tracked_[size_t(i)].setLoudness(loud);
            }
        }

        for (int base = 0; base < n; base += kMaxChunk) {
            const int m = std::min(kMaxChunk, n - base);

            for (int i = 0; i < m; ++i) {
                const int j = base + i;
                const double dryL = left[j];
                double l = hpf_.process(dryL);
                inSq += l * l;
                l = eqLeft_.process(l);
                l = deEsser_.process(l);
                l *= outGain_;
                dryChL_[size_t(i)] = static_cast<float>(dryL);
                outSq += l * l;
                wetChL_[size_t(i)] = static_cast<float>(l);
                if (right) {
                    const double dryR = right[j];
                    double r = hpf_.process(dryR);
                    inSq += r * r;
                    r = eqRight_.process(r);
                    r = deEsser_.process(r);
                    r *= outGain_;
                    dryChR_[size_t(i)] = static_cast<float>(dryR);
                    outSq += r * r;
                    wetChR_[size_t(i)] = static_cast<float>(r);
                }
            }

            // 3. dynamic layers: bands that sit on a harmonic of the pitch, follow it,
            //    and hold that harmonic at the level the take had after the curve.  That
            //    is the "move with the note and hold it at a set level" behaviour; it is
            //    silent while learning because the branch above already returned.
            for (int i = 0; i < trackedActive_; ++i) {
                auto& b = tracked_[size_t(i)];
                b.process(wetChL_.data(), right ? wetChR_.data() : nullptr, m);
                trackedPubLevel_[size_t(i)].store(b.levelDb(), std::memory_order_relaxed);
                trackedPubGain_[size_t(i)].store(b.gainDb(), std::memory_order_relaxed);
                trackedPubHz_[size_t(i)].store(b.centreHz(), std::memory_order_relaxed);
            }

            // 4. crossfade, not a gain ramp: `dry + a*(wet - dry)` is dry at a = 0,
            //    which is what makes the commit silent.  Scaling the wet path from zero
            //    turns the first block of a committed decision into a step to silence.
            for (int i = 0; i < m; ++i) {
                const int j = base + i;
                const double dryL = dryChL_[size_t(i)];
                const double l = dryL + applyGain_ * (double(wetChL_[size_t(i)]) - dryL);
                left[j] = static_cast<float>(l);
                if (right) {
                    const double dryR = dryChR_[size_t(i)];
                    const double r = dryR + applyGain_ * (double(wetChR_[size_t(i)]) - dryR);
                    right[j] = static_cast<float>(r);
                }
            }
        }

        // The 200 ms fade that puts a committed decision into the path.  Long enough
        // to be inaudible, short enough that "I pressed stop and it happened" is
        // still true (the gate measures the first change at <= 250 ms).
        if (applyGain_ < 1.0) {
            const double step = double(n) / (fs_ * 0.2);
            applyGain_ = std::min(1.0, applyGain_ + step);
        }

        // Level match, once per block: how much did the chain take out, and give it
        // back.  The comparison has to be made between two *smoothed* energies, not
        // between this block's in and out: a block that lands in a breath gap is
        // almost digital silence, and silence divided by silence is a random number
        // that used to be clamped to +-6 dB and thrown at the gain every 10 ms.
        // Both averages leak with the same 1.5 s time constant, so normal playing
        // (a louder line, a phrase) moves them together and cancels out; only the
        // chain's own loss remains.
        if (applyGain_ >= 1.0 && inSq / double(n) > 3.0e-6) {   // fade done, and not a gap
            const double a = 1.0 - std::exp(-double(n) / (fs_ * 1.5));
            inAvg_ += a * (inSq / double(n) - inAvg_);
            outAvg_ += a * (outSq / double(n) - outAvg_);
            if (inAvg_ > 1e-9 && outAvg_ > 1e-9) {
                const double lossDb = 10.0 * std::log10(inAvg_ / outAvg_);
                outGain_ = std::pow(10.0, std::clamp(lossDb, -6.0, 6.0) / 20.0);
            }
        }

        // Loudness, smoothed over ~150 ms, published for the dynamic layers: they hold a
        // harmonic *relative to how loud the voice is*, by the corpus slope.  Smoothed
        // because a per-block number jumps with the waveform and would make every layer
        // ride single notes.
        {
            const double ms = inSq / double(n * (right ? 2 : 1));
            const double loud = 10.0 * std::log10(std::max(ms, 1e-14));
            const double a = 1.0 - std::exp(-double(n) / (fs_ * 0.15));
            loudSm_ += a * (loud - loudSm_);
            loudPub_.store(loudSm_, std::memory_order_relaxed);
        }
    }

    // ------------------------------------------------------------- analysis
    // Call from a timer (message/timer thread) - never from the audio thread.
    // Move whatever the audio thread wrote into the analyser without solving.
    // The plugin calls this between solves: the measurements stay current, and the
    // expensive part (solver + detector + problem text) runs at its own pace.
    void drainPending() {
        std::lock_guard<std::mutex> lk(analyzeMx_);
        drainFromAudioThread();
    }

    // Called ~25-30 times a second from the plugin's timer: read whatever the audio
    // thread wrote, then publish the *display* spectrum.  No solver, no detector, no
    // strings - this is what makes the analyser move with the music instead of
    // updating only when a solve happens (four times a second, which the user heard
    // as the analyser being slow).
    void pump() {
        std::lock_guard<std::mutex> lk(analyzeMx_);
        drainFromAudioThread();
        if (! analyzer_.hasSpectrum()) return;
        publishLiveLocked();
    }

    LiveSpectrum live() const {
        std::lock_guard<std::mutex> lk(liveMx_);
        return live_;
    }

    void refresh() {
        std::lock_guard<std::mutex> lk(analyzeMx_);
        drainFromAudioThread();
        refreshLocked();
    }

    // Everything the audio thread wrote into the ring, handed to the analyser.  This
    // is the only place Analyzer::processBlock is called from now, and it runs on
    // whichever thread called refresh() - in the plugin, the 4 Hz timer.
    void drainFromAudioThread() {
        for (;;) {
            const unsigned long long r = fifoRead_.load(std::memory_order_relaxed);
            const unsigned long long w = fifoWrite_.load(std::memory_order_acquire);
            if (r >= w) break;
            const size_t n = size_t(std::min<unsigned long long>(w - r, (unsigned long long) kMaxChunk));
            std::array<float, kMaxChunk> chunk{};
            for (size_t i = 0; i < n; ++i) chunk[i] = fifo_[size_t((r + i) & kFifoMask)];
            fifoRead_.store(r + n, std::memory_order_release);
            analyzer_.processBlock(chunk.data(), int(n));
            learnProgress_ = analyzer_.activeFraction();
        }
    }

    // Which blocks may call refresh(): the background thread (plugin) or the
    // main thread (CLI).  Never the audio thread - that is what pollAndApply is.
    void publishLiveLocked() {
        std::lock_guard<std::mutex> lk(liveMx_);
        live_.freqs = analyzer_.grid().f;
        live_.liveDb = analyzer_.liveLevelsDb();
        live_.peakDb = analyzer_.peakHoldDb();
        live_.valid = true;
        ++live_.version;
    }

    void refreshLocked() {
        if (!analyzer_.ready()) return;
        publishLiveLocked();
        // Frozen means frozen: once committed, the measurement keeps running (the
        // editor's meter and the dynamic layers read it) but the decision is not
        // re-solved.  Re-solving after the user stopped the pass is exactly the
        // behaviour they asked to remove.
        if (frozen()) {
            const double heard = analyzer_.activeSeconds();
            if (heard > 0.0) {
                std::lock_guard<std::mutex> lk(stateMx_);
                state_.analyzedSeconds = heard;      // the counter is a meter, not a decision
            }
            return;
        }
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
        // Restore may not close the top.  A wide cut above 6 kHz is a different
        // record, not a fix: the user's own words were that the take came back
        // "eaten", and the air is the first thing missed.  1 dB is the most the
        // automatic path is allowed to take off the top of a voice.
        if (sc.mode == MatchMode::Restore)
            for (auto& b : corr)
                if (b.gainDb < 0.0 && b.f0 > 6000.0) b.gainDb = std::max(b.gainDb, -1.0);

        // ---- narrow cuts for static resonances (surgical, and bounded) -----
        //
        // These were unbounded in practice: 7 dB each, up to 8 dB in total, Q 8, and
        // nothing stopped two of them landing inside the singer's fundamental region
        // or on top of a broad corrective cut.  On a real vocal that is heard as the
        // voice being hollowed out - "بوّظ الصوت وأكل حاجات" - because the body of a
        // note lives in exactly the 150-500 Hz those notches were hitting.
        //
        // A narrow cut now: is at most 2 dB and never sharper than Q 1.5, is never
        // below 1.15x the fundamental, is never on a harmonic of the sung pitch, never
        // inside 1/3 octave of a band that already cuts, and the set is capped at 4 dB
        // in total - two bands, not five.
        std::vector<BandSpec> reso;
        double resoTotal = 0.0;
        const double bodyFloor = (m.medianF0 > 0.0 ? 1.15 * m.medianF0 : 150.0);
        for (const auto& p : probs) {
            if (p.action != Action::CutNarrow) continue;
            if (resoTotal >= 4.0 || reso.size() >= 2) break;
            if (p.freq < bodyFloor) continue;                       // never the fundamental
            // Never cut a harmonic of the note being sung.  570 Hz and 761 Hz on a
            // 190 Hz voice are not room resonances, they are the third and fourth
            // partials - the singer.  A Q8 notch there is heard as the voice going
            // thin, however surgical the graph looks.
            if (m.medianF0 > 0.0) {
                const double k = std::round(p.freq / m.medianF0);
                if (k >= 2.0 && std::abs(p.freq / m.medianF0 - k) / k < 0.03) continue;
            }
            bool clashes = false;
            for (const auto& c : corr)
                if (std::abs(std::log2(c.f0 / p.freq)) < 1.0 / 3.0 && c.gainDb < 0) clashes = true;
            for (const auto& r : reso)
                if (std::abs(std::log2(r.f0 / p.freq)) < 1.0 / 3.0) clashes = true;
            if (clashes) continue;

            // Tame, do not remove.  A Q8 / -7 dB notch is a *manual* tool: on a
            // voice it deletes one partial of the note, which is heard as a hole
            // that moves with the melody.  Automatic resonance control here stays
            // broad (Q <= 1.5) and shallow (-2 dB), so it lowers a ring without
            // deleting a harmonic.
            BandSpec b{BandType::Peak, p.freq, std::clamp(p.q, 0.7, 1.5), std::max(p.gainDb, -2.0)};
            b.gainDb *= amt;
            if (std::abs(b.gainDb) < 0.5) continue;
            reso.push_back(b);
            resoTotal += std::abs(b.gainDb);
        }

        // ---- body budget -------------------------------------------------
        // 150-500 Hz holds the fundamental and the first two formants: the body of
        // the note.  Every band down there was individually gentle and inside its
        // own limit, and they still added up to ~9 dB of cut on the test take - the
        // exact thing the user described as the plugin eating the vocal.  So the low
        // end gets a single budget, shared proportionally by the bands that ask.
        {
            constexpr double kBodyBudgetDb = 2.5;
            constexpr double kBodyTopHz = 600.0;
            double asked = 0.0;
            for (const auto& b : corr) if (b.f0 < kBodyTopHz && b.gainDb < 0.0) asked += -b.gainDb;
            for (const auto& b : reso) if (b.f0 < kBodyTopHz && b.gainDb < 0.0) asked += -b.gainDb;
            if (asked > kBodyBudgetDb) {
                const double k = kBodyBudgetDb / asked;
                for (auto& b : corr) if (b.f0 < kBodyTopHz && b.gainDb < 0.0) b.gainDb *= k;
                for (auto& b : reso) if (b.f0 < kBodyTopHz && b.gainDb < 0.0) b.gainDb *= k;
            }
        }

        const double before = EqSolver::distanceToTarget(in, {}, fs_);
        const double after  = EqSolver::distanceToTarget(in, corr, fs_);
        // honest secondary metric: did we actually fix the regions we chose to
        // touch?  (the overall distance can rise slightly when the dead-zone
        // refuses to chase the family's natural variation - that is by design)
        const double fixBefore = EqSolver::distanceOnActive(in, {}, fs_, 2.5);
        const double fixAfter  = EqSolver::distanceOnActive(in, corr, fs_, 2.5);

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
            // A take that has been reported as boomy gets a corner that actually
            // addresses the boom: at least 95 Hz, and never more than the pitch cap
            // below.  Previously the corner followed the *measured* low end, and a
            // take with a +6 dB boom under 110 Hz measures as "no roll-off", so the
            // corner stayed at 80 Hz and nothing was removed.
            if (hasRumble || measured < 100.0) corner = std::min(standard, std::max(measured, 95.0));
            else corner = std::min(measured, standard);
            // The fundamental is the one thing this must never eat, so the cap is
            // applied *always*.  It used to depend on the pitch having been detected,
            // and when it had not - a breathy verse, a whisper, a take the gate did not
            // believe - the corner fell back to the corpus frame of 172 Hz.  On a 196 Hz
            // voice that removes the body of every note; the user heard it as the plugin
            // eating the vocal, and was right.
            const double f0Cap = m.medianF0 > 0.0 ? std::max(85.0, 0.62 * m.medianF0) : 120.0;
            corner = std::min(corner, f0Cap);
        }
        corner = std::clamp(corner, 30.0, 220.0);

        // de-esser decision
        double deThr = -30.0, deGr = 0.0;
        for (const auto& p : probs) {
            if (p.action == Action::DeEss) { deGr = std::min(1.5, std::abs(p.gainDb) * 0.6); deThr = -34.0; }
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
            version_.fetch_add(1, std::memory_order_release);
        }

        // Dynamic layers from the *take*, not from a guess: any learned cut that sits
        // on a harmonic of the measured f0 becomes a band that follows that harmonic
        // and holds it at the level it had after the cut.  That is the moving band the
        // user asked for, and it is why "hold it at a fixed level" needs no threshold -
        // the target is the level this singer's take actually had.
        std::vector<TrackedSpec> layers;
        {
            std::vector<BandSpec> all = corr;
            all.insert(all.end(), reso.begin(), reso.end());
            layers = chooseTrackedLayers(all, m.medianF0, m.freqs, m.bandDb);
            std::lock_guard<std::mutex> lk(stateMx_);
            state_.trackedLayers = int(layers.size());
            version_.fetch_add(1, std::memory_order_release);
        }

        // hand the solution to the audio thread; it applies it at the top of the
        // next block, so no processing object is ever written from here
        {
            std::lock_guard<std::mutex> lk(pendingMx_);
            pendingCount_ = activeBandsIntoLocked(pendingBands_.data(), kBands);
            pendingHpf_ = corner;
            pendingDeEssOn_ = deGr > 0.05;
            pendingDeThr_ = deThr;
            pendingDeGr_ = deGr;
            pendingSettled_ = state_.settled;
            const int nLayers = std::min<int>(int(layers.size()), kTrackedMax);
            for (int i = 0; i < nLayers; ++i) pendingTrack_[size_t(i)] = layers[size_t(i)];
            pendingTrackCount_ = nLayers;

            ++pendingVersion_;
        }
        pendingSeen_.store(false);

        // Automatic commit: the CLI and the older automatic gates ask for a decision
        // after N seconds of sung audio; the plugin sets this as a *safety cap* so a
        // forgotten Stop button cannot leave the tool listening forever.
        if (settings_.settleSeconds > 0.0 && state_.settled) {
            phase_.store(int(Phase::Committed), std::memory_order_release);
            frozen_.store(true, std::memory_order_release);
        }
    }

    // apply a pending correction (audio thread, called from processBlock)
    void pollAndApply() {
        const bool trimsMoved = trimsDirty_.exchange(false, std::memory_order_acq_rel);
        if (pendingSeen_.load(std::memory_order_acquire) && !trimsMoved) return;
        std::unique_lock<std::mutex> lk(pendingMx_, std::try_to_lock);
        if (!lk.owns_lock()) {                       // solver is writing; retry next block
            if (trimsMoved) trimsDirty_.store(true, std::memory_order_release);
            return;
        }
        // Analyse-then-apply: until enough of the performance has been heard,
        // only the manual taste moves reach the signal path.  The corrective
        // curve would otherwise be a different filter every few seconds during
        // the first pass, which is both audible and dishonest.
        // The signal path follows the *phase*, not a flag in the pending struct: a
        // committed decision applies even if the solve that produced it ran a moment
        // before the user pressed stop.
        const bool settled = committed() || settings_.settleSeconds <= 0.0;
        std::array<BandSpec, kBands> bands{};
        int count = 0;
        if (settled) {
            count = std::min(pendingCount_, kBands);
            for (int i = 0; i < count; ++i) bands[size_t(i)] = pendingBands_[size_t(i)];
        } else {
            count = manualBandsInto(bands.data(), kBands);
        }
        for (int i = 0; i < count && i < int(trimDb_.size()); ++i) {
            const double t = trimDb_[size_t(i)].load(std::memory_order_relaxed);
            if (std::abs(t) > 0.001) bands[size_t(i)].gainDb = static_cast<float>(std::clamp<double>(bands[size_t(i)].gainDb + t, -18.0, 18.0));
        }
        eqLeft_.setBands(bands.data(), count, fs_);
        eqRight_.setBands(bands.data(), count, fs_);
        // the dynamic layers travel in the same pending slot as the curve: decided
        // together at commit, adopted together here, never written from another thread
        trackedActive_ = std::min(pendingTrackCount_, kTrackedMax);
        for (int i = 0; i < trackedActive_; ++i) {
            const TrackedSpec& sp = pendingTrack_[size_t(i)];
            TrackedBand::Tuning t;
            t.mode = TrackedBand::Mode::Pitch;
            t.harmonic = sp.harmonic;
            t.q = sp.q;
            t.detectQ = sp.detectQ;
            t.targetDb = sp.targetDb;
            t.rangeDown = sp.rangeDown;
            t.rangeUp = sp.rangeUp;
            t.glideMs = sp.glideMs;
            t.holdOffSec = 0.25;
            t.deadBandDb = sp.deadBandDb;
            t.slopeDb = sp.slopeDb;
            t.loudRefDb = sp.loudRefDb;
            t.attackMs = sp.attackMs;
            t.releaseMs = sp.releaseMs;
            auto& b = tracked_[size_t(i)];
            b.setTuning(t);
            b.reset();
            trackSpec_[size_t(i)] = sp;
        }
        hpf_.set(settled && pendingHpf_ > 0 ? pendingHpf_ : 70.0, fs_);
        deEsser_.setEnabled(settled && pendingDeEssOn_);
        if (settled) deEsser_.setParameters(pendingDeThr_, pendingDeGr_);
        pendingSeen_.store(true, std::memory_order_release);
    }

    // ------------------------------------------------------------- settings
    void setSettings(const RestoreSettings& s) {
        settings_ = s;
        // "settle 0" has always meant "apply as soon as a decision exists" - the CLI
        // and the offline gates depend on it.  Keep that meaning: the phase starts
        // committed, with no fade, so a measurement harness sees the steady state.
        if (s.settleSeconds <= 0.0) {
            phase_.store(int(Phase::Committed), std::memory_order_release);
            applyGain_ = 1.0;
        }
        updateBands();
        version_.fetch_add(1, std::memory_order_release);
    }
    const RestoreSettings& settings() const { return settings_; }

    RestoreState state() const {
        std::lock_guard<std::mutex> lk(stateMx_);
        return state_;
    }
    Measurements measurements() const {
        std::lock_guard<std::mutex> lk(stateMx_);
        return measurements_;
    }
    // Bumped whenever the published state changes.  The editor polls this integer
    // instead of copying the state, the measurements, the band list and the problem
    // texts twelve times a second - at 4 Hz of real change that was three quarters of
    // the UI thread's work thrown away, and it showed up as a window that stuttered
    // while the audio played.
    long version() const { return version_.load(std::memory_order_acquire); }
    double learnProgress() const { return learnProgress_.load(); }
    long   samplesAnalysed() const { return analyzer_.samplesSeen(); }
    Analyzer& analyzer() { return analyzer_; }

    // the full correction that is actually running (for the UI curve / delta)
    // The UI draws the correction as movable handles.  A trim is a gain offset the
    // engineer adds on top of what the analysis proposed: it never changes the
    // measurement, only what is applied, and it survives a re-analysis because it
    // is keyed by band index rather than by frequency.
    // Called from the editor thread.  It must NOT rebuild the cascade here: the
    // audio thread is reading it, and replacing coefficients (and reallocating the
    // band list) under it is a race that ends in a crash inside the DAW.  The flag
    // is picked up by pollAndApply() on the audio thread, which is the only place
    // that is allowed to touch the processing objects.
    void setBandTrim(int index, double db) {
        if (index < 0 || index >= static_cast<int>(trimDb_.size())) return;
        trimDb_[index].store(std::clamp(db, -12.0, 12.0));
        trimsDirty_.store(true, std::memory_order_release);
        version_.fetch_add(1, std::memory_order_release);
    }
    double bandTrim(int index) const {
        if (index < 0 || index >= static_cast<int>(trimDb_.size())) return 0.0;
        return trimDb_[index].load();
    }
    void clearTrims() {
        for (auto& t : trimDb_) t.store(0.0);
        trimsDirty_.store(true, std::memory_order_release);
        version_.fetch_add(1, std::memory_order_release);
    }

    std::vector<BandSpec> activeBands() const {
        std::lock_guard<std::mutex> lk(stateMx_);
        return activeBandsLocked();
    }

    // Bands that are actually in the cascade.  activeBands() includes the six taste
    // bands the corpus study puts in the chain; at rest they are all 0 dB and the
    // cascade skips them, so counting the list said "10 bands" when seven were doing
    // nothing.  The number the window shows is this one.
public:
    // How much gain the level match is holding, in dB.  Shown in the editor so the
    // "auto gain" is a number the user can see, not a hidden fudge.
    double levelMatchDb() const { return 20.0 * std::log10(std::max(outGain_, 1e-6)); }

    // Bands in the signal path.  During the learn pass the solver already has a
    // curve, but nothing of it is in the audio - so the honest answer is zero.
    int appliedBandCount() const {
        if (!committed()) return 0;
        return appliedBandCountInternal();
    }

private:
    int appliedBandCountInternal() const {
        auto active = activeBands();
        int n = 0;
        for (const auto& b : active) if (std::abs(b.gainDb) > 0.05) ++n;
        return n;
    }

    // The bands the *user* asked for, independent of any analysis.  Written into a
    // caller-provided array: this runs on the audio thread, and returning a vector
    // from it meant a heap allocation inside every block that had no solution yet.
    int manualBandsInto(BandSpec* out, int cap) const {
        int n = 0;
        auto push = [&](BandType t, double f, double q, double g) {
            if (n < cap) out[n++] = BandSpec{t, f, q, g};
        };
        if (std::abs(settings_.airTrimDb) > 0.01) push(BandType::HighShelf, 9000.0, 0.7, settings_.airTrimDb);
        push(BandType::Peak, 250.0, 0.9, 2.5 * settings_.axis1);   // PC1 warm
        push(BandType::Peak, 330.0, 0.9, 1.5 * settings_.axis1);
        push(BandType::Peak, 3500.0, 0.8, 1.2 * settings_.axis1);  // PC1 presence
        push(BandType::Peak, 200.0, 1.0, 2.0 * settings_.axis2);   // PC2 body
        push(BandType::Peak, 2200.0, 1.2, 1.5 * settings_.axis2);
        return n;
    }

    // message-thread convenience
    std::vector<BandSpec> manualBands() const {
        std::array<BandSpec, kBands> b{};
        const int n = manualBandsInto(b.data(), kBands);
        return std::vector<BandSpec>(b.begin(), b.begin() + n);
    }

    // Writes the running correction into a fixed array (audio thread, no heap).
    // state_ is read by the caller under its own lock when it needs one.
    int activeBandsIntoLocked(BandSpec* out, int cap) const {
        int n = 0;
        auto push = [&](const BandSpec& b) { if (n < cap) out[n++] = b; };
        for (size_t i = 0; i < state_.corrective.size() && n < cap; ++i) {
            BandSpec b = state_.corrective[i];
            if (i < trimDb_.size()) {
                const double t = trimDb_[i].load();
                if (std::abs(t) > 0.001) b.gainDb = float(std::clamp<double>(b.gainDb + t, -18.0, 18.0));
            }
            push(b);
        }
        for (const auto& r : state_.resonance) push(r);
        return n + manualBandsInto(out + n, cap - n);
    }

    std::vector<BandSpec> activeBandsLocked() const {
        std::array<BandSpec, kBands> b{};
        const int n = activeBandsIntoLocked(b.data(), kBands);
        return std::vector<BandSpec>(b.begin(), b.begin() + n);
    }

    double hpfFreq() const { std::lock_guard<std::mutex> lk(stateMx_); return state_.hpfHz; }

    // magnitude of the 24 dB/oct Butterworth pair, for anything that needs to know
    // what the low cut is doing without touching the running object
    static double hpfDbAt(double f, double corner) {
        if (corner <= 0.0 || f <= 0.0) return 0.0;
        const double r = f / corner;
        return -10.0 * std::log10(1.0 + std::pow(r, -4.0)) * 2.0;
    }
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
    // Which learned bands deserve to become a moving band?  Only the ones that sit on
    // a harmonic of the note (within 3 %, i.e. closer than half a semitone), that
    // actually do something (>= 1 dB), and that are inside the range where a harmonic
    // of a voice can live.  The strongest few win.
    std::vector<TrackedSpec> chooseTrackedLayers(const std::vector<BandSpec>& bands,
                                                 double medianF0,
                                                 const std::vector<double>& freqs,
                                                 const std::vector<double>& levelDb) const {
        std::vector<TrackedSpec> out;
        if (medianF0 <= 0.0 || freqs.empty() || levelDb.empty()) return out;
        // The median f0 is reported at the lower edge of a 10 Hz histogram bin, so add
        // half a bin before calling it a harmonic reference: on a 196 Hz voice that is
        // 2.5 %, and at the third harmonic it is the difference between the band landing
        // on the note and landing between two of them.
        medianF0 += 5.0;
        std::vector<int> usedHarmonic;
        struct Cand { double strength; TrackedSpec spec; };
        std::vector<Cand> cands;
        for (const auto& b : bands) {
            if (b.f0 < 80.0 || b.f0 > 8000.0) continue;
            // only cuts: a boost is a static decision about the whole take, and a
            // dynamic boost on a harmonic is a noise amplifier between notes
            if (b.gainDb > -3.0) continue;
            const int k = int(std::lround(b.f0 / medianF0));
            if (k < 1 || k > 8) continue;
            // 5 %: the median f0 comes from a 10 Hz histogram, so at the third harmonic
            // it is already worth ~2.5 %.  The spacing between harmonics is 1/k (33 % at
            // k = 3), so 5 % cannot capture a neighbour.
            if (std::abs(b.f0 / (k * medianF0) - 1.0) > 0.05) continue;
            if (std::find(usedHarmonic.begin(), usedHarmonic.end(), k) != usedHarmonic.end()) continue;
            usedHarmonic.push_back(k);
            TrackedSpec sp;
            sp.harmonic = k;
            // Narrower than the spacing between harmonics, or the band would pull its
            // neighbours with it: at harmonic k the spacing is f0, so Q has to exceed
            // ~1.25 k for the band's skirt to stay inside one harmonic.
            sp.q = std::clamp(1.6 * double(k), 4.0, 16.0);
            // The layer sits after the curve, so its target is the level the take had
            // *after* the cut: a louder note is pulled back to where the corrected take
            // sat, not to where the raw take sat.
            sp.targetDb = levelAt(freqs, levelDb, b.f0) + b.gainDb;   // where the curve puts it
            // How much this harmonic is allowed to travel with loudness, from the corpus
            // table.  Capped at 1.0: a slope above 1 describes his mids, which move *more*
            // than the voice does, and matching that would mean boosting when the singer
            // pushes - on a dry vocal that lifts the noise floor with the harmonic, so the
            // automatic path never expands.  The corpus can therefore make a hold tighter
            // (0.19 at 100 Hz, 0.65 at 10 kHz) and never looser.
            sp.slopeDb = std::min(1.0, corpusSlope(b.f0));
            sp.loudRefDb = loudPub_.load(std::memory_order_relaxed);
            sp.rangeDown = std::clamp(2.0 - b.gainDb, 2.0, 6.0);
            sp.rangeUp = 0.0;
            cands.push_back({std::abs(b.gainDb), sp});
        }
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b) { return a.strength > b.strength; });
        // Two, not four.  The user's verdict on an earlier, busier version was that it
        // ate the voice ("بوّظ الصوت وأكل حاجات كتير"); a dynamic layer is the most
        // intrusive thing in the chain, so it gets the two strongest moves and no more.
        for (size_t i = 0; i < cands.size() && int(out.size()) < 2; ++i)
            out.push_back(cands[i].spec);
        return out;
    }

    // level of the measurement grid at an arbitrary frequency (log-x interpolation)
    static double levelAt(const std::vector<double>& freqs, const std::vector<double>& db, double f) {
        if (freqs.empty()) return -30.0;
        if (f <= freqs.front()) return db.front();
        if (f >= freqs.back()) return db.back();
        for (size_t i = 1; i < freqs.size(); ++i) {
            if (f <= freqs[i]) {
                const double t = std::log(f / freqs[i - 1]) / std::log(freqs[i] / freqs[i - 1]);
                return db[i - 1] + t * (db[i] - db[i - 1]);
            }
        }
        return db.back();
    }

    void updateBands() {
        std::array<BandSpec, kBands> bands{};
        int count = 0;
        {
            std::lock_guard<std::mutex> lk(stateMx_);
            count = activeBandsIntoLocked(bands.data(), kBands);
        }
        eqLeft_.setBands(bands.data(), count, fs_);
        eqRight_.setBands(bands.data(), count, fs_);
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
    mutable std::mutex liveMx_;
    LiveSpectrum live_;
    Analyzer analyzer_;
    HighPass24 hpf_;
    BiquadCascade eqLeft_, eqRight_;
    DeEsser deEsser_;
    std::array<TrackedBand, kTrackedMax> tracked_{};   // audio thread only
    TrackedSpec trackSpec_[kTrackedMax]{};
    int trackedActive_ = 0;
    double loudSm_ = -30.0;
    std::atomic<double> loudPub_{-30.0};
    std::atomic<double> trackedPubLevel_[kTrackedMax];
    std::atomic<double> trackedPubGain_[kTrackedMax];
    std::atomic<double> trackedPubHz_[kTrackedMax];
    // decided on the analysis thread, adopted by the audio thread in pollAndApply()
    TrackedSpec pendingTrack_[kTrackedMax]{};
    int pendingTrackCount_ = 0;
    EqSolver solver_;
    ProblemDetector detector_;
    static constexpr int kMaxChunk = 2048;         // 8 KB of stack-adjacent member
    std::array<float, kMaxChunk> scratchChunk_{};  // drain scratch (analysis thread)
    // audio-thread scratch for the wet path: the dynamic layers need a whole block and
    // sit after the static curve, so the curve's output is built here first
    std::array<float, kMaxChunk> wetChL_{}, wetChR_{}, dryChL_{}, dryChR_{};

    // Audio thread -> analysis thread.  1.36 s at 48 kHz, drained four times a
    // second: a slow solve can never make the audio thread wait, and if the ring ever
    // does fill up the samples are counted as dropped instead of overwriting the
    // reader's position.
    //
    // NOTE: with the analyser and this ring inside it, RestoreCore is ~470 KB.  It is
    // a *member* of the processor, which is correct; it must not be a local.  The
    // Windows gate found that the hard way - 0xC00000FD, stack overflow, in the test
    // that created three of them on the stack.
    static constexpr int kFifoBits = 16;          // 65 536 samples = 1.36 s at 48 kHz
    static constexpr int kFifoSize = 1 << kFifoBits;
    static constexpr int kFifoMask = kFifoSize - 1;
    std::array<float, kFifoSize> fifo_{};
    std::atomic<unsigned long long> fifoWrite_{0}, fifoRead_{0};
    std::atomic<long> fifoDropped_{0};
    std::vector<float> scratch_;                    // legacy: sized by prepareToPlay
    std::vector<int> tmpCount_;
    std::array<std::atomic<double>, 8> trimDb_{};   // UI gain offsets, per corrective band
    std::atomic<bool> trimsDirty_{false};           // the editor moved a handle
    std::atomic<double> learnProgress_{0.0};
    std::atomic<long> version_{0};
    std::atomic<int> phase_{int(Phase::Learning)};
    std::atomic<bool> frozen_{false};
    double applyGain_ = 0.0;           // 0 -> 1 over 200 ms when a decision commits
    double outGain_ = 1.0;             // level match, see the loop in processBlock
    double inAvg_ = 0.0, outAvg_ = 0.0;  // smoothed mean square, in and out of the chain

    mutable std::mutex stateMx_;
    std::mutex analyzeMx_;                    // guards Analyzer (audio <-> solver)
    std::mutex pendingMx_;                    // guards the hand-off below
    static constexpr int kBands = 24;                   // fixed band capacity, no heap
    std::array<BandSpec, kBands> pendingBands_{};
    int pendingCount_ = 0;
    double pendingHpf_ = 70.0, pendingDeThr_ = -30.0, pendingDeGr_ = 0.0;
    bool pendingDeEssOn_ = false;
    bool pendingSettled_ = true;
    std::atomic<bool> pendingSeen_{true};
    std::atomic<long> pendingVersion_{0};
    RestoreState state_;
    Measurements measurements_;
};

// It is big by design (analyser ring, FFT buffers, the audio FIFO), and it is a
// member of the processor for a reason.  If this ever fires, the object has become
// too large to be sane anywhere near a stack - heap it, or shrink what grew.
static_assert(sizeof(RestoreCore) < 768 * 1024, "RestoreCore is heap-only; see kFifoBits");

} // namespace aeq
